#pragma once

#include "larry/base_rules.hpp"
#include "larry/description.hpp"
#include "larry/electron.hpp"
#include "larry/sentence.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

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

/// The comparisons of form (PLAN.md, track C).
enum class ComparisonKind : std::uint8_t {
    Identity = 1,       ///< C1: are the bits the same?
    SameForm = 2,       ///< C2: the same apart from capitals, spacing and punctuation?
    Alignment = 3,      ///< C3: which entity in one corresponds to which in the other?
    Difference = 4,     ///< C4: where exactly do they differ?
    SameStructure = 5,  ///< C5: the same categories and types in the same order?
};

/// Entity a of the first atom lines up with entity b of the second.
struct Match {
    std::size_t a;
    std::size_t b;
    bool same_word;  ///< The words are the same apart from capitals.
};

/// The result of a comparison of two atoms.
struct Comparison {
    ComparisonKind kind;
    /// The answer to the comparison's question.
    bool holds = false;
    /// Which entity matched which, in sentence order.
    std::vector<Match> matches;
    /// The entities of the first atom that matched nothing, and of the second.
    std::vector<std::size_t> only_a;
    std::vector<std::size_t> only_b;
    /// C4: when exactly one entity differs, the first atom with that entity
    /// left open: "the [noun] is blue". Empty otherwise.
    Bytes pattern;

    /// The result as bytes.
    [[nodiscard]] Bytes bytes() const;
};

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

    /// C1: are the bits the same?
    [[nodiscard]] Comparison identity(const Description& a, const Description& b) const;

    /// C2: are the entities the same apart from capitals, spacing and punctuation?
    [[nodiscard]] Comparison same_form(const Description& a, const Description& b) const;

    /// C3: which entity in one corresponds to which in the other? Entities line
    /// up in order; a pair with the same word counts more than a pair with the
    /// same category. Holds when every entity of both atoms lines up.
    [[nodiscard]] Comparison align(const Description& a, const Description& b) const;

    /// C4: where exactly do they differ? Holds when they differ. When exactly
    /// one entity differs, the result carries the pattern.
    [[nodiscard]] Comparison difference(const Description& a, const Description& b) const;

    /// C5: do they have the same categories and types in the same order? Holds
    /// only when every category is known.
    [[nodiscard]] Comparison same_structure(const Description& a, const Description& b) const;
};

}  // namespace larry
