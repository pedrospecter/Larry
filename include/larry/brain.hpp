#pragma once

#include "larry/algebra.hpp"
#include "larry/arithmetic.hpp"
#include "larry/assimilation.hpp"
#include "larry/base_rules.hpp"
#include "larry/calendar.hpp"
#include "larry/cognition.hpp"
#include "larry/database.hpp"
#include "larry/description.hpp"
#include "larry/dictionary.hpp"
#include "larry/electron.hpp"
#include "larry/grammar.hpp"
#include "larry/harness.hpp"
#include "larry/memory.hpp"
#include "larry/sentence.hpp"
#include "larry/tolerance.hpp"

#include <cstdint>
#include <functional>
#include <optional>
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
    /// The rules that decided it, as text: "blue and green are both colours,
    /// and a thing has one colour at a time".
    std::vector<std::string> rules;
    /// When unknown, the conceptions that share the most words with the concept.
    std::vector<StoredAtom> nearest;
    /// Whether the answer came from the cloud, not from the cache.
    bool from_cloud = false;
    /// K3: the sentence as Larry read it, when the reading changed what was
    /// said ("The sky is blue." for "Sky is blue."); empty otherwise.
    std::string reading;
    /// K3: the deviations from the grammar, named; and whether they were
    /// beyond the allowance, so that the claim was not read at all.
    std::vector<std::string> deviations;
    bool refused = false;
    /// K4: what is unusual in the claim against the conceptions: a word
    /// never used with the word it is used with here, and what is known
    /// instead.
    std::vector<std::string> unusual;
};

/// K5: how a sentence is qualified: by the rules (A3), with the rule that
/// fired, and by the conceptions of the same structure (C5), with the
/// examples. When the two disagree, Larry says both.
struct Qualifying {
    Qualification by_rules = Qualification::Affirmation;
    std::string rule;
    /// What most of the examples are; nothing without examples or on a tie.
    std::optional<Qualification> by_examples;
    /// The conceptions of the same structure: the validated ones when there
    /// are any, else the proposed ones.
    std::vector<StoredAtom> examples;
    bool validated = false;
    bool from_cloud = false;

    [[nodiscard]] bool agree() const noexcept {
        return !by_examples.has_value() || *by_examples == by_rules;
    }
    /// The answer with its reason, as one line.
    [[nodiscard]] std::string text() const;
};

/// An order Larry knows how to do (W3, the first step of S1): the operation
/// and its arguments, from the pattern in commands.txt that the sentence
/// matched.
struct Command {
    std::string operation;
    std::vector<std::string> arguments;  ///< One per '*' in the pattern, the words as written.
    std::string pattern;
};

/// What Larry says back, with what it used to say it (rule 6).
struct Reply {
    std::string text;
    /// The conceptions and rules the reply came from, as text.
    std::vector<std::string> because;
    /// Whether the sentence was stored as a conception or an assumption.
    bool stored = false;
    /// W3: the command the sentence is, for whoever runs the brain to do.
    std::optional<Command> command;
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
    /// Without a cloud, the brain has only the cache; without a dictionary,
    /// only what the conceptions taught; without a grammar, roles by
    /// position. With a grammar, the validated conceptions of the cache add
    /// their patterns to it (K2), now and as they are validated.
    Brain(const BaseRules& rules, Memory& memory, Database* cloud = nullptr,
          const Dictionary* dictionary = nullptr, Grammar* grammar = nullptr);

    /// R1 (first step) and K1: is this concept true? A concept is true when an
    /// affirmation in memory has the same core with the same polarity, false
    /// when one has the same core with the opposite polarity, and false too
    /// when one gives the same thing another exclusive attribute ("the sky is
    /// green" against "the sky is blue": a thing has one colour at a time);
    /// the negation of such a false concept is true. Unknown otherwise. A
    /// yes/no question ("Is the sky blue?") is read as the statements it asks
    /// about ("the sky is blue"). Larry never produces an answer it cannot
    /// trace to conceptions and rules.
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

    /// W3: the command a described sentence is, when its words, after any
    /// "please", match a pattern in commands.txt; nothing otherwise.
    [[nodiscard]] std::optional<Command> command(const Description& d) const;

    /// M1, M2: the calculation a sentence asks for, when it is arithmetic
    /// or about dates.
    [[nodiscard]] std::optional<Calculation> calculate(const Sentence& sentence) const;

    /// A3b: what a sentence is, in the user's words, with its reasons.
    struct Recognition {
        std::string kind;  ///< "statement", "question", "request", "assumption", "expression".
        std::string kind_reason;
        Qualification qualification = Qualification::Affirmation;
        std::string emotion;  ///< "neutral", "sarcasm", "joy", ...
        std::string emotion_reason;
        std::optional<Command> command;
        std::optional<Calculation> calculation;
        Description description;
    };
    [[nodiscard]] Recognition recognize(const Sentence& sentence) const;

    /// W3: what Larry can do, as the first pattern of each operation:
    /// "search for *", "define *", ...
    [[nodiscard]] std::vector<std::string> abilities() const;

    /// Answers a question or judges a claim, and stores nothing: what `larry
    /// ask` does. A question gets yes, no, the conception that fills its gap
    /// or "I don't know"; a claim gets true, false or I don't know; an
    /// assumption is not judged, an order not done, an expression returned.
    /// The reply carries the conceptions and rules it came from, the reading
    /// of the sentence when it deviated, and what is unusual in it.
    [[nodiscard]] Reply answer(const Sentence& sentence);

    /// Stores a conception in the cache and, when there is a cloud, in the
    /// cloud. What the cache says about it is the result. It stays proposed:
    /// only a validator decides (R2, first step, the user's safeguard). A
    /// conception already there takes this description when it differs and
    /// is complete (Q28): the machine describes, the record follows. With a
    /// reading (Q29), the sentence as Larry read it when it differed from
    /// what was said, the conception is noted as read that way.
    Stored remember(const Description& d, Status status, std::string_view source,
                    std::string_view reading = "");

    /// The validators: the names allowed to validate or withdraw, from the
    /// cloud when there is one, else from the cache.
    [[nodiscard]] std::vector<std::string> validators() const;
    [[nodiscard]] bool is_validator(std::string_view name) const;

    /// Adds a validator. The first one may be added by anyone; after that,
    /// only a validator adds another. False when refused.
    bool add_validator(std::string_view name, std::string_view by);

    /// Validates or withdraws a conception, in the cache and the cloud, when
    /// `by` is a validator. Throws std::runtime_error when it is not: Larry
    /// rejects a decision from anyone but a validator.
    bool decide(const MetadataElectron& metadata, Status status, std::string_view by);

    /// The conceptions waiting for validation: the cloud's when there is a
    /// cloud, else the cache's.
    [[nodiscard]] std::vector<StoredAtom> proposed() const;

    /// The conception with this id in the cloud, or in the cache without one.
    [[nodiscard]] std::optional<StoredAtom> conception(std::int64_t id) const;

    /// Sets a conception's status in the cache and in the cloud, with who
    /// decided. False when neither has it. decide() is the guarded way.
    bool set_status(const MetadataElectron& metadata, Status status, std::string_view by = "");

    /// What sync did.
    struct Synced {
        std::int64_t pushed = 0;       ///< Conceptions the cloud lacked.
        std::int64_t pulled = 0;       ///< Conceptions the cache lacked.
        std::int64_t redescribed = 0;  ///< Conceptions the cloud held with an older description.
        std::int64_t refreshed = 0;    ///< Cached conceptions whose standing the cloud changed.
        std::int64_t bonds_pushed = 0; ///< Bonds the cloud lacked (N3).
        std::int64_t bonds_pulled = 0; ///< Bonds the cache lacked.
        std::int64_t members_pushed = 0;  ///< Molecule members the cloud lacked (N4).
        std::int64_t members_pulled = 0;  ///< Molecule members the cache lacked.
        bool operator==(const Synced&) const = default;
    };

    /// N2: pushes every conception of the cache that the cloud does not have,
    /// gives the cloud the cache's description where it holds an older one
    /// (Q28), pulls the cloud's most recent conceptions into the cache,
    /// refreshes the standing of the cached ones, and exchanges the bonds
    /// (N3) both ways. Throws when there is no cloud.
    Synced sync(std::int64_t pull);

    /// N3: records a bond in the cache and, with a cloud, in the cloud. True
    /// when the cache did not have it.
    bool bond(const Bond& bond);

    /// N3: the bonds at an end, either way: the cache's; when the cache has
    /// none and there is a cloud, the cloud's, which are cached then.
    [[nodiscard]] std::vector<Bond> bonds_of(const BondEnd& end) const;

    /// N3: the conception an atom end names, from the cache, else from the
    /// cloud; nothing for an entity end or an identity nobody holds.
    [[nodiscard]] std::optional<StoredAtom> conception_at(const BondEnd& end) const;

    /// N4: from here on, what the brain remembers joins this molecule: the
    /// text or the conversation being heard, with who said it (the source)
    /// and when (now). Empty: nothing joins. Names: "read:<file>:<when>",
    /// "chat:<user>:<when>".
    void molecule(Bytes name) { molecule_ = std::move(name); }
    [[nodiscard]] const Bytes& molecule() const noexcept { return molecule_; }

    /// N4: the molecule with this name, from the cache, else from the cloud.
    [[nodiscard]] std::optional<Molecule> molecule_named(const Bytes& name) const;

    /// N4: the time now as a member records it: ISO 8601 in UTC.
    [[nodiscard]] static std::string now();

    /// N2c: the cloud is the record. Every cached conception takes the
    /// status, the decision and the reading the cloud holds for it, in one
    /// query. How many changed; nothing without a cloud. Larry does this
    /// when it starts with a cloud, so that a question is answered by what
    /// the record says, not by what the cache remembered.
    std::int64_t refresh();

    /// R1 (first step): the conceptions that answer a question that opens
    /// with a question word: those whose core has the known words of the
    /// question around the gap. "What is the sky?" asks for "the sky is
    /// [?]"; "Who went to the kitchen?" for "[?] went to the kitchen".
    [[nodiscard]] std::vector<StoredAtom> answers(const Description& question) const;

    /// K3: how the brain reads a described sentence: as said when it fits
    /// the grammar, else by the nearest pattern within the allowance, with
    /// the deviations named; refused beyond it.
    [[nodiscard]] Reading read(const Description& said) const;

    /// K4: the context harness on a described sentence: each relation of
    /// its words judged against the conceptions, the cache first and the
    /// cloud when the cache has nothing.
    [[nodiscard]] Report judge(const Description& d) const;

    /// K5: the qualification of a described sentence by the rules and by the
    /// conceptions of the same structure, with the reasons.
    [[nodiscard]] Qualifying qualify(const Description& d) const;

    /// The core of a described sentence.
    [[nodiscard]] Core core(const Description& d) const;

    /// The statements a yes/no question asks about, as cores: the auxiliary
    /// verb it opens with moved after each possible subject. "Is the sky
    /// blue" gives "the is sky blue" and "the sky is blue". Do-support goes:
    /// "Do birds fly" gives "birds fly". Empty when the sentence is not a
    /// yes/no question.
    [[nodiscard]] std::vector<Core> statements(const Description& question) const;

    /// G4a: what to call with a short line while the brain does something
    /// slow (a search in the cloud), so that a wait is never empty.
    using Notice = std::function<void(std::string_view)>;
    void notice(Notice on_notice) {
        notice_ = std::move(on_notice);
        last_notice_.clear();
    }

    [[nodiscard]] Memory& memory() const noexcept { return *memory_; }
    [[nodiscard]] Database* cloud() const noexcept { return cloud_; }
    [[nodiscard]] const Assimilation& assimilation() const noexcept { return assimilation_; }
    [[nodiscard]] Grammar* grammar() const noexcept { return grammar_; }
    [[nodiscard]] const Tolerance& tolerance() const noexcept { return tolerance_; }
    [[nodiscard]] const Harness& harness() const noexcept { return harness_; }

    /// K2: adds the pattern of a validated conception to the grammar: its
    /// categories with their roles, named after the sentence. False when
    /// there is no grammar, the conception is not validated or was read as
    /// something else (Q29), a category or role is missing, or the grammar
    /// already gives those roles.
    bool learn_grammar(const StoredAtom& atom);
    [[nodiscard]] const Cognition& cognition() const noexcept { return cognition_; }

private:
    /// hear() with a source and answer() without one: the one loop, which
    /// stores what it hears only when `store` is set.
    [[nodiscard]] Reply respond(const Sentence& sentence, std::string_view source, bool store);
    [[nodiscard]] std::vector<Bytes> expanded_words(const Description& d) const;
    [[nodiscard]] Core core_of(std::vector<Bytes> words) const;
    /// The exclusive group two different words share, or empty: "colour".
    [[nodiscard]] Bytes exclusive_group(const Bytes& a, const Bytes& b) const;
    [[nodiscard]] bool is_auxiliary(const Bytes& folded_word, const Bytes& category) const;

    /// The conceptions of the cache or the cloud that contain the rarest word
    /// of a core, affirmations only.
    [[nodiscard]] std::vector<StoredAtom> candidates(const Core& form, bool cloud) const;
    /// The spellings a core word may have in a stored atom: "3" and "three".
    [[nodiscard]] std::vector<Bytes> spellings(const Bytes& word) const;
    /// Puts a conception the cloud gave into the cache.
    void cache(const StoredAtom& atom) const;

    /// Says it once: the same notice twice in a row is one wait.
    void tell(std::string_view what) const {
        if (notice_ && what != last_notice_) {
            last_notice_ = std::string{what};
            notice_(what);
        }
    }

    const BaseRules* rules_;
    Grammar* grammar_;
    Assimilation assimilation_;
    Tolerance tolerance_;
    Harness harness_;
    Arithmetic arithmetic_;
    Calendar calendar_;
    Algebra algebra_;
    Cognition cognition_;
    Memory* memory_;
    Database* cloud_;
    Notice notice_;
    mutable std::string last_notice_;
    Bytes molecule_;  ///< N4: what is remembered joins it, when it is named.
};

}  // namespace larry
