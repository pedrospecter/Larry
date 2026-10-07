#include "larry/cognition.hpp"

#include "larry/atom_operations.hpp"
#include "larry/utf8.hpp"

#include <algorithm>
#include <format>
#include <stdexcept>
#include <string>

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
    const auto skip = [&](std::size_t from, const std::vector<const Bytes*>& categories) {
        while (from < list.size() &&
               std::ranges::any_of(categories, [&](const Bytes* c) { return list[from].category == *c; })) {
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
    if (in(rules.question_words(), ops.fold(opening.word)) && opening.category != noun &&
        opening.category != proper_noun) {
        return Qualification::Question;
    }
    // 4. A sentence that opens with an auxiliary verb is a question ("Can birds
    // fly"), unless a verb follows it: then it is an order ("Do not stop").
    bool imperative = false;
    if (opening.category == auxiliary_verb) {
        const std::size_t next = skip(first + 1, {&adverb});
        if (next < list.size() && list[next].category == verb) {
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
    if (imperative || (head < list.size() && list[head].category == verb)) {
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

}  // namespace larry
