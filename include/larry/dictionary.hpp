#pragma once

#include "larry/constellation.hpp"
#include "larry/electron.hpp"

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace larry {

/// The dictionary: the words of a language with their categories, always on
/// the machine (A2b). It is a fallback for a word no conception has taught:
/// one category is taken as the word's, several are candidates for the
/// context to choose from. It also finds the words one typing slip away from
/// an unknown word, as a spell checker does. The English one is built from
/// the Moby Part-of-Speech list by scripts/dictionary.sh: dictionary/en/words.txt,
/// one word per line, a tab, the categories separated by commas, sorted.
class Dictionary {
public:
    /// Reads the file. Throws std::runtime_error when it cannot be read.
    explicit Dictionary(const std::filesystem::path& file);

    /// The file of a language: dictionary/<locale>/words.txt in the repository.
    [[nodiscard]] static std::filesystem::path file_for(Language language);

    [[nodiscard]] std::size_t size() const noexcept { return words_.size(); }

    /// Whether the word (as the index keys it, in lower case) is there.
    [[nodiscard]] bool contains(const Bytes& word) const;

    /// The categories of a word, in the order of the file, or none.
    [[nodiscard]] std::vector<Bytes> categories(const Bytes& word) const;

    /// The words one slip away from this one, in the order slips are made:
    /// two letters swapped, one dropped, one changed, one added. At most
    /// `limit`.
    [[nodiscard]] std::vector<Bytes> near(const Bytes& word, std::size_t limit = 20) const;

private:
    struct Entry {
        std::string word;
        std::string categories;
    };
    [[nodiscard]] const Entry* find(const Bytes& word) const;

    std::vector<Entry> words_;  ///< Sorted by word.
};

}  // namespace larry
