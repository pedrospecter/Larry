#include "larry/dictionary.hpp"

#include <algorithm>
#include <format>
#include <fstream>
#include <iterator>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace larry {

Dictionary::Dictionary(const std::filesystem::path& file) {
    std::ifstream in{file, std::ios::binary};
    if (!in) {
        throw std::runtime_error(std::format("Dictionary: cannot read {}", file.string()));
    }
    const std::string all{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    std::string_view rest{all};
    std::size_t number = 0;
    while (!rest.empty()) {
        ++number;
        const std::size_t end = rest.find('\n');
        std::string_view line = rest.substr(0, end);
        rest.remove_prefix(end == std::string_view::npos ? rest.size() : end + 1);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        if (line.empty() || line.front() == '#') {
            continue;
        }
        const std::size_t tab = line.find('\t');
        if (tab == std::string_view::npos || tab == 0 || tab + 1 == line.size()) {
            throw std::runtime_error(
                std::format("Dictionary: {} line {} is not a word and its categories",
                            file.string(), number));
        }
        words_.push_back({std::string{line.substr(0, tab)}, std::string{line.substr(tab + 1)}});
    }
    if (!std::ranges::is_sorted(words_, {}, &Entry::word)) {
        std::ranges::sort(words_, {}, &Entry::word);
    }
}

std::filesystem::path Dictionary::file_for(Language language) {
    return std::filesystem::path{LARRY_DICTIONARY_DIR} / locale(language) / "words.txt";
}

const Dictionary::Entry* Dictionary::find(const Bytes& word) const {
    const std::string_view key{reinterpret_cast<const char*>(word.data()), word.size()};
    const auto it = std::ranges::lower_bound(words_, key, {}, &Entry::word);
    if (it == words_.end() || it->word != key) {
        return nullptr;
    }
    return &*it;
}

bool Dictionary::contains(const Bytes& word) const {
    return find(word) != nullptr;
}

std::vector<Bytes> Dictionary::categories(const Bytes& word) const {
    std::vector<Bytes> out;
    const Entry* entry = find(word);
    if (entry == nullptr) {
        return out;
    }
    std::string_view rest{entry->categories};
    while (!rest.empty()) {
        const std::size_t comma = rest.find(',');
        const std::string_view item = rest.substr(0, comma);
        out.emplace_back(item.begin(), item.end());
        rest.remove_prefix(comma == std::string_view::npos ? rest.size() : comma + 1);
    }
    return out;
}

std::vector<Bytes> Dictionary::near(const Bytes& word, std::size_t limit) const {
    static constexpr std::string_view letters = "abcdefghijklmnopqrstuvwxyz'-";
    // The slips in the order they are made: two letters swapped, one doubled
    // or dropped, one changed, one added.
    std::vector<std::string> ranked;
    std::set<std::string> seen;
    const std::string base(word.begin(), word.end());
    const auto try_word = [&](const std::string& candidate) {
        if (candidate == base || ranked.size() >= limit || seen.contains(candidate)) {
            return;
        }
        if (contains(Bytes(candidate.begin(), candidate.end()))) {
            ranked.push_back(candidate);
            seen.insert(candidate);
        }
    };
    for (std::size_t i = 0; i + 1 < base.size(); ++i) {
        std::string swapped = base;
        std::swap(swapped[i], swapped[i + 1]);
        try_word(swapped);
    }
    for (std::size_t i = 0; i < base.size(); ++i) {
        try_word(base.substr(0, i) + base.substr(i + 1));
    }
    for (std::size_t i = 0; i < base.size(); ++i) {
        for (const char c : letters) {
            std::string changed = base;
            changed[i] = c;
            try_word(changed);
        }
    }
    for (std::size_t i = 0; i <= base.size(); ++i) {
        for (const char c : letters) {
            try_word(base.substr(0, i) + c + base.substr(i));
        }
    }
    std::vector<Bytes> out;
    for (const std::string& candidate : ranked) {
        out.emplace_back(candidate.begin(), candidate.end());
    }
    return out;
}

}  // namespace larry
