#pragma once

#include "larry/base_rules.hpp"
#include "larry/description.hpp"
#include "larry/electron.hpp"
#include "larry/forms.hpp"
#include "larry/sentence.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace larry {

class Dictionary;
class Grammar;
class Memory;

/// Assimilation: how Larry takes in language (PLAN.md, track A). It splits
/// text into sentences and entities with the rules of the constellation, and
/// describes a sentence from what was taught and from the atoms in memory.
class Assimilation {
public:
    /// With a dictionary, a word no conception has taught takes its
    /// categories from it (A2b); without one, it is unknown. With a grammar,
    /// the roles come from the pattern the sentence fits (K2); without one,
    /// or when none fits, from the position of each word.
    explicit Assimilation(const BaseRules& rules, const Dictionary* dictionary = nullptr,
                          const Grammar* grammar = nullptr);

    /// A1: the sentences in a text, each an atom: its bytes, without the white
    /// space around them. A sentence ends at a sentence-end mark that is
    /// followed by white space, unless the mark belongs to an abbreviation,
    /// and at a blank line. Text with no word is no sentence.
    [[nodiscard]] std::vector<Sentence> sentences(std::string_view text) const;

    /// A1: the entities of one sentence, with their words as written and no
    /// category yet. An entity is a word, a numeral ("3.14", "1,000"), a
    /// contraction ("don't"), a hyphenated word, an abbreviation ("Mr.",
    /// "e.g.") or, after the first word of the sentence, a name of several
    /// capitalized words ("New York"). Punctuation is not an entity (Q6).
    [[nodiscard]] EntitiesElectron entities(const Sentence& sentence) const;

    /// A2: describe a sentence: its entities, their categories, its
    /// qualification, image and metadata. With taught categories (one per
    /// entity, in order) the description is level 0. Otherwise each word
    /// takes the one category the atoms in memory give it, stays open when
    /// they give several, and is unknown when memory has none or is null.
    [[nodiscard]] Description describe(const Sentence& atom, Memory* memory,
                                       std::span<const Bytes> taught = {}) const;

    /// A5: describes a sentence again from its entities as they are, after a
    /// category was taught or corrected: the qualification, the types (the
    /// "guessed" marks kept), the image and the metadata are made anew.
    void redescribe(Description& d) const;

    /// A7 (first step, Q1 and Q2 as proposed, with emotion): fill the types.
    /// Each entity gets its grammatical features, from its form and category
    /// (number, tense, person, degree, from the endings, forms, pronouns and
    /// auxiliaries in the base rules), and its role: from the grammar pattern
    /// the categories fit (K2), else by position: subject before the first
    /// verb, predicate, then object, attribute after a copula, complement
    /// after a preposition, modifier for adverbs. The atom's type is the
    /// roles in order and its emotion: sarcasm when a marker phrase occurs,
    /// else the emotion of the first emotion word, else neutral.
    void types(Description& d) const;

    /// The emotion of a sentence and what gave it (A3b): sarcasm when a
    /// marker phrase of sarcasm.txt occurs, else the emotion of the first
    /// word in emotions.txt, else neutral with no marker.
    struct Emotion {
        Bytes feeling;
        std::string marker;
    };
    [[nodiscard]] Emotion emotion(const Description& d) const;

    [[nodiscard]] const Grammar* grammar() const noexcept { return grammar_; }
    [[nodiscard]] const Forms& forms() const noexcept { return forms_; }

    /// A4: the form a word is, when its base is known to memory (with the
    /// category the ending gives) or to the dictionary, or when it is an
    /// irregular pair. The word as written, in any case.
    [[nodiscard]] std::optional<Form> form_of(const Bytes& word, const Memory* memory) const;

    /// A9: the image of a described sentence, the form in which two sentences
    /// that say the same thing are equal: its qualification, then its words in
    /// their base form grouped by role in a fixed order (subject, predicate,
    /// object, attribute, complement, modifier, link, none), the articles,
    /// the interjections and the do-support dropped, a number word as its
    /// digits, a contraction expanded, the marks that carry meaning kept
    /// beside a word ("(plural)", "(past)"), and "not" at the end when the
    /// sentence is negated. Text, so `larry show` reads it: "affirmation |
    /// subject: sky | predicate: be | attribute: blue".
    [[nodiscard]] ImageElectron image(const Description& d, const Memory* memory) const;

    /// G1: the word a base takes for a category and a feature ("sky", noun,
    /// plural: "skies"): of the forms the rules give (A4 backwards), the one
    /// memory has met, else the one the dictionary knows, else the first;
    /// the base itself when the rules give none.
    [[nodiscard]] Bytes word_form(const Bytes& base, const Bytes& category, const Bytes& feature,
                                  const Memory* memory) const;

    /// G1: a sentence from an image (A9), the reverse of describing: the
    /// words of each role in the order subject, predicate, object,
    /// attribute, complement, modifier, link, none (a question puts its
    /// question words or its auxiliary first), each base in the form its
    /// marks and the subject ask for (the plural, the time, the person), a
    /// common noun with the article memory saw before it most, the negation
    /// word after the first auxiliary with do-support when the rules have it
    /// ("Penguins do not fly.") or before the predicate when they do not ("O
    /// céu não é azul."), a capital first and the end mark of the
    /// qualification. Describing the result gives the image back. Empty for
    /// an image that is not one.
    [[nodiscard]] std::string sentence_of(const ImageElectron& image, const Memory* memory) const;

    /// A8 (first step): which entity each entity attaches to, as an index,
    /// or `root` for the one that holds the sentence. The entities of a role
    /// form a group, split at a preposition or a conjunction; a group's head
    /// is its last noun, proper noun, pronoun or numeral (its last adjective
    /// for an attribute), and the other words of the group attach to it. The
    /// subject, the objects and the complements attach to the main verb; a
    /// preposition attaches to its own group's head; an auxiliary attaches to
    /// the main verb; with a copula and an attribute, the attribute's head
    /// holds the sentence and the subject and the copula attach to it, as the
    /// treebanks have it. A modifier attaches to the attribute after it, else
    /// to what holds the sentence; a conjunction to the group after it, whose
    /// head attaches to the group before.
    [[nodiscard]] std::vector<std::size_t> attachments(const Description& d) const;

    /// G7 (first step): the image with each base word that has a pair in
    /// `pairs` replaced by its other word, the marks, the roles, the numbers
    /// and the proper nouns kept; the words with no pair are listed in
    /// `missing` when given. The other constellation says it (sentence_of).
    [[nodiscard]] ImageElectron translate(const ImageElectron& image, const std::vector<std::pair<Bytes, Bytes>>& pairs,
                                          std::vector<Bytes>* missing = nullptr) const;
    static constexpr std::size_t root = static_cast<std::size_t>(-1);


private:
    struct Span {
        std::size_t begin;
        std::size_t end;
    };

    [[nodiscard]] bool is_word_unit(std::string_view unit) const noexcept;
    [[nodiscard]] bool is_abbreviation(std::string_view folded) const noexcept;
    [[nodiscard]] std::vector<Span> tokens(std::string_view sentence) const;
    [[nodiscard]] std::vector<Span> join_names(std::string_view sentence,
                                               const std::vector<Span>& tokens) const;

    const BaseRules* rules_;

    Forms forms_;
    const Dictionary* dictionary_;
    const Grammar* grammar_;
    std::vector<std::string> punctuation_;
    std::vector<std::string> sentence_ends_;
    std::vector<std::string> closers_;
    std::vector<std::string> joiners_;
    std::vector<std::string> number_joiners_;
    std::vector<std::string> abbreviations_;
    std::vector<std::string> titles_;
};

}  // namespace larry
