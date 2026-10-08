#include "larry/lesson.hpp"

#include <algorithm>
#include <format>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace larry {

namespace {

std::string_view trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) {
        text.remove_suffix(1);
    }
    return text;
}

std::vector<Bytes> split_categories(std::string_view line) {
    std::vector<Bytes> out;
    while (true) {
        const std::size_t comma = line.find(',');
        const std::string_view item = trim(line.substr(0, comma));
        out.emplace_back(item.begin(), item.end());
        if (comma == std::string_view::npos) {
            break;
        }
        line.remove_prefix(comma + 1);
    }
    return out;
}

}  // namespace

std::vector<Lesson> read_lessons(const std::filesystem::path& file) {
    std::ifstream in{file, std::ios::binary};
    if (!in) {
        throw std::runtime_error(std::format("Lessons: cannot read {}", file.string()));
    }
    const std::string text{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    return read_lessons_text(text, file.string());
}

std::vector<Lesson> read_lessons_text(std::string_view text, std::string_view name) {
    std::istringstream in{std::string{text}};
    std::vector<Lesson> out;
    std::size_t number = 0;
    Lesson pending;
    bool have_sentence = false;
    const auto bad = [&](std::size_t line, const char* why) {
        throw std::runtime_error(
            std::format("Lessons: {} line {}: {}", name, line, why));
    };
    for (std::string raw; std::getline(in, raw);) {
        ++number;
        const std::string_view line = trim(raw);
        if (line.empty()) {
            if (have_sentence) {
                bad(pending.line, "the sentence has no line of categories after it");
            }
            continue;
        }
        if (line.front() == '#') {
            continue;
        }
        if (!have_sentence) {
            pending = Lesson{.sentence = std::string{line}, .categories = {}, .line = number};
            have_sentence = true;
            continue;
        }
        pending.categories = split_categories(line);
        for (const Bytes& category : pending.categories) {
            if (category.empty()) {
                bad(number, "a category is empty");
            }
        }
        out.push_back(std::move(pending));
        pending = Lesson{};
        have_sentence = false;
    }
    if (have_sentence) {
        bad(pending.line, "the sentence has no line of categories after it");
    }
    return out;
}

std::vector<std::filesystem::path> lesson_files(Language language) {
    const std::filesystem::path dir = std::filesystem::path{LARRY_LESSONS_DIR} / locale(language);
    std::vector<std::filesystem::path> out;
    if (!std::filesystem::is_directory(dir)) {
        return out;
    }
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator{dir}) {
        if (entry.is_regular_file() && entry.path().extension() == ".txt") {
            out.push_back(entry.path());
        }
    }
    std::ranges::sort(out);
    return out;
}

}  // namespace larry
