#include "larry/base_rules.hpp"

#include "larry/hex.hpp"

#include <algorithm>
#include <cstddef>
#include <format>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace larry {

std::vector<Bytes> BaseRules::read(const std::filesystem::path& file) {
    std::ifstream in{file};
    if (!in) {
        throw std::runtime_error(std::format("BaseRules: cannot read {}", file.string()));
    }
    std::vector<Bytes> out;
    std::size_t number = 0;
    for (std::string line; std::getline(in, line);) {
        ++number;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty() || line.front() == '#') {
            continue;
        }
        std::optional<Bytes> bytes = hex::decode(line);
        if (!bytes) {
            throw std::runtime_error(
                std::format("BaseRules: {} line {} is not hex bytes", file.string(), number));
        }
        out.push_back(std::move(*bytes));
    }
    return out;
}

std::vector<std::pair<Bytes, Bytes>> BaseRules::read_pairs(const std::filesystem::path& file) {
    std::ifstream in{file};
    if (!in) {
        throw std::runtime_error(std::format("BaseRules: cannot read {}", file.string()));
    }
    std::vector<std::pair<Bytes, Bytes>> out;
    std::size_t number = 0;
    for (std::string line; std::getline(in, line);) {
        ++number;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty() || line.front() == '#') {
            continue;
        }
        const std::size_t equals = line.find('=');
        const std::optional<Bytes> first =
            equals == std::string::npos ? std::nullopt
                                        : hex::decode(std::string_view{line}.substr(0, equals));
        const std::optional<Bytes> second =
            equals == std::string::npos ? std::nullopt
                                        : hex::decode(std::string_view{line}.substr(equals + 1));
        if (!first || !second) {
            throw std::runtime_error(std::format(
                "BaseRules: {} line {} is not a pair of hex bytes", file.string(), number));
        }
        out.emplace_back(std::move(*first), std::move(*second));
    }
    return out;
}

std::filesystem::path BaseRules::directory() const {
    return std::filesystem::path{LARRY_BASE_RULES_DIR} / locale(language_);
}

BaseRules::BaseRules(Language language) : language_(language) {
    const std::filesystem::path dir = directory();
    categories_ = read(dir / "categories.txt");
    punctuation_ = read(dir / "punctuation.txt");
    sentence_ends_ = read(dir / "sentence_ends.txt");
    closers_ = read(dir / "closers.txt");
    joiners_ = read(dir / "joiners.txt");
    number_joiners_ = read(dir / "number_joiners.txt");
    abbreviations_ = read(dir / "abbreviations.txt");
    titles_ = read(dir / "titles.txt");
    question_words_ = read(dir / "question_words.txt");
    assumption_words_ = read(dir / "assumption_words.txt");
    expressions_ = read(dir / "expressions.txt");
    negation_words_ = read(dir / "negation_words.txt");
    contractions_ = read_pairs(dir / "contractions.txt");
    endings_ = read_pairs(dir / "endings.txt");
    std::ranges::stable_sort(endings_, [](const auto& a, const auto& b) {
        return a.first.size() > b.first.size();
    });
    forms_ = read_pairs(dir / "forms.txt");
    pronouns_ = read_pairs(dir / "pronouns.txt");
    auxiliaries_ = read_pairs(dir / "auxiliaries.txt");
    emotions_ = read_pairs(dir / "emotions.txt");
    sarcasm_ = read(dir / "sarcasm.txt");
}

}  // namespace larry
