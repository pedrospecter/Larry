#pragma once

#include "larry/assimilation.hpp"
#include "larry/base_rules.hpp"
#include "larry/dictionary.hpp"
#include "larry/electron.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace larry {

/// A sentence of a Universal Dependencies treebank (CoNLL-U), as far as
/// Larry reads it: its text and its tokens with their part of speech.
struct UdToken {
    std::string form;
    std::string upos;  ///< "NOUN", "VERB", "PUNCT", ...
};
struct UdSentence {
    std::string text;
    std::vector<UdToken> tokens;
};

/// Reads a CoNLL-U file: the "# text =" line and the token lines of each
/// sentence; ranges ("1-2") and empty nodes ("1.1") are skipped. Throws
/// std::runtime_error when the file cannot be read.
[[nodiscard]] std::vector<UdSentence> read_conllu(const std::filesystem::path& file);

/// Larry's category for a Universal Dependencies part of speech, or empty
/// for PUNCT, SYM and X, which are no entity.
[[nodiscard]] Bytes category_for(std::string_view upos);

/// A sentence aligned: Larry's entities with the category the treebank gives
/// each, or nothing when an entity matches no run of tokens, or its first
/// token has no category.
struct Aligned {
    Sentence atom;
    std::vector<Bytes> categories;  ///< One per entity.
};
[[nodiscard]] std::optional<Aligned> align(const UdSentence& sentence, const Assimilation& assimilation);

/// One point of the curve (A6): Larry taught the first `taught` sentences
/// of the training set, scored on the test set.
struct Score {
    std::int64_t taught = 0;        ///< Sentences taught.
    std::int64_t taught_words = 0;
    std::int64_t scored = 0;        ///< Test words scored (entities aligned).
    std::int64_t correct = 0;
    std::int64_t unknown = 0;       ///< Test words Larry gave no category.
    std::int64_t sentences = 0;     ///< Test sentences aligned.
    std::int64_t skipped = 0;       ///< Test sentences not aligned.
    double seconds = 0;
    [[nodiscard]] double accuracy() const noexcept {
        return scored == 0 ? 0 : static_cast<double>(correct) / static_cast<double>(scored);
    }
    /// "taught 1000 sentences (14212 words): 71.2% of 24852 test words right, 18.3% unknown, in 1.2 s".
    [[nodiscard]] std::string text() const;
};

/// A6: the curve of accuracy against the number of taught sentences. For
/// each count, a scratch memory is taught that many training sentences
/// (categories from the treebank, as lessons are taught) and every test
/// sentence is described from memory alone, or with the dictionary too;
/// a word's category is the one memory gives it, the most used of several,
/// the dictionary's, or the guess (A6a); unknown counts as wrong. The scratch
/// file is removed. `progress` is told each point as it starts.
[[nodiscard]] std::vector<Score> measure(const BaseRules& rules, const std::vector<UdSentence>& train,
                                         const std::vector<UdSentence>& test, const std::vector<std::int64_t>& counts,
                                         const std::filesystem::path& scratch, const Dictionary* dictionary = nullptr,
                                         const std::function<void(std::string_view)>& progress = {});

}  // namespace larry
