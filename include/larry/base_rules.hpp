#pragma once

#include "larry/constellation.hpp"
#include "larry/electron.hpp"

#include <filesystem>
#include <string_view>
#include <utility>
#include <vector>

namespace larry {

/// The base rules of a language, read from its directory in base_rules/, which
/// is named by the language's locale ("en"). Each file holds one item per line,
/// each byte written as two hex digits (PLAN.md, section 3, rule 4). Empty
/// lines and lines that start with '#' are comments. scripts/hex.sh turns a
/// plain list into this form and back.
class BaseRules {
public:
    explicit BaseRules(Language language);

    [[nodiscard]] Language language() const noexcept { return language_; }

    /// The categories a word can have, from categories.txt.
    [[nodiscard]] const std::vector<Bytes>& categories() const noexcept { return categories_; }

    /// Marks that are never part of a word, from punctuation.txt. A mark is
    /// not an entity: it stays in the atom's bits (Q6).
    [[nodiscard]] const std::vector<Bytes>& punctuation() const noexcept { return punctuation_; }

    /// Marks that end a sentence, from sentence_ends.txt.
    [[nodiscard]] const std::vector<Bytes>& sentence_ends() const noexcept {
        return sentence_ends_;
    }

    /// Quotes and brackets that may follow the end of a sentence, from closers.txt.
    [[nodiscard]] const std::vector<Bytes>& closers() const noexcept { return closers_; }

    /// Marks that join two parts of one word ("don't", "well-known"), from joiners.txt.
    [[nodiscard]] const std::vector<Bytes>& joiners() const noexcept { return joiners_; }

    /// Marks that join two runs of digits ("3.14", "1,000", "10:30"), from
    /// number_joiners.txt.
    [[nodiscard]] const std::vector<Bytes>& number_joiners() const noexcept {
        return number_joiners_;
    }

    /// Words that end with a full stop that is part of them ("mr.", "etc."), in
    /// lower case, from abbreviations.txt.
    [[nodiscard]] const std::vector<Bytes>& abbreviations() const noexcept {
        return abbreviations_;
    }

    /// Abbreviations after which a capital does not start a new sentence
    /// ("Dr. Smith"), from titles.txt.
    [[nodiscard]] const std::vector<Bytes>& titles() const noexcept { return titles_; }

    /// Words that open a question ("what"), from question_words.txt.
    [[nodiscard]] const std::vector<Bytes>& question_words() const noexcept {
        return question_words_;
    }

    /// Words a sentence hangs on to be an assumption ("if"), from assumption_words.txt.
    [[nodiscard]] const std::vector<Bytes>& assumption_words() const noexcept {
        return assumption_words_;
    }

    /// Whole sentences that are expressions ("hello", "thank you"), in lower
    /// case, from expressions.txt.
    [[nodiscard]] const std::vector<Bytes>& expressions() const noexcept { return expressions_; }

    /// Words that negate a sentence ("not", "never"), from negation_words.txt.
    [[nodiscard]] const std::vector<Bytes>& negation_words() const noexcept {
        return negation_words_;
    }

    /// Contractions and the words they stand for ("isn't" and "is not"), in
    /// lower case, from contractions.txt: each line the contraction, '=', the words.
    [[nodiscard]] const std::vector<std::pair<Bytes, Bytes>>& contractions() const noexcept {
        return contractions_;
    }

    /// Regular endings and the feature they give a word of a category, from
    /// endings.txt: the ending, and "category:feature". Longest first.
    [[nodiscard]] const std::vector<std::pair<Bytes, Bytes>>& endings() const noexcept {
        return endings_;
    }

    /// Words whose features the endings get wrong or that have none, from
    /// forms.txt: the word, and "category:feature;feature".
    [[nodiscard]] const std::vector<std::pair<Bytes, Bytes>>& forms() const noexcept {
        return forms_;
    }

    /// Pronouns and their features ("first person;singular"), from pronouns.txt.
    [[nodiscard]] const std::vector<std::pair<Bytes, Bytes>>& pronouns() const noexcept {
        return pronouns_;
    }

    /// Auxiliary verbs and their features, from auxiliaries.txt.
    [[nodiscard]] const std::vector<std::pair<Bytes, Bytes>>& auxiliaries() const noexcept {
        return auxiliaries_;
    }

    /// Words that carry an emotion, and which, from emotions.txt.
    [[nodiscard]] const std::vector<std::pair<Bytes, Bytes>>& emotions() const noexcept {
        return emotions_;
    }

    /// Marker phrases of sarcasm, from sarcasm.txt.
    [[nodiscard]] const std::vector<Bytes>& sarcasm() const noexcept { return sarcasm_; }

    /// A group of exclusive attributes: values a thing has one of at a time.
    struct Exclusive {
        Bytes name;
        std::vector<Bytes> words;
    };

    /// The exclusive attributes, from exclusives.txt (K1).
    [[nodiscard]] const std::vector<Exclusive>& exclusives() const noexcept { return exclusives_; }

    /// Number words and their digits ("three" and "3"), from number_words.txt.
    [[nodiscard]] const std::vector<std::pair<Bytes, Bytes>>& number_words() const noexcept {
        return number_words_;
    }

    /// The grammar patterns as written, one per line, from grammar.txt (K2).
    /// The Grammar class parses them.
    [[nodiscard]] const std::vector<Bytes>& grammar() const noexcept { return grammar_; }

    /// The content markers, from content.txt (W2): "reference", "speech",
    /// "context", "heading" lists and the "least words" and "most words" of
    /// a fact. The Content class reads them.
    [[nodiscard]] const std::vector<std::pair<Bytes, Bytes>>& content() const noexcept {
        return content_;
    }

    /// The tolerance settings, from tolerance.txt (K3): "words per deviation",
    /// "most deviations", "fill <category>" and "singular <word>" with their
    /// values. The Tolerance class reads them.
    [[nodiscard]] const std::vector<std::pair<Bytes, Bytes>>& tolerance() const noexcept {
        return tolerance_;
    }

    /// Reads one rule file. Throws std::runtime_error when the file cannot be
    /// read or a line is not hex bytes.
    [[nodiscard]] static std::vector<Bytes> read(const std::filesystem::path& file);

    /// Reads a rule file of pairs: each line two items as hex bytes with '='
    /// between them.
    [[nodiscard]] static std::vector<std::pair<Bytes, Bytes>>
    read_pairs(const std::filesystem::path& file);

    /// The directory the rules of this language are read from.
    [[nodiscard]] std::filesystem::path directory() const;

private:
    Language language_;
    std::vector<Bytes> categories_;
    std::vector<Bytes> punctuation_;
    std::vector<Bytes> sentence_ends_;
    std::vector<Bytes> closers_;
    std::vector<Bytes> joiners_;
    std::vector<Bytes> number_joiners_;
    std::vector<Bytes> abbreviations_;
    std::vector<Bytes> titles_;
    std::vector<Bytes> question_words_;
    std::vector<Bytes> assumption_words_;
    std::vector<Bytes> expressions_;
    std::vector<Bytes> negation_words_;
    std::vector<std::pair<Bytes, Bytes>> contractions_;
    std::vector<std::pair<Bytes, Bytes>> endings_;
    std::vector<std::pair<Bytes, Bytes>> forms_;
    std::vector<std::pair<Bytes, Bytes>> pronouns_;
    std::vector<std::pair<Bytes, Bytes>> auxiliaries_;
    std::vector<std::pair<Bytes, Bytes>> emotions_;
    std::vector<Bytes> sarcasm_;
    std::vector<Exclusive> exclusives_;
    std::vector<std::pair<Bytes, Bytes>> number_words_;
    std::vector<Bytes> grammar_;
    std::vector<std::pair<Bytes, Bytes>> tolerance_;
    std::vector<std::pair<Bytes, Bytes>> content_;
};

}  // namespace larry
