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
        // "×" and "÷" as UTF-8; "°" stays with its letter ("°c").
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

/// A value under evaluation: a number, with the quantity it measures when it
/// came with a unit, in the base unit of that quantity.
struct Value {
    long double number = 0;
    std::string quantity;      ///< Empty for a plain number.
    std::optional<Unit> unit;  ///< The first unit it was given in.
};

}  // namespace

std::string Calculation::rule() const {
    const bool named = expression.starts_with("calendar: ") || expression.starts_with("algebra: ");
    const std::string prefix = named ? "" : "arithmetic: ";
    if (!defined) {
        return prefix + expression + " is " + result;
    }
    if (comparison) {
        return prefix + expression + (holds ? " holds" : " does not hold");
    }
    return prefix + expression + " " + link + " " + result;
}

Arithmetic::Arithmetic(const BaseRules& rules) : rules_(&rules) {
    for (const auto& [word, symbol] : rules.arithmetic()) {
        const std::string key = text_of(word);
        if (key == "frame") {
            for (const std::string& f : split(text_of(symbol), ';')) {
                frame_.push_back(f);
            }
        } else if (key.starts_with("fraction ")) {
            fractions_.emplace_back(key.substr(9), number_of(text_of(symbol)));
        } else {
            words_.emplace_back(key, text_of(symbol));
        }
    }
    std::ranges::stable_sort(words_, [](const auto& a, const auto& b) {
        return std::ranges::count(a.first, ' ') > std::ranges::count(b.first, ' ');
    });
    for (const auto& [name, definition] : rules.units()) {
        const std::vector<std::string> parts = split(text_of(definition), ':');
        if (parts.size() < 2) {
            continue;
        }
        Unit u;
        u.name = text_of(name);
        u.quantity = parts[0];
        u.factor = number_of(parts[1]);
        u.offset = parts.size() > 2 ? std::strtold(parts[2].c_str(), nullptr) : 0;
        units_.push_back(std::move(u));
    }
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

std::optional<Unit> Arithmetic::unit(std::string_view word) const {
    for (const Unit& u : units_) {
        if (u.name == word) {
            return u;
        }
    }
    return std::nullopt;
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
    const auto fraction = [&](const std::string& word) -> std::optional<long double> {
        for (const auto& [name, denominator] : fractions_) {
            if (name == word) {
                return 1 / denominator;
            }
        }
        return std::nullopt;
    };
    const auto last_is_number = [&] { return !out.empty() && out.back().kind == Token::Kind::Number; };
    const auto starts_number = [&](std::size_t i) {
        return i < words.size() && (is_number(words[i]) || number_word(words[i]) || fraction(words[i]) ||
                                    words[i] == "(" || words[i] == "a" || words[i] == "an");
    };
    for (std::size_t i = 0; i < words.size();) {
        const std::string& word = words[i];
        // A number, or number words that compose: "one thousand two hundred thirty".
        if (is_number(word)) {
            out.push_back({Token::Kind::Number, number_of(word), {}, std::nullopt});
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
            out.push_back({Token::Kind::Number, total + current, {}, std::nullopt});
            continue;
        }
        // A fraction word: "a third" is 1/3, "two thirds" 2/3; "of" after it multiplies.
        if (const std::optional<long double> f = fraction(word)) {
            if (last_is_number()) {
                out.back().value *= *f;
            } else {
                out.push_back({Token::Kind::Number, *f, {}, std::nullopt});
            }
            ++i;
            continue;
        }
        if ((word == "a" || word == "an") && i + 1 < words.size() && fraction(words[i + 1])) {
            ++i;  // "a third": the article counts as one, the fraction follows
            out.push_back({Token::Kind::Number, 1, {}, std::nullopt});
            continue;
        }
        // A unit after a number makes a quantity; a unit on its own is the unit asked for.
        if (const std::optional<Unit> u = unit(word)) {
            if (last_is_number() && !out.back().unit) {
                out.back().unit = *u;
            } else {
                out.push_back({Token::Kind::Target, 0, {}, *u});
            }
            ++i;
            continue;
        }
        // "of" between two numbers multiplies: "a third of 9", "20 percent of 50" aside.
        if (word == "of" && last_is_number() && starts_number(i + 1)) {
            out.push_back({Token::Kind::Operator, 0, "*", std::nullopt});
            ++i;
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
            if (symbol == ">" || symbol == "<" || symbol == "=") {
                out.push_back({Token::Kind::Compare, 0, symbol, std::nullopt});
            } else {
                out.push_back({Token::Kind::Operator, 0, symbol, std::nullopt});
            }
            i += parts.size();
            matched = true;
            break;
        }
        if (matched) {
            continue;
        }
        if (word == "(") {
            out.push_back({Token::Kind::Open, 0, {}, std::nullopt});
        } else if (word == ")") {
            out.push_back({Token::Kind::Close, 0, {}, std::nullopt});
        } else if (word == "+" || word == "-" || word == "*" || word == "/" || word == "^" || word == "%") {
            out.push_back({Token::Kind::Operator, 0, word, std::nullopt});
        } else if (word == "=" || word == "<" || word == ">") {
            out.push_back({Token::Kind::Compare, 0, word, std::nullopt});
        } else if (std::ranges::contains(frame_, word)) {
            framed = true;
        } else {
            return std::nullopt;  // a word that is not arithmetic
        }
        ++i;
    }
    // A comparison word with nothing to compare is frame.
    while (!out.empty() && out.front().kind == Token::Kind::Compare) {
        out.erase(out.begin());
        framed = true;
    }
    while (!out.empty() && out.back().kind == Token::Kind::Compare) {
        out.pop_back();
        framed = true;
    }
    const bool has_operator = std::ranges::any_of(out, [](const Token& t) {
        return t.kind == Token::Kind::Operator || t.kind == Token::Kind::Compare || t.kind == Token::Kind::Target;
    });
    const bool has_quantity = std::ranges::any_of(out, [](const Token& t) {
        return t.kind == Token::Kind::Number && t.unit.has_value();
    });
    if (out.empty() || (!has_operator && !has_quantity && !(framed && out.size() == 1))) {
        return std::nullopt;
    }
    return out;
}

namespace {

// A recursive-descent evaluator over the tokens: comparison, then sum,
// product, power, and the unary functions, over values with quantities.
struct Evaluator {
    using Token = Arithmetic::Token;
    const std::vector<Token>& tokens;
    std::size_t at = 0;
    bool bad = false;
    bool undefined = false;
    std::string why;    ///< Why it is undefined.
    std::string shown;  ///< The expression in symbols, as read.

    explicit Evaluator(const std::vector<Token>& list) : tokens(list) {}

    const Token* peek() const { return at < tokens.size() ? &tokens[at] : nullptr; }

    void show(std::string_view piece) {
        if (!shown.empty()) {
            shown += ' ';
        }
        shown += piece;
    }

    void fail(std::string reason) {
        if (!undefined) {
            undefined = true;
            why = std::move(reason);
        }
    }

    Value combine(Value left, const Value& right, char op) {
        if (op == '+' || op == '-') {
            if (left.quantity != right.quantity) {
                fail(std::format("undefined: {} and {} do not add", left.quantity.empty() ? "a number" : left.quantity,
                                 right.quantity.empty() ? "a number" : right.quantity));
                return left;
            }
            left.number = op == '+' ? left.number + right.number : left.number - right.number;
            return left;
        }
        if (op == '*') {
            if (!left.quantity.empty() && !right.quantity.empty()) {
                fail("undefined: a product of two quantities");
                return left;
            }
            if (left.quantity.empty()) {
                left.quantity = right.quantity;
                left.unit = right.unit;
            }
            left.number *= right.number;
            return left;
        }
        // Division.
        if (right.number == 0) {
            fail("undefined: division by zero");
            return left;
        }
        if (!right.quantity.empty()) {
            if (left.quantity != right.quantity) {
                fail("undefined: a quantity divided by another kind of quantity");
                return left;
            }
            left.quantity.clear();  // a ratio
            left.unit.reset();
        }
        left.number /= right.number;
        return left;
    }

    Value sum() {
        Value left = product();
        while (const Token* t = peek()) {
            if (t->kind != Token::Kind::Operator || (t->symbol != "+" && t->symbol != "-")) {
                break;
            }
            const std::string op = t->symbol;
            ++at;
            show(op);
            const Value right = product();
            left = combine(std::move(left), right, op.front());
        }
        return left;
    }

    Value product() {
        Value left = power();
        while (const Token* t = peek()) {
            if (t->kind != Token::Kind::Operator || (t->symbol != "*" && t->symbol != "/" && t->symbol != "%of")) {
                break;
            }
            const std::string op = t->symbol;
            ++at;
            show(op == "%of" ? "% of" : op);
            Value right = power();
            if (op == "%of") {
                left.number /= 100;
                left = combine(std::move(left), right, '*');
            } else {
                left = combine(std::move(left), right, op.front());
            }
        }
        return left;
    }

    Value power() {
        Value base = unary();
        while (const Token* t = peek()) {
            if (t->kind != Token::Kind::Operator || (t->symbol != "^" && t->symbol != "^2" && t->symbol != "^3")) {
                break;
            }
            const std::string op = t->symbol;
            ++at;
            if (!base.quantity.empty()) {
                fail("undefined: a power of a quantity");
            }
            if (op == "^") {
                show("^");
                const Value exponent = unary();
                base.number = std::pow(base.number, exponent.number);
            } else {
                show(op == "^2" ? "^ 2" : "^ 3");
                base.number = std::pow(base.number, op == "^2" ? 2 : 3);
            }
        }
        return base;
    }

    Value unary() {
        const Token* t = peek();
        if (t == nullptr) {
            bad = true;
            return {};
        }
        if (t->kind == Token::Kind::Operator && t->symbol == "-") {
            ++at;
            show("-");
            Value v = unary();
            v.number = -v.number;
            return v;
        }
        if (t->kind == Token::Kind::Operator && (t->symbol == "sqrt" || t->symbol == "half" || t->symbol == "double")) {
            const std::string op = t->symbol;
            ++at;
            show(op == "sqrt" ? "sqrt" : op == "half" ? "half of" : "double");
            Value v = unary();
            if (op == "sqrt") {
                if (v.number < 0 || !v.quantity.empty()) {
                    fail(v.quantity.empty() ? "undefined: the square root of a negative number"
                                            : "undefined: the square root of a quantity");
                    return v;
                }
                v.number = std::sqrt(v.number);
                return v;
            }
            v.number = op == "half" ? v.number / 2 : v.number * 2;
            return v;
        }
        if (t->kind == Token::Kind::Open) {
            ++at;
            show("(");
            const Value v = sum();
            if (const Token* close = peek(); close != nullptr && close->kind == Token::Kind::Close) {
                ++at;
                show(")");
            } else {
                bad = true;
            }
            return v;
        }
        if (t->kind == Token::Kind::Number) {
            ++at;
            Value v;
            if (t->unit) {
                v.quantity = t->unit->quantity;
                v.unit = t->unit;
                v.number = t->unit->to_base(t->value);
                show(Arithmetic::number(t->value) + " " + t->unit->name);
            } else {
                v.number = t->value;
                show(Arithmetic::number(t->value));
            }
            return v;
        }
        bad = true;
        return {};
    }
};

// A value as Larry says it: in the unit asked for, or in its own.
std::string written(const Value& v, const std::optional<Unit>& target, std::string& error) {
    if (v.quantity.empty()) {
        if (target) {
            error = "undefined: a number has no " + target->name;
            return {};
        }
        return Arithmetic::number(v.number);
    }
    const Unit& out = target ? *target : *v.unit;
    if (out.quantity != v.quantity) {
        error = std::format("undefined: {} in {}", v.quantity, out.name);
        return {};
    }
    return Arithmetic::number(out.from_base(v.number)) + " " + out.name;
}

}  // namespace

std::optional<Calculation> Arithmetic::calculate(std::string_view text) const {
    std::optional<std::vector<Token>> list = tokens(text);
    if (!list) {
        return std::nullopt;
    }
    // The unit asked for: a target anywhere ("how many minutes in 3 hours",
    // "3 hours in minutes"); at most one.
    std::optional<Unit> target;
    for (auto it = list->begin(); it != list->end();) {
        if (it->kind == Token::Kind::Target) {
            if (target) {
                return std::nullopt;
            }
            target = it->unit;
            it = list->erase(it);
        } else {
            ++it;
        }
    }
    if (list->empty()) {
        return std::nullopt;
    }
    Evaluator left{*list};
    const Value a = left.sum();
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
        const Value b = left.sum();
        if (left.bad || left.peek() != nullptr) {
            return std::nullopt;
        }
        out.comparison = true;
        out.expression = left.shown;
        if (left.undefined) {
            out.defined = false;
            out.result = left.why;
            return out;
        }
        if (a.quantity != b.quantity) {
            out.defined = false;
            out.result = "undefined: two kinds of quantity compared";
            return out;
        }
        out.holds = op == "=" ? std::fabs(a.number - b.number) < 1e-9L : op == ">" ? a.number > b.number : a.number < b.number;
        out.result = out.holds ? "yes" : "no";
        return out;
    }
    if (left.peek() != nullptr) {
        return std::nullopt;
    }
    out.expression = left.shown + (target ? " in " + target->name : "");
    if (left.undefined) {
        out.defined = false;
        out.result = left.why;
        return out;
    }
    std::string error;
    out.result = written(a, target, error);
    if (!error.empty()) {
        out.defined = false;
        out.result = error;
    }
    return out;
}

}  // namespace larry
