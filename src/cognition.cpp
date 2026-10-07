#include "larry/cognition.hpp"

#include "larry/atom_operations.hpp"
#include "larry/utf8.hpp"

#include <algorithm>
#include <format>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace larry {

namespace {

Bytes bytes_of(std::string_view text) {
    return Bytes(text.begin(), text.end());
}

bool in(const std::vector<Bytes>& list, const Bytes& item) {
    return std::ranges::contains(list, item);
}

// Does the sentence end with a question mark, before any closing quotes?
bool ends_with_question_mark(std::string_view text, const BaseRules& rules) {
    const auto unit_in = [](const std::vector<Bytes>& list, std::string_view u) {
        return std::ranges::any_of(list, [&](const Bytes& b) {
            return std::string_view{reinterpret_cast<const char*>(b.data()), b.size()} == u;
        });
    };
    std::size_t end = text.size();
    while (end > 0) {
        const std::size_t p = utf8::last_unit_begin(text, 0, end);
        const std::string_view u = text.substr(p, end - p);
        if (!utf8::is_space(u) && !unit_in(rules.closers(), u)) {
            break;
        }
        end = p;
    }
    bool question = false;
    while (end > 0) {
        const std::size_t p = utf8::last_unit_begin(text, 0, end);
        const std::string_view u = text.substr(p, end - p);
        if (!unit_in(rules.sentence_ends(), u)) {
            break;
        }
        question = question || u == "?";
        end = p;
    }
    return question;
}

std::vector<Bytes> folded_words(const Description& d) {
    const AtomOperations ops;
    std::vector<Bytes> out;
    out.reserve(d.entities.entities.size());
    for (const Entity& e : d.entities.entities) {
        out.push_back(ops.fold(e.word));
    }
    return out;
}

void match_in_place(Comparison& c, const Description& a, const Description& b) {
    const std::vector<Bytes> fa = folded_words(a);
    const std::vector<Bytes> fb = folded_words(b);
    const std::size_t n = std::min(fa.size(), fb.size());
    for (std::size_t i = 0; i < n; ++i) {
        c.matches.push_back({i, i, fa[i] == fb[i]});
    }
    for (std::size_t i = n; i < fa.size(); ++i) {
        c.only_a.push_back(i);
    }
    for (std::size_t i = n; i < fb.size(); ++i) {
        c.only_b.push_back(i);
    }
}

}  // namespace

std::string_view name(Qualification qualification) noexcept {
    switch (qualification) {
    case Qualification::Affirmation:
        return "affirmation";
    case Qualification::Question:
        return "question";
    case Qualification::Order:
        return "order";
    case Qualification::Assumption:
        return "assumption";
    case Qualification::Expression:
        return "expression";
    }
    return "";
}

Bytes Comparison::bytes() const {
    std::string out = std::format("C{} {}", static_cast<int>(kind), holds ? "yes" : "no");
    for (const Match& m : matches) {
        out += std::format(" {}{}{}", m.a, m.same_word ? '=' : '~', m.b);
    }
    for (const std::size_t i : only_a) {
        out += std::format(" a{}", i);
    }
    for (const std::size_t i : only_b) {
        out += std::format(" b{}", i);
    }
    if (!pattern.empty()) {
        out += " pattern:";
        out.append(pattern.begin(), pattern.end());
    }
    return bytes_of(out);
}

Qualification Cognition::qualify(const Sentence& sentence, const EntitiesElectron& entities,
                                 const BaseRules& rules) const {
    static const Bytes interjection = bytes_of("interjection");
    static const Bytes adverb = bytes_of("adverb");
    static const Bytes auxiliary_verb = bytes_of("auxiliary verb");
    static const Bytes verb = bytes_of("verb");
    static const Bytes noun = bytes_of("noun");
    static const Bytes proper_noun = bytes_of("proper noun");
    const AtomOperations ops;

    // 1. A sentence that ends with "?" is a question.
    if (ends_with_question_mark(ops.text(sentence), rules)) {
        return Qualification::Question;
    }
    const std::vector<Entity>& list = entities.entities;
    if (list.empty()) {
        return Qualification::Expression;
    }
    // 2. A sentence in the list of expressions, or made of interjections, is
    // an expression.
    Bytes whole;
    for (const Entity& e : list) {
        if (!whole.empty()) {
            whole.push_back(' ');
        }
        const Bytes word = ops.fold(e.word);
        whole.insert(whole.end(), word.begin(), word.end());
    }
    if (in(rules.expressions(), whole)) {
        return Qualification::Expression;
    }
    // A guessed category (A6) never drives the rules: it counts as unknown here.
    static const Bytes guessed = bytes_of("guessed");
    const auto category_of = [&](const Entity& e) -> const Bytes& {
        static const Bytes none;
        return std::ranges::contains(e.types, guessed) ? none : e.category;
    };
    const auto skip = [&](std::size_t from, const std::vector<const Bytes*>& categories) {
        while (from < list.size() && std::ranges::any_of(categories, [&](const Bytes* c) {
                   return category_of(list[from]) == *c;
               })) {
            ++from;
        }
        return from;
    };
    const std::size_t first = skip(0, {&interjection});
    if (first == list.size()) {
        return Qualification::Expression;
    }
    const Entity& opening = list[first];
    // 3. A sentence that opens with a question word is a question, unless the
    // word is used as a noun ("What is a question word").
    if (in(rules.question_words(), ops.fold(opening.word)) && category_of(opening) != noun &&
        category_of(opening) != proper_noun) {
        return Qualification::Question;
    }
    // 4. A sentence that opens with an auxiliary verb is a question ("Can birds
    // fly"), unless a verb follows it: then it is an order ("Do not stop").
    bool imperative = false;
    if (category_of(opening) == auxiliary_verb) {
        const std::size_t next = skip(first + 1, {&adverb});
        if (next < list.size() && category_of(list[next]) == verb) {
            imperative = true;
        } else {
            return Qualification::Question;
        }
    }
    // 5. A sentence that hangs on an assumption word is an assumption.
    for (const Entity& e : list) {
        if (in(rules.assumption_words(), ops.fold(e.word))) {
            return Qualification::Assumption;
        }
    }
    // 6. A sentence that opens with a verb, after any adverb, is an order.
    const std::size_t head = skip(first, {&interjection, &adverb});
    if (imperative || (head < list.size() && category_of(list[head]) == verb)) {
        return Qualification::Order;
    }
    // 7. Anything else is an affirmation.
    return Qualification::Affirmation;
}

void Cognition::categorize(EntitiesElectron& entities, std::span<const Bytes> categories,
                           const BaseRules& base_rules) const {
    if (categories.size() != entities.entities.size()) {
        throw std::invalid_argument(
            std::format("Cognition::categorize: {} words but {} categories",
                        entities.entities.size(), categories.size()));
    }
    for (const Bytes& category : categories) {
        if (!std::ranges::contains(base_rules.categories(), category)) {
            throw std::invalid_argument(
                std::format("Cognition::categorize: \"{}\" is not a category in the base rules",
                            std::string(category.begin(), category.end())));
        }
    }
    for (std::size_t i = 0; i < categories.size(); ++i) {
        entities.entities[i].category = categories[i];
    }
}

Comparison Cognition::identity(const Description& a, const Description& b) const {
    const AtomOperations ops;
    Comparison c;
    c.kind = ComparisonKind::Identity;
    c.holds = std::ranges::equal(ops.bytes(a.atom), ops.bytes(b.atom));
    match_in_place(c, a, b);
    return c;
}

Comparison Cognition::same_form(const Description& a, const Description& b) const {
    Comparison c;
    c.kind = ComparisonKind::SameForm;
    match_in_place(c, a, b);
    c.holds = c.only_a.empty() && c.only_b.empty() &&
              std::ranges::all_of(c.matches, [](const Match& m) { return m.same_word; });
    return c;
}

Comparison Cognition::align(const Description& a, const Description& b) const {
    const std::vector<Bytes> fa = folded_words(a);
    const std::vector<Bytes> fb = folded_words(b);
    const std::vector<Entity>& ea = a.entities.entities;
    const std::vector<Entity>& eb = b.entities.entities;
    const std::size_t n = fa.size();
    const std::size_t m = fb.size();

    // 2 for the same word, 1 for the same known category, 0 for no match.
    const auto score = [&](std::size_t i, std::size_t j) -> int {
        if (fa[i] == fb[j]) {
            return 2;
        }
        if (!ea[i].category.empty() && ea[i].category == eb[j].category) {
            return 1;
        }
        return 0;
    };
    std::vector<std::vector<int>> best(n + 1, std::vector<int>(m + 1, 0));
    for (std::size_t i = 1; i <= n; ++i) {
        for (std::size_t j = 1; j <= m; ++j) {
            int value = std::max(best[i - 1][j], best[i][j - 1]);
            const int s = score(i - 1, j - 1);
            if (s > 0) {
                value = std::max(value, best[i - 1][j - 1] + s);
            }
            best[i][j] = value;
        }
    }
    Comparison c;
    c.kind = ComparisonKind::Alignment;
    std::size_t i = n;
    std::size_t j = m;
    while (i > 0 && j > 0) {
        const int s = score(i - 1, j - 1);
        if (s > 0 && best[i][j] == best[i - 1][j - 1] + s) {
            c.matches.push_back({i - 1, j - 1, s == 2});
            --i;
            --j;
        } else if (best[i][j] == best[i - 1][j]) {
            --i;
        } else {
            --j;
        }
    }
    std::ranges::reverse(c.matches);
    std::vector<bool> matched_a(n, false);
    std::vector<bool> matched_b(m, false);
    for (const Match& match : c.matches) {
        matched_a[match.a] = true;
        matched_b[match.b] = true;
    }
    for (std::size_t k = 0; k < n; ++k) {
        if (!matched_a[k]) {
            c.only_a.push_back(k);
        }
    }
    for (std::size_t k = 0; k < m; ++k) {
        if (!matched_b[k]) {
            c.only_b.push_back(k);
        }
    }
    c.holds = c.only_a.empty() && c.only_b.empty();
    return c;
}

Comparison Cognition::difference(const Description& a, const Description& b) const {
    Comparison c = align(a, b);
    c.kind = ComparisonKind::Difference;
    std::size_t differing = 0;
    std::size_t where = 0;
    for (const Match& m : c.matches) {
        if (!m.same_word) {
            ++differing;
            where = m.a;
        }
    }
    c.holds = differing > 0 || !c.only_a.empty() || !c.only_b.empty();
    if (differing == 1 && c.only_a.empty() && c.only_b.empty()) {
        const std::vector<Entity>& ea = a.entities.entities;
        for (std::size_t i = 0; i < ea.size(); ++i) {
            if (i > 0) {
                c.pattern.push_back(' ');
            }
            if (i == where) {
                c.pattern.push_back('[');
                if (ea[i].category.empty()) {
                    c.pattern.push_back('?');
                } else {
                    c.pattern.insert(c.pattern.end(), ea[i].category.begin(),
                                     ea[i].category.end());
                }
                c.pattern.push_back(']');
            } else {
                c.pattern.insert(c.pattern.end(), ea[i].word.begin(), ea[i].word.end());
            }
        }
    }
    return c;
}

Comparison Cognition::same_structure(const Description& a, const Description& b) const {
    Comparison c;
    c.kind = ComparisonKind::SameStructure;
    match_in_place(c, a, b);
    const std::vector<Entity>& ea = a.entities.entities;
    const std::vector<Entity>& eb = b.entities.entities;
    c.holds = ea.size() == eb.size() && a.type.bytes == b.type.bytes;
    for (std::size_t i = 0; c.holds && i < ea.size(); ++i) {
        c.holds = !ea[i].category.empty() && ea[i].category == eb[i].category &&
                  ea[i].types == eb[i].types;
    }
    return c;
}

}  // namespace larry
