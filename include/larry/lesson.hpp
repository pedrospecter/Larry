#pragma once

#include "larry/constellation.hpp"
#include "larry/electron.hpp"

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace larry {

/// One lesson: a sentence and the category of each of its entities (A2).
struct Lesson {
    std::string sentence;
    std::vector<Bytes> categories;
    std::size_t line;  ///< The line of the sentence in its file.
};

/// Reads a lesson file (F7, Q9): plain UTF-8, one lesson per block, each block
/// a sentence line followed by a line with its categories separated by commas.
/// Blank lines separate blocks and lines that start with '#' are comments.
/// Throws std::runtime_error with the line number when the file is not in
/// this form.
[[nodiscard]] std::vector<Lesson> read_lessons(const std::filesystem::path& file);

/// The lesson files of a language in lessons/<locale>/, in name order: the
/// fixed order a rebuild assimilates them in.
[[nodiscard]] std::vector<std::filesystem::path> lesson_files(Language language);

}  // namespace larry
