#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace larry {

/// One thing a source file says, as a Larry sentence, and the line it came from.
struct CodeFact {
    std::string sentence;  ///< "read_file is a function in sample.py."
    int line = 0;
};

/// Code as sentences (the first step of understanding code): the definitions,
/// the uses and the calls of a source file become Larry sentences, by the
/// keyword rules of base_rules/code/<language>.txt, which are data: the
/// shape of a line that defines a function or a class, with '*' for the
/// name, the shape of a line that uses a module, the words that look like
/// calls but are not, the comment mark, and how a body is marked (by
/// indentation or by braces). A language with no "function" shape takes a
/// function to be a line with a type, a name and '(' that opens a body.
/// The sentences: "F is a function in file.", "C is a class in file.",
/// "M is a method of C.", "file uses module.", "F calls G.".
class Code {
public:
    /// With no directory, base_rules/code in the repository.
    explicit Code(std::filesystem::path directory = {});

    /// The language of a file by its extension: "python", "cpp",
    /// "javascript", "csharp"; empty when none.
    [[nodiscard]] static std::string language_of(const std::filesystem::path& file);
    /// The languages that have rules.
    [[nodiscard]] std::vector<std::string> languages() const;

    /// The sentences of a file. Throws std::runtime_error when the file
    /// cannot be read or its language has no rules.
    [[nodiscard]] std::vector<CodeFact> read(const std::filesystem::path& file) const;
    /// The sentences of a text in a language, named as `name` in them.
    [[nodiscard]] std::vector<CodeFact> read_text(std::string_view text, std::string_view language,
                                                  std::string_view name) const;

private:
    std::filesystem::path directory_;
};

}  // namespace larry
