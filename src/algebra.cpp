#include "larry/algebra.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace larry {

namespace {

std::string text_of(const Bytes& bytes) {
    return std::string(bytes.begin(), bytes.end());
}

std::vector<std::string> split(std::string_view text, char separator) {
    std::vector<std::string> out;
    std::string current;
    for (const char c : text) {
        if (c == separator) {
            if (!current.empty()) {
                out.push_back(std::move(current));
                current.clear();
            }
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty()) {
        out.push_back(std::move(current));
    }
    return out;
}

// The words in lower case, symbols on their own, and "2x" as "2", "x".
std::vector<std::string> words_of(std::string_view text) {
    std::vector<std::string> out;
    std::string current;
    const auto flush = [&] {
        if (!current.empty()) {
            out.push_back(std::move(current));
            current.clear();
        }
    };
    for (std::size_t i = 0; i < text.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '?' || c == '!' || c == ',' || c == ';' ||
            c == ':' || c == '"') {
            flush();
            continue;
        }
        if (c == '.' && !(i + 1 < text.size() && text[i + 1] >= '0' && text[i + 1] <= '9' && !current.empty() &&
                          current.back() >= '0' && current.back() <= '9')) {
            flush();
            continue;
        }
        if (c == '+' || c == '-' || c == '*' || c == '/' || c == '^' || c == '(' || c == ')' || c == '=') {
            flush();
            out.emplace_back(1, static_cast<char>(c));
            continue;
        }
        if ((c == 'x' || c == 'X') && !current.empty() && std::ranges::all_of(current, [](char ch) {
                return (ch >= '0' && ch <= '9') || ch == '.';
            })) {
            flush();  // "2x" is "2" then "x"
            out.emplace_back("x");
            continue;
        }
        if (c == 0xC2 && i + 1 < text.size() && static_cast<unsigned char>(text[i + 1]) == 0xB2) {
            flush();  // "x²"
            out.emplace_back("^");
            out.emplace_back("2");
            ++i;
            continue;
        }
        current.push_back(static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c));
    }
    flush();
    return out;
}

bool is_number(const std::string& word) {
    bool digit = false;
    bool point = false;
    for (const char c : word) {
        if (c >= '0' && c <= '9') {
            digit = true;
        } else if (c == '.' && !point) {
            point = true;
        } else {
            return false;
        }
    }
    return digit;
}

std::string number(long double v) {
    return Arithmetic::number(v);
}

}  // namespace

int Poly::degree() const noexcept {
    for (int d = 2; d >= 0; --d) {
        if (std::fabs(c[static_cast<std::size_t>(d)]) > 1e-12L) {
            return d;
        }
    }
    return -1;
}

std::string Poly::written() const {
    std::string out;
    for (int d = 2; d >= 0; --d) {
        const long double v = c[static_cast<std::size_t>(d)];
        if (std::fabs(v) < 1e-12L) {
            continue;
        }
        const bool negative = v < 0;
        const long double size = std::fabs(v);
        if (out.empty()) {
            out += negative ? "-" : "";
        } else {
            out += negative ? " - " : " + ";
        }
        const bool one = std::fabs(size - 1) < 1e-12L;
        if (d == 0 || !one) {
            out += number(size);
        }
        if (d >= 1) {
            out += "x";
        }
        if (d == 2) {
            out += "^2";
        }
    }
    return out.empty() ? "0" : out;
}

Algebra::Algebra(const BaseRules& rules) : rules_(&rules) {
    for (const auto& [word, symbol] : rules.arithmetic()) {
        const std::string key = text_of(word);
        if (key == "frame" || key.starts_with("fraction ")) {
            continue;
        }
        const std::string value = text_of(symbol);
        if (value == ">" || value == "<" || value == "%of" || key == "x") {
            continue;  // comparisons and "x" as times are no part of algebra
        }
        words_.emplace_back(key, value);
    }
    std::ranges::stable_sort(words_, [](const auto& a, const auto& b) {
        return std::ranges::count(a.first, ' ') > std::ranges::count(b.first, ' ');
    });
    for (const auto& [key, value] : rules.algebra()) {
        if (text_of(key) == "frame") {
            for (const std::string& f : split(text_of(value), ';')) {
                frame_.push_back(f);
            }
        }
    }
}

std::optional<std::vector<Algebra::Token>> Algebra::tokens(std::string_view text, std::optional<long double>& given,
                                                           bool& solve, bool& simplify) const {
    std::vector<std::string> words = words_of(text);
    given.reset();
    solve = false;
    simplify = false;
    const auto is_condition = [](const std::string& w) {
        return w == "if" || w == "when" || w == "where" || w == "given";
    };
    const auto is_link = [](const std::string& w) { return w == "is" || w == "=" || w == "equals"; };
    // The condition: "if x is 3", "when x = 4", "for x = 4" at the end.
    for (std::size_t i = 0; i < words.size(); ++i) {
        if ((is_condition(words[i]) || words[i] == "for") && i + 2 < words.size() && words[i + 1] == "x" &&
            is_link(words[i + 2])) {
            if (i + 3 < words.size() && is_number(words[i + 3]) && i + 4 == words.size()) {
                given = std::strtold(words[i + 3].c_str(), nullptr);
                words.resize(i);
                break;
            }
            return std::nullopt;
        }
        if (words[i] == "for" && i + 1 < words.size() && words[i + 1] == "x") {
            words.erase(words.begin() + static_cast<std::ptrdiff_t>(i), words.begin() + static_cast<std::ptrdiff_t>(i) + 2);
            solve = true;  // "solve ... for x", "solve for x: ..."
            --i;
        }
    }
    // "What is x if 2x + 3 = 11": the x asked for before the condition means solve.
    for (std::size_t i = 0; i < words.size(); ++i) {
        if (!is_condition(words[i])) {
            continue;
        }
        const std::size_t xs = static_cast<std::size_t>(std::count(words.begin(), words.begin() + static_cast<std::ptrdiff_t>(i), "x"));
        const bool plain = xs == 1 && std::all_of(words.begin(), words.begin() + static_cast<std::ptrdiff_t>(i), [&](const std::string& w) {
            return w == "x" || w == "solve" || w == "find" || std::ranges::contains(frame_, w);
        });
        if (plain) {
            words.erase(words.begin(), words.begin() + static_cast<std::ptrdiff_t>(i) + 1);
            solve = true;
        }
        break;
    }
    const auto number_word = [&](const std::string& word) -> std::optional<long double> {
        if (is_number(word)) {
            return std::strtold(word.c_str(), nullptr);
        }
        for (const auto& [name, digits] : rules_->number_words()) {
            if (text_of(name) == word) {
                return std::strtold(text_of(digits).c_str(), nullptr);
            }
        }
        return std::nullopt;
    };
    std::vector<Token> out;
    for (std::size_t i = 0; i < words.size();) {
        const std::string& word = words[i];
        if (const std::optional<long double> n = number_word(word)) {
            out.push_back({Token::Kind::Number, *n, {}});
            ++i;
            continue;
        }
        if (word == "x") {
            out.push_back({Token::Kind::Unknown, 0, "x"});
            ++i;
            continue;
        }
        bool matched = false;
        for (const auto& [phrase, symbol] : words_) {
            const std::vector<std::string> parts = split(phrase, ' ');
            if (i + parts.size() > words.size()) {
                continue;
            }
            bool same = true;
            for (std::size_t k = 0; k < parts.size() && same; ++k) {
                same = words[i + k] == parts[k];
            }
            if (!same) {
                continue;
            }
            if (symbol == "=") {
                out.push_back({Token::Kind::Equals, 0, "="});
            } else {
                out.push_back({Token::Kind::Operator, 0, symbol});
            }
            i += parts.size();
            matched = true;
            break;
        }
        if (matched) {
            continue;
        }
        if (word == "(") {
            out.push_back({Token::Kind::Open, 0, {}});
        } else if (word == ")") {
            out.push_back({Token::Kind::Close, 0, {}});
        } else if (word == "+" || word == "-" || word == "*" || word == "/" || word == "^") {
            out.push_back({Token::Kind::Operator, 0, word});
        } else if (word == "=") {
            out.push_back({Token::Kind::Equals, 0, "="});
        } else if (word == "solve" || word == "find") {
            solve = true;
        } else if (word == "simplify" || word == "expand") {
            simplify = true;
        } else if (std::ranges::contains(frame_, word)) {
            // frame
        } else {
            return std::nullopt;
        }
        ++i;
    }
    // "is" read as "=" at the front ("what is 2x + 3") is frame.
    while (!out.empty() && out.front().kind == Token::Kind::Equals) {
        out.erase(out.begin());
    }
    while (!out.empty() && out.back().kind == Token::Kind::Equals) {
        out.pop_back();
    }
    // No unknown: not algebra. An x between two numbers alone is the times sign.
    const auto unknowns = std::ranges::count_if(out, [](const Token& t) { return t.kind == Token::Kind::Unknown; });
    if (unknowns == 0) {
        return std::nullopt;
    }
    bool only_times = !solve && !simplify && !given;
    for (std::size_t i = 0; i < out.size() && only_times; ++i) {
        if (out[i].kind == Token::Kind::Unknown) {
            only_times = i > 0 && i + 1 < out.size() && out[i - 1].kind == Token::Kind::Number &&
                         out[i + 1].kind == Token::Kind::Number;
        }
    }
    if (only_times && std::ranges::none_of(out, [](const Token& t) { return t.kind == Token::Kind::Equals; })) {
        return std::nullopt;
    }
    return out;
}

namespace {

// A recursive-descent evaluator over polynomials: sum, product, power,
// unary; a number before x or a bracket multiplies.
struct Evaluator {
    using Token = Algebra::Token;
    const std::vector<Token>& tokens;
    std::size_t at = 0;
    bool bad = false;
    bool beyond = false;  ///< A degree above two, or a division by the unknown.
    bool undefined = false;

    explicit Evaluator(const std::vector<Token>& list) : tokens(list) {}

    const Token* peek() const { return at < tokens.size() ? &tokens[at] : nullptr; }

    static Poly add(const Poly& a, const Poly& b, long double sign) {
        Poly out;
        for (std::size_t i = 0; i < 3; ++i) {
            out.c[i] = a.c[i] + sign * b.c[i];
        }
        return out;
    }

    Poly mul(const Poly& a, const Poly& b) {
        Poly out;
        for (std::size_t i = 0; i < 3; ++i) {
            for (std::size_t j = 0; j < 3; ++j) {
                if (std::fabs(a.c[i]) < 1e-12L || std::fabs(b.c[j]) < 1e-12L) {
                    continue;
                }
                if (i + j > 2) {
                    beyond = true;
                    continue;
                }
                out.c[i + j] += a.c[i] * b.c[j];
            }
        }
        return out;
    }

    Poly sum() {
        Poly left = product();
        while (const Token* t = peek()) {
            if (t->kind != Token::Kind::Operator || (t->symbol != "+" && t->symbol != "-")) {
                break;
            }
            const std::string op = t->symbol;
            ++at;
            const Poly right = product();
            left = add(left, right, op == "+" ? 1 : -1);
        }
        return left;
    }

    Poly product() {
        Poly left = power();
        for (;;) {
            const Token* t = peek();
            if (t == nullptr) {
                break;
            }
            if (t->kind == Token::Kind::Operator && (t->symbol == "*" || t->symbol == "/")) {
                const std::string op = t->symbol;
                ++at;
                const Poly right = power();
                if (op == "*") {
                    left = mul(left, right);
                } else if (!right.constant()) {
                    beyond = true;
                } else if (std::fabs(right.c[0]) < 1e-12L) {
                    undefined = true;
                } else {
                    for (long double& c : left.c) {
                        c /= right.c[0];
                    }
                }
                continue;
            }
            // Juxtaposition: "2x", "2(x + 1)", "(x + 1)(x + 1)".
            if (t->kind == Token::Kind::Unknown || t->kind == Token::Kind::Open) {
                const Poly right = power();
                left = mul(left, right);
                continue;
            }
            break;
        }
        return left;
    }

    Poly power() {
        Poly base = unary();
        while (const Token* t = peek()) {
            if (t->kind != Token::Kind::Operator || (t->symbol != "^" && t->symbol != "^2" && t->symbol != "^3")) {
                break;
            }
            const std::string op = t->symbol;
            ++at;
            long double exponent = 2;
            if (op == "^") {
                const Poly e = unary();
                if (!e.constant()) {
                    beyond = true;
                    return base;
                }
                exponent = e.c[0];
            } else if (op == "^3") {
                exponent = 3;
            }
            if (std::fabs(exponent - std::round(exponent)) > 1e-9L || exponent < 0) {
                beyond = true;
                return base;
            }
            const int n = static_cast<int>(std::llround(exponent));
            Poly out;
            out.c[0] = 1;
            for (int k = 0; k < n; ++k) {
                out = mul(out, base);
            }
            base = out;
        }
        return base;
    }

    Poly unary() {
        const Token* t = peek();
        if (t == nullptr) {
            bad = true;
            return {};
        }
        if (t->kind == Token::Kind::Operator && t->symbol == "-") {
            ++at;
            Poly v = unary();
            for (long double& c : v.c) {
                c = -c;
            }
            return v;
        }
        if (t->kind == Token::Kind::Operator && (t->symbol == "half" || t->symbol == "double")) {
            const std::string op = t->symbol;
            ++at;
            Poly v = unary();
            for (long double& c : v.c) {
                c = op == "half" ? c / 2 : c * 2;
            }
            return v;
        }
        if (t->kind == Token::Kind::Operator && t->symbol == "sqrt") {
            ++at;
            const Poly v = unary();
            if (!v.constant() || v.c[0] < 0) {
                beyond = true;
                return v;
            }
            Poly out;
            out.c[0] = std::sqrt(v.c[0]);
            return out;
        }
        if (t->kind == Token::Kind::Open) {
            ++at;
            const Poly v = sum();
            if (const Token* close = peek(); close != nullptr && close->kind == Token::Kind::Close) {
                ++at;
            } else {
                bad = true;
            }
            return v;
        }
        if (t->kind == Token::Kind::Number) {
            ++at;
            Poly v;
            v.c[0] = t->value;
            return v;
        }
        if (t->kind == Token::Kind::Unknown) {
            ++at;
            Poly v;
            v.c[1] = 1;
            return v;
        }
        bad = true;
        return {};
    }
};

std::string solved(const Poly& p) {
    const int d = p.degree();
    if (d <= 0) {
        return d < 0 ? "any x" : "no solution";
    }
    if (d == 1) {
        return "x = " + Arithmetic::number(-p.c[0] / p.c[1]);
    }
    const long double a = p.c[2];
    const long double b = p.c[1];
    const long double c = p.c[0];
    const long double discriminant = b * b - 4 * a * c;
    if (discriminant < -1e-12L) {
        return "no solution";
    }
    if (std::fabs(discriminant) <= 1e-12L) {
        return "x = " + Arithmetic::number(-b / (2 * a));
    }
    const long double root = std::sqrt(discriminant);
    long double first = (-b + root) / (2 * a);
    long double second = (-b - root) / (2 * a);
    if (first > second) {
        std::swap(first, second);  // the smaller root first
    }
    return "x = " + Arithmetic::number(first) + " or x = " + Arithmetic::number(second);
}

}  // namespace

std::optional<Calculation> Algebra::calculate(std::string_view text) const {
    std::optional<long double> given;
    bool solve = false;
    bool simplify = false;
    const std::optional<std::vector<Token>> list = tokens(text, given, solve, simplify);
    if (!list || list->empty()) {
        return std::nullopt;
    }
    // Left and right of "=".
    std::vector<Token> left_tokens;
    std::vector<Token> right_tokens;
    bool equation = false;
    for (const Token& t : *list) {
        if (t.kind == Token::Kind::Equals) {
            if (equation) {
                return std::nullopt;
            }
            equation = true;
            continue;
        }
        (equation ? right_tokens : left_tokens).push_back(t);
    }
    if (left_tokens.empty() || (equation && right_tokens.empty())) {
        return std::nullopt;
    }
    Evaluator left{left_tokens};
    const Poly a = left.sum();
    if (left.bad || left.peek() != nullptr || left.beyond) {
        return std::nullopt;
    }
    Calculation out;
    if (left.undefined) {
        out.defined = false;
        out.expression = "algebra: " + std::string{text};
        out.result = "undefined: division by zero";
        return out;
    }
    if (equation) {
        Evaluator right{right_tokens};
        const Poly b = right.sum();
        if (right.bad || right.peek() != nullptr || right.beyond) {
            return std::nullopt;
        }
        if (right.undefined) {
            out.defined = false;
            out.expression = "algebra: " + a.written() + " = " + b.written();
            out.result = "undefined: division by zero";
            return out;
        }
        const Poly difference = Evaluator::add(a, b, -1);
        out.expression = "algebra: " + a.written() + " = " + b.written();
        out.link = "gives";
        out.result = solved(difference);
        return out;
    }
    if (given) {
        out.expression = std::format("algebra: {} with x = {}", a.written(), number(*given));
        out.result = number(a.at(*given));
        return out;
    }
    out.expression = "algebra: " + std::string{text.substr(0, text.find_last_not_of(" .?!") + 1)};
    out.link = "is";
    out.result = a.written();
    return out;
}

}  // namespace larry
