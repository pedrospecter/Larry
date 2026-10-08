#include "larry/web.hpp"

#include "larry/json.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <format>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

namespace larry {

namespace {


// The extract of the first page of a query answer.
const Json* first_page(const Json& answer) {
    const Json* query = answer.get("query");
    const Json* pages = query != nullptr ? query->get("pages") : nullptr;
    const JsonObject* object = pages != nullptr ? pages->object() : nullptr;
    if (object == nullptr || object->empty()) {
        return nullptr;
    }
    return &object->front().second;
}

std::string shell_quoted(std::string_view text) {
    std::string out = "'";
    for (const char c : text) {
        if (c == '\'') {
            out += "'\\''";
        } else {
            out.push_back(c);
        }
    }
    out += "'";
    return out;
}

std::string run(const std::string& command) {
    std::unique_ptr<FILE, int (*)(FILE*)> pipe{popen(command.c_str(), "r"), pclose};
    if (!pipe) {
        throw std::runtime_error("Web: cannot run curl");
    }
    std::string out;
    std::array<char, 4096> buffer{};
    for (std::size_t n = 0; (n = fread(buffer.data(), 1, buffer.size(), pipe.get())) > 0;) {
        out.append(buffer.data(), n);
    }
    return out;
}

std::string today() {
    const std::time_t now = std::time(nullptr);
    std::tm parts{};
#ifdef _WIN32
    gmtime_s(&parts, &now);
#else
    gmtime_r(&now, &parts);
#endif
    return std::format("{:04}-{:02}-{:02}", parts.tm_year + 1900, parts.tm_mon + 1, parts.tm_mday);
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) {
        s.remove_suffix(1);
    }
    return s;
}

// Lines too: for the whole of a text.
std::string_view trim_lines(std::string_view s) {
    while (!s.empty() && (s.front() == '\n' || s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == '\n' || s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) {
        s.remove_suffix(1);
    }
    return s;
}

std::string lower(std::string_view text) {
    std::string out{text};
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return out;
}

// Wiktionary's parts of speech, as Larry's categories.
const std::map<std::string, std::string> parts_of_speech = {
    {"noun", "noun"},           {"proper noun", "proper noun"}, {"verb", "verb"},
    {"adjective", "adjective"}, {"adverb", "adverb"},           {"pronoun", "pronoun"},
    {"preposition", "preposition"}, {"conjunction", "conjunction"}, {"determiner", "determiner"},
    {"article", "determiner"},  {"interjection", "interjection"}, {"numeral", "numeral"},
    {"number", "numeral"},      {"particle", "particle"},
};

const char* const wikipedia = "https://en.wikipedia.org";
const char* const wiktionary = "https://en.wiktionary.org";

}  // namespace

Web::Web(Language language, Transport transport, std::filesystem::path directory)
    : language_(language), transport_(std::move(transport)), directory_(std::move(directory)) {
    if (!transport_) {
        transport_ = [](const std::string& url) { return curl(url); };
    }
    if (directory_.empty()) {
        const char* const from_environment = std::getenv("LARRY_CONTENT_DIR");
        directory_ = from_environment != nullptr && *from_environment != '\0'
                         ? std::filesystem::path{from_environment}
                         : std::filesystem::path{LARRY_CONTENT_DIR} / locale(language_);
    }
}

std::string Web::user_agent() {
    return "Larry/0.1 (https://github.com/pedrospecter/larry)";
}

std::string Web::curl(const std::string& url) {
    const std::string command = std::format(
        "curl -sS -L --max-time 60 -A {} -w '\\n%{{http_code}}' {} 2>&1", shell_quoted(user_agent()),
        shell_quoted(url));
    for (int attempt = 0; attempt < 2; ++attempt) {
        std::string out = run(command);
        const std::size_t cut = out.rfind('\n');
        if (cut == std::string::npos) {
            throw std::runtime_error(std::format("Web: curl gave no answer for {}", url));
        }
        const std::string_view code = trim(std::string_view{out}.substr(cut + 1));
        std::string body = out.substr(0, cut);
        if (code == "200") {
            return body;
        }
        if (code == "429" && attempt == 0) {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            continue;
        }
        if (code == "000" || code.empty()) {
            throw std::runtime_error(std::format("Web: cannot reach {}: {}", url, trim(body)));
        }
        throw std::runtime_error(std::format("Web: {} answered {}", url, code));
    }
    throw std::runtime_error(std::format("Web: {} answered 429 twice: too many requests", url));
}

std::string Web::url_encode(std::string_view text) {
    static constexpr char digits[] = "0123456789ABCDEF";
    std::string out;
    for (const char ch : text) {
        const auto c = static_cast<unsigned char>(ch);
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
            c == '_' || c == '.' || c == '~') {
            out.push_back(ch);
        } else {
            out.push_back('%');
            out.push_back(digits[c >> 4]);
            out.push_back(digits[c & 0x0F]);
        }
    }
    return out;
}

bool Web::is_url(std::string_view text) {
    return text.starts_with("http://") || text.starts_with("https://");
}

std::string Web::slug(std::string_view title) {
    std::string out;
    bool dash = false;
    for (const char ch : title) {
        const auto c = static_cast<unsigned char>(ch);
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            out.push_back(ch);
            dash = false;
        } else if (c >= 'A' && c <= 'Z') {
            out.push_back(static_cast<char>(c - 'A' + 'a'));
            dash = false;
        } else if (c >= 0x80) {
            out.push_back(static_cast<char>(c));  // UTF-8 stays as it is
            dash = false;
        } else if (!out.empty() && !dash) {
            out.push_back('-');
            dash = true;
        }
    }
    while (!out.empty() && out.back() == '-') {
        out.pop_back();
    }
    return out.empty() ? "page" : out;
}

std::string Web::strip_html(std::string_view html) {
    std::string out;
    std::size_t i = 0;
    const auto starts = [&](std::string_view what) {
        return html.size() - i >= what.size() &&
               lower(html.substr(i, what.size())) == lower(what);
    };
    while (i < html.size()) {
        if (html[i] == '<') {
            // Scripts and styles go whole; a block tag is a break; the rest goes.
            for (const char* block : {"script", "style"}) {
                const std::string open = std::string{"<"} + block;
                if (starts(open)) {
                    const std::string close = std::string{"</"} + block;
                    std::size_t end = i;
                    for (; end < html.size(); ++end) {
                        if (html.size() - end >= close.size() && lower(html.substr(end, close.size())) == close) {
                            break;
                        }
                    }
                    i = end;
                }
            }
            const std::size_t end = html.find('>', i);
            const std::string tag = lower(html.substr(i + 1, end == std::string_view::npos ? 0 : end - i - 1));
            for (const char* block : {"p", "/p", "br", "br/", "br /", "div", "/div", "li", "h1", "h2", "h3", "h4", "tr", "/tr", "td", "/td"}) {
                if (tag == block || tag.starts_with(std::string{block} + " ")) {
                    out.push_back('\n');
                    break;
                }
            }
            i = end == std::string_view::npos ? html.size() : end + 1;
            continue;
        }
        if (html[i] == '&') {
            const std::size_t end = html.find(';', i);
            if (end != std::string_view::npos && end - i <= 8) {
                const std::string entity{html.substr(i + 1, end - i - 1)};
                static const std::map<std::string, std::string> entities = {
                    {"amp", "&"}, {"lt", "<"}, {"gt", ">"}, {"quot", "\""}, {"apos", "'"},
                    {"nbsp", " "}, {"#39", "'"}, {"#34", "\""}, {"ndash", "–"}, {"mdash", "—"}};
                if (const auto found = entities.find(entity); found != entities.end()) {
                    out += found->second;
                    i = end + 1;
                    continue;
                }
                if (entity.size() > 1 && entity.front() == '#') {
                    const unsigned code = entity[1] == 'x' || entity[1] == 'X'
                                              ? static_cast<unsigned>(std::strtoul(entity.c_str() + 2, nullptr, 16))
                                              : static_cast<unsigned>(std::strtoul(entity.c_str() + 1, nullptr, 10));
                    if (code > 0 && code < 0x110000) {
                        std::string utf8;
                        if (code < 0x80) {
                            utf8.push_back(static_cast<char>(code));
                        } else if (code < 0x800) {
                            utf8.push_back(static_cast<char>(0xC0 | (code >> 6)));
                            utf8.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                        } else if (code < 0x10000) {
                            utf8.push_back(static_cast<char>(0xE0 | (code >> 12)));
                            utf8.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                            utf8.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                        } else {
                            utf8.push_back(static_cast<char>(0xF0 | (code >> 18)));
                            utf8.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
                            utf8.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                            utf8.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                        }
                        out += utf8;
                        i = end + 1;
                        continue;
                    }
                }
            }
        }
        out.push_back(html[i]);
        ++i;
    }
    // Spaces and lines in order: no runs of spaces, no more than one blank line.
    std::string clean;
    int newlines = 0;
    bool space = false;
    for (const char c : out) {
        if (c == '\n') {
            if (newlines < 2) {
                clean.push_back('\n');
            }
            ++newlines;
            space = false;
        } else if (c == ' ' || c == '\t' || c == '\r') {
            space = true;
        } else {
            if (space && !clean.empty() && clean.back() != '\n') {
                clean.push_back(' ');
            }
            space = false;
            newlines = 0;
            clean.push_back(c);
        }
    }
    return std::string{trim_lines(clean)};
}

std::vector<Hit> Web::parse_search(std::string_view json) {
    const Json answer = parse_json(json, "Web");
    std::vector<Hit> out;
    const Json* query = answer.get("query");
    const Json* search = query != nullptr ? query->get("search") : nullptr;
    const JsonArray* hits = search != nullptr ? search->array() : nullptr;
    if (hits == nullptr) {
        return out;
    }
    for (const Json& hit : *hits) {
        Hit h;
        if (const Json* title = hit.get("title"); title != nullptr && title->string() != nullptr) {
            h.title = *title->string();
        }
        if (const Json* snippet = hit.get("snippet"); snippet != nullptr && snippet->string() != nullptr) {
            h.snippet = strip_html(*snippet->string());
        }
        std::string path = h.title;
        std::ranges::replace(path, ' ', '_');
        h.url = std::string{wikipedia} + "/wiki/" + url_encode(path);
        out.push_back(std::move(h));
    }
    return out;
}

std::string Web::parse_extract(std::string_view json) {
    const Json answer = parse_json(json, "Web");
    const Json* page = first_page(answer);
    const Json* extract = page != nullptr ? page->get("extract") : nullptr;
    if (extract == nullptr || extract->string() == nullptr) {
        return {};
    }
    return *extract->string();
}

std::vector<Meaning> Web::parse_wiktionary(std::string_view extract) {
    std::vector<Meaning> out;
    // The English section: from "== English ==" to the next "== X ==".
    const std::size_t start = extract.find("== English ==");
    if (start == std::string_view::npos) {
        return out;
    }
    std::size_t end = extract.find("\n== ", start + 1);
    if (end == std::string_view::npos) {
        end = extract.size();
    }
    const std::string_view english = extract.substr(start, end - start);
    // Headings of level 3 or 4 that are parts of speech; the lines after the
    // headword line are the definitions, until the next heading.
    Meaning* current = nullptr;
    bool headword_seen = false;
    std::size_t at = 0;
    while (at < english.size()) {
        std::size_t line_end = english.find('\n', at);
        if (line_end == std::string_view::npos) {
            line_end = english.size();
        }
        const std::string_view line = trim(english.substr(at, line_end - at));
        at = line_end + 1;
        if (line.starts_with("=")) {
            current = nullptr;
            std::size_t level = 0;
            while (level < line.size() && line[level] == '=') {
                ++level;
            }
            if (level < 3 || level > 4) {
                continue;
            }
            const std::string title = lower(trim(line.substr(level, line.size() - 2 * level)));
            const auto part = parts_of_speech.find(title);
            if (part == parts_of_speech.end()) {
                continue;
            }
            const Bytes category(part->second.begin(), part->second.end());
            auto held = std::ranges::find_if(out, [&](const Meaning& m) { return m.category == category; });
            if (held == out.end()) {
                out.push_back(Meaning{category, {}});
                held = out.end() - 1;
            }
            current = &*held;
            headword_seen = false;
            continue;
        }
        if (current == nullptr || line.empty()) {
            continue;
        }
        if (!headword_seen) {
            headword_seen = true;  // "vast (comparative vaster ...)"
            continue;
        }
        static const std::array<std::string_view, 8> notes = {"Synonym", "Antonym", "Hyponym", "Hypernym",
                                                               "Coordinate", "Usage", "Troponym", "Meronym"};
        if (std::ranges::any_of(notes, [&](std::string_view n) { return line.starts_with(n); })) {
            continue;
        }
        if (current->definitions.size() < 3) {
            current->definitions.emplace_back(line);
        }
    }
    std::erase_if(out, [](const Meaning& m) { return m.definitions.empty(); });
    return out;
}

std::vector<Hit> Web::search(std::string_view words, std::size_t limit) const {
    const std::string url = std::format(
        "{}/w/api.php?action=query&list=search&format=json&srlimit={}&srsearch={}", wikipedia,
        limit == 0 ? 5 : limit, url_encode(words));
    return parse_search(transport_(url));
}

std::string Web::text_of(std::string_view title_or_url) const {
    if (is_url(title_or_url)) {
        return strip_html(transport_(std::string{title_or_url}));
    }
    const std::string url = std::format(
        "{}/w/api.php?action=query&prop=extracts&explaintext=1&redirects=1&format=json&titles={}", wikipedia,
        url_encode(title_or_url));
    return parse_extract(transport_(url));
}

Page Web::fetch(std::string_view title_or_url, bool keep_file) const {
    Page page;
    page.text = text_of(title_or_url);
    if (is_url(title_or_url)) {
        page.source = std::string{title_or_url};
        std::string_view name = title_or_url;
        if (const std::size_t cut = name.rfind('/'); cut != std::string_view::npos && cut + 1 < name.size()) {
            name = name.substr(cut + 1);
        }
        page.title = std::string{name};
    } else {
        page.title = std::string{title_or_url};
        std::string path = page.title;
        std::ranges::replace(path, ' ', '_');
        page.source = std::string{wikipedia} + "/wiki/" + url_encode(path);
    }
    if (trim(page.text).empty()) {
        throw std::runtime_error(std::format("Web: nothing to read at \"{}\"", title_or_url));
    }
    if (!keep_file) {
        return page;
    }
    std::filesystem::create_directories(directory_);
    page.file = directory_ / (slug(page.title) + ".txt");
    std::ofstream out{page.file, std::ios::binary | std::ios::trunc};
    if (!out) {
        throw std::runtime_error(std::format("Web: cannot write {}", page.file.string()));
    }
    out << "# source: " << page.source << '\n'
        << "# fetched: " << today() << '\n'
        << "# title: " << page.title << "\n\n"
        << page.text << '\n';
    return page;
}

std::optional<Page> Web::read_page(const std::filesystem::path& file) {
    std::ifstream in{file, std::ios::binary};
    if (!in) {
        return std::nullopt;
    }
    Page page;
    page.file = file;
    std::string text;
    bool header = true;
    for (std::string line; std::getline(in, line);) {
        if (header && line.starts_with("# ")) {
            const std::string_view rest = std::string_view{line}.substr(2);
            if (rest.starts_with("source: ")) {
                page.source = std::string{rest.substr(8)};
            } else if (rest.starts_with("title: ")) {
                page.title = std::string{rest.substr(7)};
            }
            continue;
        }
        if (header && line.empty() && text.empty()) {
            header = false;
            continue;
        }
        header = false;
        text += line;
        text += '\n';
    }
    page.text = std::string{trim_lines(text)};
    if (page.title.empty()) {
        page.title = file.stem().string();
    }
    return page;
}

std::vector<Meaning> Web::define(std::string_view word) const {
    const std::string url = std::format(
        "{}/w/api.php?action=query&prop=extracts&explaintext=1&redirects=1&format=json&titles={}", wiktionary,
        url_encode(word));
    return parse_wiktionary(parse_extract(transport_(url)));
}

}  // namespace larry
