#pragma once

#include "larry/base_rules.hpp"
#include "larry/constellation.hpp"
#include "larry/electron.hpp"
#include "larry/sentence.hpp"

#include <cstdint>
#include <span>

namespace larry {

/// What kind of sentence an atom is. This is the atom's category electron.
enum class Qualification : std::uint8_t {
    Affirmation,
    Question,
    Order,
    Assumption,
    Expression,
};

/// Cognition: the process that compares atoms.
class Cognition {
public:
    /// Qualify a sentence using the rules in the constellation.
    [[nodiscard]] Qualification qualify(const Sentence& sentence,
                                        const Constellation& constellation) const;

    /// Store the category of each word: the entity at each position gets the
    /// category at the same position. Every category has to be one of the
    /// categories in the base rules.
    void categorize(EntitiesElectron& entities, std::span<const Bytes> categories,
                    const BaseRules& base_rules) const;
};

}  // namespace larry
