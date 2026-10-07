#pragma once

#include "larry/base_rules.hpp"
#include "larry/electron.hpp"
#include "larry/sentence.hpp"

#include <cstdint>
#include <span>
#include <string_view>

namespace larry {

/// What kind of sentence an atom is. This is the atom's category electron.
enum class Qualification : std::uint8_t {
    Affirmation,
    Question,
    Order,
    Assumption,
    Expression,
};

/// The name of a qualification: the bytes of the category electron.
[[nodiscard]] std::string_view name(Qualification qualification) noexcept;

/// Cognition: the process that compares atoms.
class Cognition {
public:
    /// Qualify a sentence with the rules of the base rules and the categories
    /// its entities have (A3). The rules are the proposed answer to Q5: a
    /// sentence that ends with "?" or opens with a question word or an
    /// auxiliary verb is a question; one that is an expression in the base
    /// rules or is made of interjections is an expression; one that hangs on
    /// an assumption word is an assumption; one that opens with a verb is an
    /// order; anything else is an affirmation.
    [[nodiscard]] Qualification qualify(const Sentence& sentence, const EntitiesElectron& entities,
                                        const BaseRules& rules) const;

    /// Store the category of each word: the entity at each position gets the
    /// category at the same position. Every category has to be one of the
    /// categories in the base rules.
    void categorize(EntitiesElectron& entities, std::span<const Bytes> categories,
                    const BaseRules& base_rules) const;
};

}  // namespace larry
