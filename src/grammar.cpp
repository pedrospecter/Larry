#include "larry/grammar.hpp"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <format>
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace larry {

namespace {

Bytes bytes_of(std::string_view text) {
    return Bytes(text.begin(), text.end());
}

std::string text_of(const Bytes& bytes) {
    return std::string(bytes.begin(), bytes.end());
}

bool contains(const std::vector<Bytes>& list, const Bytes& item) {
    return std::ranges::contains(list, item);
}

const Bytes none = bytes_of("none");
const Bytes end_of_sentence = bytes_of("end");

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) {
        s.remove_suffix(1);
    }
    return s;
}

// The pieces of a pattern: '(', ')', '|', '?', '*', '+', '/role', '@name' and
// a category, with '_' for a space.
struct Token {
    enum class Kind { Open, Close, Bar, Optional, Any, Some, Role, Macro, Category, End };
    Kind kind;
    std::string text;
};

bool is_name_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
}

std::vector<Token> tokens_of(std::string_view text, std::string_view where) {
    std::vector<Token> out;
    std::size_t i = 0;
    const auto name_from = [&](std::size_t start) {
        std::size_t j = start;
        while (j < text.size() && is_name_char(text[j])) {
            ++j;
        }
        if (j == start) {
            throw std::runtime_error(std::format("{}: a name is missing at \"{}\"", where,
                                                 text.substr(start, 8)));
        }
        std::string name{text.substr(start, j - start)};
        std::ranges::replace(name, '_', ' ');
        i = j;
        return name;
    };
    while (i < text.size()) {
        const char c = text[i];
        if (c == ' ' || c == '\t') {
            ++i;
        } else if (c == '(') {
            out.push_back({Token::Kind::Open, "("});
            ++i;
        } else if (c == ')') {
            out.push_back({Token::Kind::Close, ")"});
            ++i;
        } else if (c == '|') {
            out.push_back({Token::Kind::Bar, "|"});
            ++i;
        } else if (c == '?') {
            out.push_back({Token::Kind::Optional, "?"});
            ++i;
        } else if (c == '*') {
            out.push_back({Token::Kind::Any, "*"});
            ++i;
        } else if (c == '+') {
            out.push_back({Token::Kind::Some, "+"});
            ++i;
        } else if (c == '/') {
            out.push_back({Token::Kind::Role, name_from(i + 1)});
        } else if (c == '@') {
            out.push_back({Token::Kind::Macro, name_from(i + 1)});
        } else if (is_name_char(c)) {
            out.push_back({Token::Kind::Category, name_from(i)});
        } else {
            throw std::runtime_error(
                std::format("{}: unexpected character '{}' in a pattern", where, c));
        }
    }
    out.push_back({Token::Kind::End, ""});
    return out;
}

// A recursive-descent parser: a sequence of items, an item an atom with its
// role and mark, '|' between items making one alternation of them.
class Parser {
public:
    using Node = Grammar::Node;

    Parser(std::vector<Token> tokens, const std::vector<Bytes>& categories,
           const std::vector<std::pair<std::string, Node>>& macros, std::string_view where)
        : tokens_(std::move(tokens)), categories_(&categories), macros_(&macros), where_(where) {}

    Node parse() {
        Node out = sequence();
        if (peek().kind != Token::Kind::End) {
            throw std::runtime_error(std::format("{}: unexpected \"{}\"", where_, peek().text));
        }
        return out;
    }

private:
    [[nodiscard]] const Token& peek() const { return tokens_[at_]; }
    Token take() { return tokens_[at_++]; }

    Node sequence() {
        Node out;
        out.kind = Node::Kind::Sequence;
        while (peek().kind != Token::Kind::End && peek().kind != Token::Kind::Close) {
            out.children.push_back(alternatives());
        }
        if (out.children.empty()) {
            throw std::runtime_error(std::format("{}: an empty group", where_));
        }
        return out;
    }

    Node alternatives() {
        Node first = item();
        if (peek().kind != Token::Kind::Bar) {
            return first;
        }
        Node out;
        out.kind = Node::Kind::Alternation;
        out.children.push_back(std::move(first));
        while (peek().kind == Token::Kind::Bar) {
            take();
            out.children.push_back(item());
        }
        return out;
    }

    Node item() {
        Node out = atom();
        for (;;) {
            const Token& t = peek();
            if (t.kind == Token::Kind::Role) {
                const Bytes role = bytes_of(take().text);
                if (!contains(Grammar::roles(), role)) {
                    throw std::runtime_error(
                        std::format("{}: \"{}\" is not a role", where_, text_of(role)));
                }
                out.role = role;
            } else if (t.kind == Token::Kind::Optional) {
                take();
                out.min = 0;
                out.max = 1;
            } else if (t.kind == Token::Kind::Any) {
                take();
                out.min = 0;
                out.max = 0;
            } else if (t.kind == Token::Kind::Some) {
                take();
                out.min = 1;
                out.max = 0;
            } else {
                return out;
            }
        }
    }

    Node atom() {
        const Token t = take();
        if (t.kind == Token::Kind::Open) {
            Node out = sequence();
            if (take().kind != Token::Kind::Close) {
                throw std::runtime_error(std::format("{}: a ')' is missing", where_));
            }
            return out;
        }
        if (t.kind == Token::Kind::Macro) {
            for (const auto& [name, body] : *macros_) {
                if (name == t.text) {
                    Node out;
                    out.kind = Node::Kind::Sequence;
                    out.children.push_back(body);
                    return out;
                }
            }
            throw std::runtime_error(
                std::format("{}: \"@{}\" is not defined above", where_, t.text));
        }
        if (t.kind == Token::Kind::Category) {
            Node out;
            out.kind = Node::Kind::Place;
            const Bytes category = bytes_of(t.text);
            if (!contains(*categories_, category)) {
                throw std::runtime_error(
                    std::format("{}: \"{}\" is not a category", where_, t.text));
            }
            out.categories.push_back(category);
            return out;
        }
        throw std::runtime_error(std::format("{}: a place was expected, not \"{}\"", where_,
                                             t.kind == Token::Kind::End ? "the end" : t.text));
    }

    std::vector<Token> tokens_;
    std::size_t at_ = 0;
    const std::vector<Bytes>* categories_;
    const std::vector<std::pair<std::string, Node>>* macros_;
    std::string_view where_;
};

// The matcher: a pattern against a sequence of categories, with backtracking.
// A place with a mark takes fewer words first, so the shortest reading wins:
// "Are birds animals?" reads as "birds" and "animals", not as one thing.
struct Matcher {
    using Node = Grammar::Node;
    using Next = std::function<bool(std::size_t)>;

    std::span<const Bytes> categories;
    std::vector<Bytes> roles;
    std::size_t furthest = 0;
    std::vector<Bytes> expected;
    /// K3: how many deviations may still be spent, and the ones spent so far
    /// on the path being tried.
    std::size_t budget = 0;
    std::vector<Slip> slips;

    explicit Matcher(std::span<const Bytes> c) : categories(c), roles(c.size()) {}

    /// Spends one deviation on a slip, tries on, and takes it back on failure.
    bool spend(Slip slip, const std::function<bool()>& on) {
        if (budget == 0) {
            return false;
        }
        --budget;
        slips.push_back(std::move(slip));
        if (on()) {
            return true;
        }
        slips.pop_back();
        ++budget;
        return false;
    }

    void expect(std::size_t pos, const Bytes& what) {
        if (pos > furthest) {
            furthest = pos;
            expected.clear();
        }
        if (pos == furthest && !contains(expected, what)) {
            expected.push_back(what);
        }
    }

    bool whole(const Node& body) {
        std::function<bool(std::size_t)> at_end = [&](std::size_t pos) {
            if (pos == categories.size()) {
                return true;
            }
            // K3: a word after the pattern's end is extra.
            if (spend({Slip::Kind::Extra, pos, {}}, [&] {
                    roles[pos] = none;
                    return at_end(pos + 1);
                })) {
                return true;
            }
            expect(pos, end_of_sentence);
            return false;
        };
        return match(body, 0, none, at_end);
    }

    bool match(const Node& node, std::size_t pos, const Bytes& inherited, const Next& next) {
        return repeat(node, 0, pos, inherited, next);
    }

    bool repeat(const Node& node, std::size_t count, std::size_t pos, const Bytes& inherited,
                const Next& next) {
        if (count >= node.min && next(pos)) {
            return true;
        }
        if (node.max == 0 || count < node.max) {
            // A repetition that takes no word and spends no slip would loop.
            const std::size_t spent = slips.size();
            return once(node, pos, inherited, [&](std::size_t p) {
                return (p != pos || slips.size() != spent) && repeat(node, count + 1, p, inherited, next);
            }, count < node.min);
        }
        return false;
    }

    bool once(const Node& node, std::size_t pos, const Bytes& inherited, const Next& next,
              bool required = true) {
        const Bytes& role = node.role.empty() ? inherited : node.role;
        switch (node.kind) {
        case Node::Kind::Place:
            if (pos < categories.size() && contains(node.categories, categories[pos])) {
                roles[pos] = role;
                if (next(pos + 1)) {
                    return true;
                }
            }
            // K3, within the budget: the place is missing from the sentence;
            // the word is extra and the place is tried at the next one; the
            // word stands in the place with the wrong category, which costs
            // two, since it reads a word as what it is not. A place that need
            // not be there is never missing or wrong.
            if (budget > 0) {
                if (required && spend({Slip::Kind::Missing, pos, node.categories.front()},
                                      [&] { return next(pos); })) {
                    return true;
                }
                if (pos < categories.size()) {
                    if (spend({Slip::Kind::Extra, pos, {}}, [&] {
                            roles[pos] = none;
                            return once(node, pos + 1, inherited, next, required);
                        })) {
                        return true;
                    }
                    if (required && budget >= 2 &&
                        spend({Slip::Kind::Wrong, pos, node.categories.front()}, [&] {
                            --budget;
                            roles[pos] = role;
                            if (next(pos + 1)) {
                                return true;
                            }
                            ++budget;
                            return false;
                        })) {
                        return true;
                    }
                }
            }
            for (const Bytes& category : node.categories) {
                expect(pos, category);
            }
            return false;
        case Node::Kind::Sequence:
            return sequence(node, 0, pos, role, next);
        case Node::Kind::Alternation:
            for (const Node& child : node.children) {
                if (match(child, pos, role, next)) {
                    return true;
                }
            }
            return false;
        }
        return false;
    }

    bool sequence(const Node& node, std::size_t i, std::size_t pos, const Bytes& role,
                  const Next& next) {
        if (i == node.children.size()) {
            return next(pos);
        }
        return match(node.children[i], pos, role,
                     [&](std::size_t p) { return sequence(node, i + 1, p, role, next); });
    }
};

}  // namespace

const std::vector<Bytes>& Grammar::roles() {
    static const std::vector<Bytes> all = {
        bytes_of("subject"),    bytes_of("predicate"), bytes_of("attribute"), bytes_of("object"),
        bytes_of("complement"), bytes_of("modifier"),  bytes_of("link"),      bytes_of("none")};
    return all;
}

Grammar::Node Grammar::parse(std::string_view text, std::string_view where) const {
    std::vector<std::pair<std::string, Node>> macros;
    macros.reserve(macros_.size());
    for (const Macro& m : macros_) {
        macros.emplace_back(m.name, m.body);
    }
    Parser parser{tokens_of(text, where), rules_->categories(), macros, where};
    return parser.parse();
}

Grammar::Grammar(const BaseRules& rules) : rules_(&rules) {
    const std::filesystem::path file = rules.directory() / "grammar.txt";
    std::size_t number = 0;
    for (const Bytes& line : rules.grammar()) {
        ++number;
        const std::string_view text = trim(std::string_view{reinterpret_cast<const char*>(line.data()), line.size()});
        const std::string where = std::format("{} pattern {}", file.filename().string(), number);
        if (text.starts_with('@')) {
            const std::size_t equals = text.find('=');
            if (equals == std::string_view::npos) {
                throw std::runtime_error(std::format("{}: a group needs '@name = places'", where));
            }
            Macro macro;
            macro.name = std::string{trim(text.substr(1, equals - 1))};
            macro.body = parse(text.substr(equals + 1), where);
            macros_.push_back(std::move(macro));
            continue;
        }
        const std::size_t colon = text.find(':');
        if (colon == std::string_view::npos || colon == 0) {
            throw std::runtime_error(std::format("{}: a pattern needs 'name: places'", where));
        }
        Pattern pattern;
        pattern.name = std::string{trim(text.substr(0, colon))};
        pattern.text = std::string{trim(text.substr(colon + 1))};
        pattern.body = parse(pattern.text, where);
        pattern.question = pattern.name.find("question") != std::string::npos;
        patterns_.push_back(std::move(pattern));
    }
}

Fit Grammar::fit(std::span<const Bytes> categories, bool question) const {
    Fit out;
    if (categories.empty() || std::ranges::any_of(categories, [](const Bytes& c) { return c.empty(); })) {
        return out;
    }
    std::size_t furthest = 0;
    // The question patterns first for a question, last otherwise.
    for (const bool questions : {question, !question}) {
        for (const Pattern& pattern : patterns_) {
            if (pattern.question != questions) {
                continue;
            }
            Matcher matcher{categories};
            if (matcher.whole(pattern.body)) {
                out.fits = true;
                out.pattern = pattern.name;
                out.roles = std::move(matcher.roles);
                out.breaks_at = 0;
                out.expected.clear();
                return out;
            }
            if (matcher.furthest > furthest || out.pattern.empty()) {
                furthest = matcher.furthest;
                out.pattern = pattern.name;
                out.expected.clear();
            }
            if (matcher.furthest == furthest) {
                for (const Bytes& e : matcher.expected) {
                    if (!contains(out.expected, e)) {
                        out.expected.push_back(e);
                    }
                }
            }
        }
    }
    out.breaks_at = furthest;
    return out;
}

Near Grammar::nearest(std::span<const Bytes> categories, bool question, std::size_t most) const {
    Near out;
    if (categories.empty() || std::ranges::any_of(categories, [](const Bytes& c) { return c.empty(); })) {
        return out;
    }
    for (std::size_t budget = 1; budget <= most; ++budget) {
        for (const bool questions : {question, !question}) {
            for (const Pattern& pattern : patterns_) {
                if (pattern.question != questions) {
                    continue;
                }
                Matcher matcher{categories};
                matcher.budget = budget;
                if (matcher.whole(pattern.body)) {
                    out.found = true;
                    out.pattern = pattern.name;
                    out.roles = std::move(matcher.roles);
                    out.slips = std::move(matcher.slips);
                    return out;
                }
            }
        }
    }
    return out;
}

bool Grammar::learn(std::string_view name, std::span<const Bytes> categories,
                    std::span<const Bytes> roles, bool question) {
    if (categories.empty() || categories.size() != roles.size()) {
        return false;
    }
    for (std::size_t i = 0; i < categories.size(); ++i) {
        if (!contains(rules_->categories(), categories[i]) || !contains(Grammar::roles(), roles[i])) {
            return false;
        }
    }
    const Fit known = fit(categories, question);
    if (known.fits && std::ranges::equal(known.roles, roles)) {
        return false;
    }
    Pattern pattern;
    pattern.name = std::string{name};
    pattern.learned = true;
    pattern.question = question;
    pattern.body.kind = Node::Kind::Sequence;
    for (std::size_t i = 0; i < categories.size(); ++i) {
        Node place;
        place.kind = Node::Kind::Place;
        place.categories.push_back(categories[i]);
        place.role = roles[i];
        pattern.body.children.push_back(std::move(place));
        std::string category = text_of(categories[i]);
        std::ranges::replace(category, ' ', '_');
        pattern.text += (i == 0 ? "" : " ") + category + "/" + text_of(roles[i]);
    }
    patterns_.push_back(std::move(pattern));
    ++learned_;
    return true;
}

std::vector<std::string> Grammar::names() const {
    std::vector<std::string> out;
    out.reserve(patterns_.size());
    for (const Pattern& p : patterns_) {
        out.push_back(p.name);
    }
    return out;
}

std::string Grammar::text(std::string_view name) const {
    for (const Pattern& p : patterns_) {
        if (p.name == name) {
            return p.text;
        }
    }
    return {};
}

}  // namespace larry
