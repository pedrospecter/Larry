#include "larry/cognition.hpp"

#include <algorithm>
#include <cstddef>
#include <format>
#include <stdexcept>
#include <string>

namespace larry {

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

Qualification Cognition::qualify(const Sentence& /*sentence*/, const EntitiesElectron& /*entities*/,
                                 const BaseRules& /*rules*/) const {
    throw std::logic_error("Cognition::qualify: the constellation has no qualification rules yet");
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
