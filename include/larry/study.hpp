#pragma once

#include "larry/base_rules.hpp"
#include "larry/content.hpp"
#include "larry/electron.hpp"

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace larry {

class Brain;

/// A word the content taught, or asked about (W4).
struct WordToLearn {
    std::string word;
    std::string category;  ///< The category used for it, or "?" when none.
    std::string from;      ///< "guessed from context", "the dictionary", "unknown", "open: noun, verb".
    std::size_t uses = 0;
};

/// What a study found and did.
struct StudyReport {
    std::string title;
    std::string source;
    ContentTally classes;
    std::size_t proposed = 0;       ///< Facts stored as new proposed conceptions.
    std::size_t known = 0;          ///< Facts that were conceptions already.
    std::size_t read_as_meant = 0;  ///< Facts read within the tolerance, not as said.
    std::size_t unusual = 0;        ///< Facts with a relation never seen before.
    std::vector<WordToLearn> words;
    std::vector<std::string> facts;  ///< The facts, as said.
    std::filesystem::path draft;     ///< The lesson draft written for the user.

    /// The report as lines of text.
    [[nodiscard]] std::vector<std::string> lines() const;
};

/// Study (W4): content becomes proposed conceptions, words to learn and a
/// lesson draft for the user to correct and teach. Each sentence is
/// classified (W2); the facts are described, read within the tolerance,
/// judged by the harness and proposed as conceptions with the source
/// "study:<name>"; the words memory did not teach are gathered with the
/// category used for them; and the draft, in the lesson format, goes to
/// lessons/<locale>/drafts/<name>.txt, with the words to check noted above
/// each sentence. This is how content teaches language: the user corrects
/// the draft and teaches it (larry teach), and the corrected lesson
/// becomes words with categories, patterns, and conceptions to validate.
class Study {
public:
    Study(const BaseRules& rules, const Content& content);

    /// Studies a text under a name, from a source (a URL, a file), with the
    /// brain that holds the memory, and writes the draft into the directory.
    [[nodiscard]] StudyReport study(std::string_view text, std::string_view name, std::string_view source,
                                    Brain& brain, const std::filesystem::path& drafts) const;

    /// Where drafts go: lessons/<locale>/drafts, or LARRY_DRAFTS_DIR.
    [[nodiscard]] static std::filesystem::path drafts_directory(Language language);

private:
    const BaseRules* rules_;
    const Content* content_;
};

}  // namespace larry
