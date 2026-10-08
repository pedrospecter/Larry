#pragma once

#include "larry/base_rules.hpp"
#include "larry/description.hpp"
#include "larry/electron.hpp"
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

    [[nodiscard]] const Grammar* grammar() const noexcept { return grammar_; }


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
