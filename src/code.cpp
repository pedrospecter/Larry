#include "larry/code.hpp"

#include "larry/base_rules.hpp"

#include <algorithm>
#include <format>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>

namespace larry {

namespace {

std::string text_of(const Bytes& b) {
    return {b.begin(), b.end()};
}

std::string trimmed(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r' || text.back() == ';')) {
        text.remove_suffix(1);
    }
    return std::string{text};
}

bool is_identifier_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

bool is_identifier(std::string_view word) {
    return !word.empty() && !(word.front() >= '0' && word.front() <= '9') &&
           std::ranges::all_of(word, is_identifier_char);
}

/// The rules of one language: shapes by kind, the keywords, the comment mark, the block kind.
struct Rules {
    std::map<std::string, std::vector<std::string>> shapes;  // "function", "class", "uses"
    std::vector<std::string> keywords;
    std::string comment;
    std::string block = "braces";
};

Rules rules_of(const std::filesystem::path& file) {
    Rules out;
    for (const auto& [kind, value] : BaseRules::read_pairs(file)) {
        const std::string k = text_of(kind);
        const std::string v = text_of(value);
        if (k == "keywords") {
            std::istringstream words{v};
            for (std::string word; words >> word;) {
                out.keywords.push_back(word);
            }
        } else if (k == "comment") {
            out.comment = v;
        } else if (k == "block") {
            out.block = v;
        } else {
            out.shapes[k].push_back(v);
        }
    }
    return out;
}

/// The name a shape with '*' takes from a line, when the line has the shape:
/// at its start, or anywhere when `anywhere` ("require(" inside a line).
std::optional<std::string> name_by_shape(const std::string& line, const std::string& shape, bool anywhere = false) {
    const std::size_t star = shape.find('*');
    if (star == std::string::npos) {
        return std::nullopt;
    }
    const std::string prefix = shape.substr(0, star);
    const std::string suffix = shape.substr(star + 1);
    const std::size_t at = anywhere ? line.find(prefix) : (line.starts_with(prefix) ? 0 : std::string::npos);
    if (at == std::string::npos) {
        return std::nullopt;
    }
    const std::string rest = line.substr(at + prefix.size());
    std::size_t end = suffix.empty() ? rest.size() : rest.find(suffix);
    if (end == std::string::npos && suffix == ";") {
        end = rest.size();  // the line's own ';' was trimmed
    }
    if (end == std::string::npos) {
        return std::nullopt;
    }
    std::string name = trimmed(rest.substr(0, end));
    while (!name.empty() && (name.front() == '"' || name.front() == '\'')) {
        name.erase(name.begin());
    }
    while (!name.empty() && (name.back() == '"' || name.back() == '\'')) {
        name.pop_back();
    }
    // "class Reader(Base)" and "import os, sys": the first word only.
    const std::size_t space = name.find_first_of(" (:,");
    if (space != std::string::npos) {
        name = name.substr(0, space);
    }
    if (name.empty()) {
        return std::nullopt;
    }
    return name;
}

/// C++ and C#: a line with a type, a name and '(' that opens a body: "int count(const std::string& text) {".
std::optional<std::string> function_by_type(const std::string& line, const Rules& rules) {
    if (line.empty() || line.front() == '#' || line.front() == '@' || line.front() == '[' || line.back() == ';' ||
        line.find('(') == std::string::npos || line.find(" = ") != std::string::npos) {
        return std::nullopt;
    }
    const std::size_t open = line.find('(');
    std::string before = trimmed(line.substr(0, open));
    if (before.find(' ') == std::string::npos) {
        return std::nullopt;  // no type: a call, "if (", "while ("
    }
    const std::size_t space = before.rfind(' ');
    std::string name = before.substr(space + 1);
    const std::size_t scope = name.rfind("::");
    if (scope != std::string::npos) {
        name = name.substr(scope + 2);
    }
    const std::size_t dot = name.rfind('.');
    if (dot != std::string::npos) {
        name = name.substr(dot + 1);
    }
    if (!is_identifier(name) || std::ranges::contains(rules.keywords, name)) {
        return std::nullopt;
    }
    const std::string first = before.substr(0, before.find(' '));
    if (first == "namespace" || first == "using" || first == "return" || first == "else" ||
        std::ranges::contains(rules.keywords, first)) {
        return std::nullopt;
    }
    // Opens a body on this line or the next: ends with '{', ')', "const", "override", "noexcept".
    const std::string tail = trimmed(line.substr(line.rfind(')') == std::string::npos ? line.size() : line.rfind(')') + 1));
    if (!(line.back() == '{' || line.back() == ')' || tail == "const" || tail == "override" || tail == "noexcept" ||
          tail == "const override" || tail.ends_with("{"))) {
        return std::nullopt;
    }
    return name;
}

/// The names called in a line: an identifier before '(' that is no keyword.
std::vector<std::string> calls_in(const std::string& line, const Rules& rules) {
    std::vector<std::string> out;
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (line[i] != '(') {
            continue;
        }
        std::size_t end = i;
        while (end > 0 && line[end - 1] == ' ') {
            --end;
        }
        std::size_t start = end;
        while (start > 0 && is_identifier_char(line[start - 1])) {
            --start;
        }
        if (start == end) {
            continue;
        }
        const std::string name = line.substr(start, end - start);
        if (!is_identifier(name) || std::ranges::contains(rules.keywords, name)) {
            continue;
        }
        // Not a definition: "def name(" and "function name(".
        const std::string before = trimmed(line.substr(0, start));
        if (before.ends_with("def") || before.ends_with("function") || before.ends_with("class")) {
            continue;
        }
        if (!std::ranges::contains(out, name)) {
            out.push_back(name);
        }
    }
    return out;
}

std::size_t indent_of(const std::string& line) {
    std::size_t n = 0;
    while (n < line.size() && (line[n] == ' ' || line[n] == '\t')) {
        ++n;
    }
    return n;
}

}  // namespace

Code::Code(std::filesystem::path directory) : directory_(std::move(directory)) {
    if (directory_.empty()) {
        directory_ = std::filesystem::path{LARRY_BASE_RULES_DIR} / "code";
    }
}

std::string Code::language_of(const std::filesystem::path& file) {
    const std::string e = file.extension().string();
    if (e == ".py") {
        return "python";
    }
    if (e == ".cpp" || e == ".cc" || e == ".cxx" || e == ".hpp" || e == ".h" || e == ".hh" || e == ".ipp") {
        return "cpp";
    }
    if (e == ".js" || e == ".mjs" || e == ".cjs" || e == ".ts" || e == ".jsx" || e == ".tsx") {
        return "javascript";
    }
    if (e == ".cs") {
        return "csharp";
    }
    return {};
}

std::vector<std::string> Code::languages() const {
    std::vector<std::string> out;
    if (std::filesystem::is_directory(directory_)) {
        for (const auto& entry : std::filesystem::directory_iterator{directory_}) {
            if (entry.path().extension() == ".txt") {
                out.push_back(entry.path().stem().string());
            }
        }
    }
    std::ranges::sort(out);
    return out;
}

std::vector<CodeFact> Code::read(const std::filesystem::path& file) const {
    const std::string language = language_of(file);
    if (language.empty()) {
        throw std::runtime_error(std::format("Code: no rules for the language of {}", file.string()));
    }
    std::ifstream in{file, std::ios::binary};
    if (!in) {
        throw std::runtime_error(std::format("Code: cannot read {}", file.string()));
    }
    std::stringstream text;
    text << in.rdbuf();
    return read_text(text.str(), language, file.filename().string());
}

std::vector<CodeFact> Code::read_text(std::string_view text, std::string_view language, std::string_view name) const {
    const std::filesystem::path file = directory_ / (std::string{language} + ".txt");
    if (!std::filesystem::exists(file)) {
        throw std::runtime_error(std::format("Code: no rules for {} ({})", language, file.string()));
    }
    const Rules rules = rules_of(file);
    const bool by_indent = rules.block == "indent";
    std::vector<CodeFact> out;
    const auto say = [&](std::string sentence, int line) {
        if (!std::ranges::any_of(out, [&](const CodeFact& f) { return f.sentence == sentence; })) {
            out.push_back({std::move(sentence), line});
        }
    };
    // Where we are: the class and the function whose body holds the line.
    struct Scope {
        std::string name;
        std::size_t indent = 0;  // by indent: the indentation of its line
        int depth = 0;           // by braces: the depth its body starts at
        bool open = false;
    };
    Scope current_class;
    Scope current_function;
    int depth = 0;
    int number = 0;
    std::size_t from = 0;
    while (from <= text.size()) {
        const std::size_t at = text.find('\n', from);
        const std::string raw(text.substr(from, at == std::string_view::npos ? std::string_view::npos : at - from));
        from = at == std::string_view::npos ? text.size() + 1 : at + 1;
        ++number;
        std::string line = raw;
        if (!rules.comment.empty()) {
            const std::size_t c = line.find(rules.comment);
            if (c != std::string::npos && (c == 0 || line[c - 1] == ' ' || line[c - 1] == '\t')) {
                line = line.substr(0, c);
            }
        }
        const std::size_t indent = indent_of(line);
        line = trimmed(line);
        if (line.empty()) {
            continue;
        }
        // Scopes that ended, by indentation.
        if (by_indent) {
            if (current_function.open && indent <= current_function.indent) {
                current_function.open = false;
            }
            if (current_class.open && indent <= current_class.indent) {
                current_class.open = false;
            }
        }
        const bool in_function = current_function.open && (by_indent ? indent > current_function.indent : depth > current_function.depth);
        const bool in_class = current_class.open && (by_indent ? indent > current_class.indent : depth > current_class.depth);
        // Definitions and uses, by the shapes.
        std::optional<std::string> defined;
        std::string kind;
        // A shape at the start of the line first; then a "uses" shape anywhere in it.
        for (const bool anywhere : {false, true}) {
            for (const auto& [shape_kind, shapes] : rules.shapes) {
                if (anywhere && shape_kind != "uses") {
                    continue;
                }
                for (const std::string& shape : shapes) {
                    if (const std::optional<std::string> found = name_by_shape(line, shape, anywhere)) {
                        defined = found;
                        kind = shape_kind;
                        break;
                    }
                }
                if (defined) {
                    break;
                }
            }
            if (defined) {
                break;
            }
        }
        if (!defined && !rules.shapes.contains("function") && !in_function) {
            if (const std::optional<std::string> found = function_by_type(line, rules)) {
                defined = found;
                kind = "function";
            }
        }
        // A method written as "name(args) {" inside a class, with no keyword before it (JavaScript).
        if (!defined && in_class && !in_function && !by_indent && rules.shapes.contains("function")) {
            const std::size_t open = line.find('(');
            if (open != std::string::npos && open > 0 && (line.back() == '{' || line.back() == ')')) {
                const std::string candidate = line.substr(0, open);
                if (is_identifier(candidate) && !std::ranges::contains(rules.keywords, candidate)) {
                    defined = candidate;
                    kind = "function";
                }
            }
        }
        if (defined && kind == "uses") {
            say(std::format("{} uses {}.", name, *defined), number);
        } else if (defined && kind == "class") {
            say(std::format("{} is a class in {}.", *defined, name), number);
            current_class = {*defined, indent, depth, true};
            current_function.open = false;
        } else if (defined && kind == "function") {
            if (in_class) {
                say(std::format("{} is a method of {}.", *defined, current_class.name), number);
            } else {
                say(std::format("{} is a function in {}.", *defined, name), number);
            }
            current_function = {*defined, indent, depth, true};
        } else if (in_function) {
            for (const std::string& called : calls_in(line, rules)) {
                say(std::format("{} calls {}.", current_function.name, called), number);
            }
        }
        // The braces of this line, after the definitions it opens.
        if (!by_indent) {
            bool in_string = false;
            for (std::size_t i = 0; i < line.size(); ++i) {
                const char c = line[i];
                if (c == '"' && (i == 0 || line[i - 1] != '\\')) {
                    in_string = !in_string;
                } else if (!in_string && c == '{') {
                    ++depth;
                } else if (!in_string && c == '}') {
                    --depth;
                    if (current_function.open && depth <= current_function.depth) {
                        current_function.open = false;
                    }
                    if (current_class.open && depth <= current_class.depth) {
                        current_class.open = false;
                    }
                }
            }
        }
    }
    return out;
}

}  // namespace larry
