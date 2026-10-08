#pragma once

#include "larry/base_rules.hpp"
#include "larry/description.hpp"
#include "larry/electron.hpp"
#include "larry/grammar.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace larry {

class Assimilation;
class Memory;

/// How Larry read a sentence that deviates from the grammar (K3).
struct Reading {
    /// Whether the sentence is read at all: within the allowance.
    bool accepted = true;
    /// How many deviations the sentence's length allows.
    std::size_t allowed = 0;
    /// The deviations, named: "a missing determiner before \"sky\"", "an
    /// extra word \"the\"", "\"are\" where \"is\" fits (\"sky\" is singular)".
    std::vector<std::string> deviations;
    /// How many of them count against the allowance: a word of the wrong
    /// category counts two; a determiner memory put back, or a filler made
    /// to agree, counts nothing.
    std::size_t counted = 0;
    /// The pattern the sentence was read by: the one it fits, or the nearest.
    std::string pattern;
    /// The sentence as meant, to think with: the sentence as said when
    /// nothing was changed by the reading.
    Description meant;
    /// Whether `meant` differs from what was said.
    bool changed = false;
    /// When refused: why.
    std::string reason;
};

/// Tolerance for non-native English (K3): a sentence no pattern fits is read
/// by the nearest pattern when its deviations are within the allowance in
/// tolerance.txt, each deviation named; a missing place is filled in the
/// reading when the rules name a filler ("is" for an auxiliary verb), an
/// extra word is left out, a word of the wrong category costs two, and an
/// auxiliary verb or verb that does not agree with its subject in number is
/// read as the form that does ("are" as "is", "have" as "has"). A noun that
/// memory knows only with a determiner before it ("the sky") and comes
/// without one is a deviation too, and the reading puts the determiner it
/// is known with back, since the grammar alone allows "Snow is white". The
/// sentence as said is what gets stored; the reading is what Larry thinks
/// with, and it is noted in the reply.
class Tolerance {
public:
    /// Without a grammar, every sentence is read as said.
    Tolerance(const BaseRules& rules, const Grammar* grammar);

    /// The reading of a described sentence. Describing the reading needs the
    /// assimilation and the memory the sentence was described with.
    [[nodiscard]] Reading read(const Description& said, const Assimilation& assimilation,
                               Memory* memory) const;

    /// How many deviations a sentence of this many words may have: one per
    /// `words per deviation` words, rounded up, at most `most deviations`.
    [[nodiscard]] std::size_t allowed(std::size_t words) const noexcept;

    [[nodiscard]] std::size_t words_per_deviation() const noexcept { return per_; }
    [[nodiscard]] std::size_t most_deviations() const noexcept { return most_; }

    /// The word that stands in for a missing place of a category, if any.
    [[nodiscard]] std::optional<Bytes> filler(const Bytes& category) const;

    /// The form of an auxiliary verb that agrees with a subject of this
    /// number ("are" for "is" and plural), or nothing when the word is not
    /// in the table or already agrees.
    [[nodiscard]] std::optional<Bytes> agreeing(const Bytes& folded_auxiliary, bool plural) const;

    /// The subject's head and the predicate (an auxiliary verb, else a verb)
    /// of a described sentence when they disagree in number: the predicate's
    /// index and the form that agrees. Nothing when they agree, the form is
    /// not in the table, or it cannot be judged.
    struct Disagreement {
        std::size_t predicate;
        std::size_t head;
        bool plural;
        Bytes form;
    };
    [[nodiscard]] std::optional<Disagreement> disagreement(const Description& d) const;

    /// A noun phrase without a determiner whose head memory knows only with
    /// one before it: the index where the determiner is missing, the head's
    /// index, the determiner it is known with and in how many uses. Nothing
    /// without memory, or when memory does not say so.
    struct MissingDeterminer {
        std::size_t at;
        std::size_t head;
        Bytes determiner;
        std::size_t uses;
    };
    [[nodiscard]] std::vector<MissingDeterminer> missing_determiners(const Description& d,
                                                                     Memory* memory) const;

private:
    const BaseRules* rules_;
    const Grammar* grammar_;
    std::size_t per_ = 4;
    std::size_t most_ = 2;
    std::vector<std::pair<Bytes, Bytes>> fillers_;
    std::vector<std::pair<Bytes, Bytes>> singular_plural_;
};

}  // namespace larry
