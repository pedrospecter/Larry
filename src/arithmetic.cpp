#include "larry/arithmetic.hpp"

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

// The words of the text in lower case, with the marks around them stripped
// and the symbols kept as words of their own: "1+1?" is "1", "+", "1".
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
            flush();  // a full stop, not a decimal point
            continue;
        }
        if (c == '+' || c == '-' || c == '*' || c == '/' || c == '^' || c == '(' || c == ')' || c == '=' ||
            c == '%' || c == '<' || c == '>') {
            flush();
            out.emplace_back(1, static_cast<char>(c));
            continue;
        }
        // "×" and "÷" as UTF-8.
        if (c == 0xC3 && i + 1 < text.size() && (static_cast<unsigned char>(text[i + 1]) == 0x97 ||
                                                   static_cast<unsigned char>(text[i + 1]) == 0xB7)) {
            flush();
            out.emplace_back(static_cast<unsigned char>(text[i + 1]) == 0x97 ? "*" : "/");
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
    for (std::size_t i = 0; i < word.size(); ++i) {
        const char c = word[i];
        if (c >= '0' && c <= '9') {
            digit = true;
        } else if (c == '.' && !point) {
            point = true;
        } else if (c == ',' && i > 0) {
            continue;  // "1,000"
        } else {
            return false;
        }
    }
    return digit;
}

long double number_of(std::string word) {
    std::erase(word, ',');
    return std::strtold(word.c_str(), nullptr);
}

}  // namespace

std::string Calculation::rule() const {
    if (!defined) {
        return "arithmetic: " + expression + " is " + result;
    }
    if (comparison) {
        return "arithmetic: " + expression + (holds ? " holds" : " does not hold");
    }
    return "arithmetic: " + expression + " = " + result;
}

Arithmetic::Arithmetic(const BaseRules& rules) : rules_(&rules) {
    for (const auto& [word, symbol] : rules.arithmetic()) {
        if (word == Bytes{'f', 'r', 'a', 'm', 'e'}) {
            for (const std::string& f : split(text_of(symbol), ';')) {
                frame_.push_back(f);
            }
        } else {
            words_.emplace_back(text_of(word), text_of(symbol));
        }
    }
    std::ranges::stable_sort(words_, [](const auto& a, const auto& b) {
        return std::ranges::count(a.first, ' ') > std::ranges::count(b.first, ' ');
    });
}

std::string Arithmetic::number(long double value) {
    if (std::isnan(value) || std::isinf(value)) {
        return "undefined";
    }
    if (std::fabs(value - std::round(value)) < 1e-9L && std::fabs(value) < 1e15L) {
        return std::format("{}", static_cast<long long>(std::llround(value)));
    }
    std::string out = std::format("{:.6f}", static_cast<double>(value));
    while (!out.empty() && out.back() == '0') {
        out.pop_back();
    }
    if (!out.empty() && out.back() == '.') {
        out.pop_back();
    }
    return out;
}

std::optional<std::vector<Arithmetic::Token>> Arithmetic::tokens(std::string_view text) const {
    const std::vector<std::string> words = words_of(text);
    std::vector<Token> out;
    bool framed = false;
    const auto number_word = [&](const std::string& word) -> std::optional<long double> {
        for (const auto& [name, digits] : rules_->number_words()) {
            if (text_of(name) == word) {
                return number_of(text_of(digits));
            }
        }
        return std::nullopt;
    };
    for (std::size_t i = 0; i < words.size();) {
        const std::string& word = words[i];
        // A number, or number words that compose: "one thousand two hundred thirty".
        if (is_number(word)) {
            out.push_back({Token::Kind::Number, number_of(word), {}});
            ++i;
            continue;
        }
        if (number_word(word)) {
            long double total = 0;
            long double current = 0;
            while (i < words.size()) {
                const std::optional<long double> v = number_word(words[i]);
                if (!v) {
                    break;
                }
                if (*v < 100) {
                    current += *v;
                } else if (*v == 100) {
                    current = (current == 0 ? 1 : current) * 100;
                } else {
                    total += (current == 0 ? 1 : current) * *v;
                    current = 0;
                }
                ++i;
            }
            out.push_back({Token::Kind::Number, total + current, {}});
            continue;
        }
        // An operator phrase, longest first.
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
            // "is" is a comparison only between two numbers; "and" a plus only
            // between two numbers, so that "what is 2 and 3" reads as 2 + 3.
            if (symbol == ">" || symbol == "<" || symbol == "=") {
                out.push_back({Token::Kind::Compare, 0, symbol});
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
        } else if (word == "+" || word == "-" || word == "*" || word == "/" || word == "^" || word == "%") {
            out.push_back({Token::Kind::Operator, 0, word});
        } else if (word == "=" || word == "<" || word == ">") {
            out.push_back({Token::Kind::Compare, 0, word});
        } else if (std::ranges::contains(frame_, word)) {
            framed = true;
        } else {
            return std::nullopt;  // a word that is not arithmetic
        }
        ++i;
    }
    // A comparison word that reads "is" with nothing to compare is frame: drop
    // leading and trailing comparisons without two sides.
    while (!out.empty() && out.front().kind == Token::Kind::Compare) {
        out.erase(out.begin());
        framed = true;
    }
    while (!out.empty() && out.back().kind == Token::Kind::Compare) {
        out.pop_back();
        framed = true;
    }
    const bool has_operator = std::ranges::any_of(out, [](const Token& t) {
        return t.kind == Token::Kind::Operator || t.kind == Token::Kind::Compare;
    });
    if (out.empty() || (!has_operator && !(framed && out.size() == 1))) {
        return std::nullopt;
    }
    return out;
}

namespace {

// A recursive-descent evaluator over the tokens: comparison, then sum,
// product, power, and the unary functions.
struct Evaluator {
    using Token = Arithmetic::Token;
    const std::vector<Token>& tokens;
    std::size_t at = 0;
    bool bad = false;
    bool undefined = false;
    std::string shown;  ///< The expression in symbols, as read.

    explicit Evaluator(const std::vector<Token>& list) : tokens(list) {}

    const Token* peek() const { return at < tokens.size() ? &tokens[at] : nullptr; }

    void show(std::string_view piece) {
        if (!shown.empty()) {
            shown += ' ';
        }
        shown += piece;
    }

    long double sum() {
        long double left = product();
        while (const Token* t = peek()) {
            if (t->kind != Token::Kind::Operator || (t->symbol != "+" && t->symbol != "-")) {
                break;
            }
            const std::string op = t->symbol;
            ++at;
            show(op);
            const long double right = product();
            left = op == "+" ? left + right : left - right;
        }
        return left;
    }

    long double product() {
        long double left = power();
        while (const Token* t = peek()) {
            if (t->kind != Token::Kind::Operator || (t->symbol != "*" && t->symbol != "/" && t->symbol != "%of")) {
                break;
            }
            const std::string op = t->symbol;
            ++at;
            show(op == "%of" ? "% of" : op);
            const long double right = power();
            if (op == "*") {
                left *= right;
            } else if (op == "/") {
                if (right == 0) {
                    undefined = true;
                } else {
                    left /= right;
                }
            } else {
                left = left / 100 * right;
            }
        }
        return left;
    }

    long double power() {
        long double base = unary();
        while (const Token* t = peek()) {
            if (t->kind != Token::Kind::Operator || (t->symbol != "^" && t->symbol != "^2" && t->symbol != "^3")) {
                break;
            }
            const std::string op = t->symbol;
            ++at;
            if (op == "^") {
                show("^");
                const long double exponent = unary();
                base = std::pow(base, exponent);
            } else {
                show(op == "^2" ? "^ 2" : "^ 3");
                base = std::pow(base, op == "^2" ? 2 : 3);
            }
        }
        return base;
    }

    long double unary() {
        const Token* t = peek();
        if (t == nullptr) {
            bad = true;
            return 0;
        }
        if (t->kind == Token::Kind::Operator && t->symbol == "-") {
            ++at;
            show("-");
            return -unary();
        }
        if (t->kind == Token::Kind::Operator && (t->symbol == "sqrt" || t->symbol == "half" || t->symbol == "double")) {
            const std::string op = t->symbol;
            ++at;
            show(op == "sqrt" ? "sqrt" : op == "half" ? "half of" : "double");
            const long double value = unary();
            if (op == "sqrt") {
                if (value < 0) {
                    undefined = true;
                    return 0;
                }
                return std::sqrt(value);
            }
            return op == "half" ? value / 2 : value * 2;
        }
        if (t->kind == Token::Kind::Open) {
            ++at;
            show("(");
            const long double value = sum();
            if (const Token* close = peek(); close != nullptr && close->kind == Token::Kind::Close) {
                ++at;
                show(")");
            } else {
                bad = true;
            }
            return value;
        }
        if (t->kind == Token::Kind::Number) {
            ++at;
            show(Arithmetic::number(t->value));
            return t->value;
        }
        bad = true;
        return 0;
    }
};

}  // namespace

std::optional<Calculation> Arithmetic::calculate(std::string_view text) const {
    const std::optional<std::vector<Token>> list = tokens(text);
    if (!list) {
        return std::nullopt;
    }
    Evaluator left{*list};
    const long double a = left.sum();
    Calculation out;
    if (left.bad) {
        return std::nullopt;
    }
    // "Is 2 plus 2 five?": the "is" went as frame, and a number after the
    // expression asks whether they are equal.
    const Token* next = left.peek();
    if (next != nullptr && (next->kind == Token::Kind::Compare || next->kind == Token::Kind::Number)) {
        const std::string op = next->kind == Token::Kind::Compare ? next->symbol : "=";
        if (next->kind == Token::Kind::Compare) {
            ++left.at;
        }
        left.show(op);
        const long double b = left.sum();
        if (left.bad || left.peek() != nullptr) {
            return std::nullopt;
        }
        out.comparison = true;
        out.expression = left.shown;
        if (left.undefined) {
            out.defined = false;
            out.result = "undefined: division by zero";
            return out;
        }
        out.holds = op == "=" ? std::fabs(a - b) < 1e-9L : op == ">" ? a > b : a < b;
        out.result = out.holds ? "yes" : "no";
        return out;
    }
    if (left.peek() != nullptr) {
        return std::nullopt;
    }
    out.expression = left.shown;
    if (left.undefined) {
        out.defined = false;
        out.result = "undefined: division by zero";
        return out;
    }
    out.result = number(a);
    return out;
}

}  // namespace larry
