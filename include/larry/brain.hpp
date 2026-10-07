#pragma once

#include "larry/assimilation.hpp"
#include "larry/base_rules.hpp"
#include "larry/cognition.hpp"
#include "larry/database.hpp"
#include "larry/description.hpp"
#include "larry/electron.hpp"
#include "larry/memory.hpp"
#include "larry/sentence.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
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
    /// Whether the answer came from the cloud, not from the cache.
    bool from_cloud = false;
};

/// What Larry says back, with what it used to say it (rule 6).
struct Reply {
    std::string text;
    /// The conceptions and rules the reply came from, as text.
    std::vector<std::string> because;
    /// Whether the sentence was stored as a conception or an assumption.
    bool stored = false;
};

/// A sentence reduced to what it claims: its words in lower case, with
/// contractions expanded, negation words and do-support removed, and
/// whether it was negated.
struct Core {
    std::vector<Bytes> words;
    bool negated = false;
};

/// The brain (Q4, proposed): the loop that takes input, uses the network
/// (memory, and the cloud behind it) and cognition, and replies. Memory is
/// the cache on the machine and answers first; what it cannot answer, the
/// brain searches in the cloud, and what it finds there goes into the cache.
class Brain {
public:
    /// Without a cloud, the brain has only the cache.
    Brain(const BaseRules& rules, Memory& memory, Database* cloud = nullptr);

    /// R1 (first step): is this concept true? A concept is true when an
    /// affirmation in memory has the same core with the same polarity, false
    /// when one has the same core with the opposite polarity, and unknown
    /// otherwise. A yes/no question ("Is the sky blue?") is read as the
    /// statements it asks about ("the sky is blue"). Larry never produces an
    /// answer it cannot trace to conceptions.
    [[nodiscard]] Verdict truth(const Sentence& claim) const;
    [[nodiscard]] Verdict truth(const Description& claim) const;

    /// G3 (first step): hear one sentence and act by its qualification. An
    /// affirmation is stored, after Larry checks what it already holds: the
    /// same, a conflict (it keeps both and says so), or something new (it
    /// asks about a word it does not know). A question is answered: yes, no,
    /// a conception that fills the gap, or "I don't know". An order is
    /// refused for now. An assumption is stored as one, never as a truth. An
    /// expression is answered in kind.
    [[nodiscard]] Reply hear(const Sentence& sentence, std::string_view source = "user");

    /// Stores a conception in the cache and, when there is a cloud, in the
    /// cloud. What the cache says about it is the result.
    Stored remember(const Description& d, Status status, std::string_view source);

    /// N2: pushes every conception of the cache that the cloud does not have,
    /// and pulls the cloud's most recent ones into the cache. Gives the two
    /// counts. Throws when there is no cloud.
    std::pair<std::int64_t, std::int64_t> sync(std::int64_t pull);

    /// R1 (first step): the conceptions that answer a question that opens
    /// with a question word: those whose core has the known words of the
    /// question around the gap. "What is the sky?" asks for "the sky is
    /// [?]"; "Who went to the kitchen?" for "[?] went to the kitchen".
    [[nodiscard]] std::vector<StoredAtom> answers(const Description& question) const;

    /// The core of a described sentence.
    [[nodiscard]] Core core(const Description& d) const;

    /// The statements a yes/no question asks about, as cores: the auxiliary
    /// verb it opens with moved after each possible subject. "Is the sky
    /// blue" gives "the is sky blue" and "the sky is blue". Do-support goes:
    /// "Do birds fly" gives "birds fly". Empty when the sentence is not a
    /// yes/no question.
    [[nodiscard]] std::vector<Core> statements(const Description& question) const;

    [[nodiscard]] Memory& memory() const noexcept { return *memory_; }
    [[nodiscard]] Database* cloud() const noexcept { return cloud_; }
    [[nodiscard]] const Assimilation& assimilation() const noexcept { return assimilation_; }
    [[nodiscard]] const Cognition& cognition() const noexcept { return cognition_; }

private:
    [[nodiscard]] std::vector<Bytes> expanded_words(const Description& d) const;
    [[nodiscard]] Core core_of(std::vector<Bytes> words) const;
    [[nodiscard]] bool is_auxiliary(const Bytes& folded_word, const Bytes& category) const;

    /// The conceptions of the cache or the cloud that contain the rarest word
    /// of a core, affirmations only.
    [[nodiscard]] std::vector<StoredAtom> candidates(const Core& form, bool cloud) const;
    /// Puts a conception the cloud gave into the cache.
    void cache(const StoredAtom& atom) const;

    const BaseRules* rules_;
    Assimilation assimilation_;
    Cognition cognition_;
    Memory* memory_;
    Database* cloud_;
};

}  // namespace larry
