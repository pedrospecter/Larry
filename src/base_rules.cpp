#include "larry/base_rules.hpp"

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

namespace larry {

namespace {

std::string_view locale(Language language) {
    switch (language) {
    case Language::English:
        return "en";
    }
    throw std::logic_error("BaseRules: the language has no locale");
}

std::vector<Bytes> read(const std::filesystem::path& file) {
    std::ifstream in{file};
    if (!in) {
        throw std::runtime_error(std::format("BaseRules: cannot read {}", file.string()));
    }
    std::vector<Bytes> out;
    std::size_t number = 0;
    for (std::string line; std::getline(in, line);) {
        ++number;
        if (line.empty()) {
            continue;
        }
        Bytes bytes;
        bytes.reserve(line.size() / 2);
        bool valid = line.size() % 2 == 0;
        for (std::size_t i = 0; valid && i < line.size(); i += 2) {
            const char* const first = line.data() + i;
            std::uint8_t byte = 0;
            const std::from_chars_result parsed = std::from_chars(first, first + 2, byte, 16);
            valid = parsed.ec == std::errc{} && parsed.ptr == first + 2;
            bytes.push_back(byte);
        }
        if (!valid) {
            throw std::runtime_error(
                std::format("BaseRules: {} line {} is not hex bytes", file.string(), number));
        }
        out.push_back(std::move(bytes));
    }
    return out;
}

}  // namespace

BaseRules::BaseRules(Language language)
    : categories_(read(std::filesystem::path{LARRY_BASE_RULES_DIR} / locale(language) /
                       "categories.txt")) {}

}  // namespace larry
