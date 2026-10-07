#include "larry/base_rules.hpp"

#include "larry/hex.hpp"

#include <cstddef>
#include <format>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>

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
}

}  // namespace larry
