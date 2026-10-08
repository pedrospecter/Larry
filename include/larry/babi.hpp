#pragma once

#include "larry/base_rules.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace larry {

/// One line of a bAbI story: a statement, or a question with its answer and
/// the lines that support it.
struct BabiLine {
    std::string text;
    bool question = false;
    std::string answer;             ///< "bathroom", "yes", "milk,football".
    std::vector<int> supporting;    ///< Line numbers, from 1.
};
struct BabiStory {
    std::vector<BabiLine> lines;
};

/// Reads the stories of a page of the rows API (scripts/babi.sh): the JSON
/// with "rows", each a "story" with "text", "type" (0 context, 1 question),
/// "answer" and "supporting_ids". Throws std::runtime_error when the file
/// cannot be read or is not such a page.
[[nodiscard]] std::vector<BabiStory> read_babi(const std::filesystem::path& file);

/// Reads the stories in the original form of the tasks (tasks_1-20_v1-2,
/// "qa1_single-supporting-fact_test.txt"): a line number, the text, and for
/// a question a tab, the answer, a tab and the supporting line numbers; a
/// story starts again at line 1. Throws std::runtime_error when the file
/// cannot be read.
[[nodiscard]] std::vector<BabiStory> read_babi_text(const std::filesystem::path& file);

/// Whether a reply answers a bAbI question: the answer is the reply's last
/// word (without punctuation, in any case), or "Yes."/"No." for yes and
/// no, or, for a list ("milk,football"), every item is a word of the reply.
[[nodiscard]] bool babi_right(std::string_view reply, std::string_view answer);

/// What a task scored.
struct BabiResult {
    int task = 0;
    std::int64_t stories = 0;
    std::int64_t asked = 0;
    std::int64_t right = 0;
    std::vector<std::string> misses;  ///< The first ten: the question, the answer, the reply.
    double seconds = 0;
    [[nodiscard]] double accuracy() const noexcept {
        return asked == 0 ? 0 : static_cast<double>(right) / static_cast<double>(asked);
    }
    /// "task 1: 97.5% of 1000 questions right in 200 stories, in 12.3 s".
    [[nodiscard]] std::string text() const;
};

/// Runs a task: each story in a fresh scratch memory as one molecule, its
/// statements heard, its questions asked (nothing stored) and judged. The
/// scratch file is removed. `progress` is told every fifty stories.
[[nodiscard]] BabiResult run_babi(const BaseRules& rules, int task, const std::vector<BabiStory>& stories,
                                  const std::filesystem::path& scratch,
                                  const std::function<void(std::string_view)>& progress = {});

}  // namespace larry
