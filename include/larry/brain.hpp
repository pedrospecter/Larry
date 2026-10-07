#pragma once

#include "larry/assimilation.hpp"
#include "larry/base_rules.hpp"
#include "larry/cognition.hpp"
#include "larry/description.hpp"
#include "larry/electron.hpp"
#include "larry/memory.hpp"
#include "larry/sentence.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace larry {

/// What Larry holds a concept to be, from its conceptions.
enum class Truth : std::uint8_t {
    True,     ///< A conception says the same, with the same polarity.
    False,    ///< A conception says the same with the opposite polarity.
    Unknown,  ///< No conception says the same either way.
};

/// The answer to "is this true?", with the conceptions it came from (PLAN.md,
/// section 3, rule 6: every result can be traced).
struct Verdict {
    Truth truth = Truth::Unknown;
    /// The conceptions that decided it: one for true or false.
    std::vector<StoredAtom> because;
    /// When unknown, the conceptions that share the most words with the concept.
    std::vector<StoredAtom> nearest;
};

/// A sentence reduced to what it claims: its words in lower case, with
/// contractions expanded, negation words and do-support removed, and
/// whether it was negated.
struct Core {
    std::vector<Bytes> words;
    bool negated = false;
};

/// The brain (Q4, proposed): the loop that takes input, uses the network
/// (memory) and cognition, and replies.
class Brain {
public:
    Brain(const BaseRules& rules, Memory& memory);

    /// R1 (first step): is this concept true? A concept is true when an
    /// affirmation in memory has the same core with the same polarity, false
    /// when one has the same core with the opposite polarity, and unknown
    /// otherwise. A yes/no question ("Is the sky blue?") is read as the
    /// statements it asks about ("the sky is blue"). Larry never produces an
    /// answer it cannot trace to conceptions.
    [[nodiscard]] Verdict truth(const Sentence& claim) const;
    [[nodiscard]] Verdict truth(const Description& claim) const;

    /// The core of a described sentence.
    [[nodiscard]] Core core(const Description& d) const;

    /// The statements a yes/no question asks about, as cores: the auxiliary
    /// verb it opens with moved after each possible subject. "Is the sky
    /// blue" gives "the is sky blue" and "the sky is blue". Do-support goes:
    /// "Do birds fly" gives "birds fly". Empty when the sentence is not a
    /// yes/no question.
    [[nodiscard]] std::vector<Core> statements(const Description& question) const;

    [[nodiscard]] Memory& memory() const noexcept { return *memory_; }
    [[nodiscard]] const Assimilation& assimilation() const noexcept { return assimilation_; }
    [[nodiscard]] const Cognition& cognition() const noexcept { return cognition_; }

private:
    [[nodiscard]] std::vector<Bytes> expanded_words(const Description& d) const;
    [[nodiscard]] Core core_of(std::vector<Bytes> words) const;
    [[nodiscard]] bool is_auxiliary(const Bytes& folded_word, const Bytes& category) const;

    const BaseRules* rules_;
    Assimilation assimilation_;
    Cognition cognition_;
    Memory* memory_;
};

}  // namespace larry
