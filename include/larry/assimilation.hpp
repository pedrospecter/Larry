#pragma once

#include "larry/base_rules.hpp"
#include "larry/electron.hpp"
#include "larry/sentence.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace larry {

/// Assimilation: how Larry takes in language (PLAN.md, track A). It splits
/// text into sentences and entities with the rules of the constellation, and
/// describes a sentence from what was taught and from the atoms in memory.
class Assimilation {
public:
    explicit Assimilation(const BaseRules& rules);

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
    std::vector<std::string> punctuation_;
    std::vector<std::string> sentence_ends_;
    std::vector<std::string> closers_;
    std::vector<std::string> joiners_;
    std::vector<std::string> number_joiners_;
    std::vector<std::string> abbreviations_;
    std::vector<std::string> titles_;
};

}  // namespace larry
