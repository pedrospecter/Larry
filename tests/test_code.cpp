// Tests for Code: source files as Larry sentences, by the keyword rules of
// base_rules/code/<language>.txt.
#include "larry/code.hpp"

#include "check.hpp"

#include <algorithm>
#include <filesystem>
#include <print>
#include <string>
#include <vector>

using larry::Code;
using larry::CodeFact;

namespace {

std::filesystem::path sample(std::string_view name) {
    return std::filesystem::path{LARRY_TEST_DATA_DIR} / "code" / name;
}

std::vector<std::string> sentences_of(const std::vector<CodeFact>& facts) {
    std::vector<std::string> out;
    for (const CodeFact& f : facts) {
        out.push_back(f.sentence);
    }
    return out;
}

bool has(const std::vector<std::string>& sentences, std::string_view wanted) {
    const bool found = std::ranges::contains(sentences, std::string{wanted});
    if (!found) {
        std::println("missing: {}", wanted);
        for (const std::string& s : sentences) {
            std::println("  have: {}", s);
        }
    }
    return found;
}

}  // namespace

TEST(the_languages_have_rules) {
    const Code code;
    CHECK(code.languages() == (std::vector<std::string>{"cpp", "csharp", "javascript", "python"}));
    CHECK(Code::language_of("a.py") == "python");
    CHECK(Code::language_of("a.hpp") == "cpp");
    CHECK(Code::language_of("a.ts") == "javascript");
    CHECK(Code::language_of("a.cs") == "csharp");
    CHECK(Code::language_of("a.txt").empty());
    CHECK_THROWS(code.read("no/such/file.py"), std::runtime_error);
    CHECK_THROWS(code.read(sample("sample.txt")), std::runtime_error);
}

TEST(python_definitions_uses_and_calls) {
    const std::vector<std::string> s = sentences_of(Code{}.read(sample("sample.py")));
    CHECK(has(s, "sample.py uses os."));
    CHECK(has(s, "sample.py uses json."));
    CHECK(has(s, "read_file is a function in sample.py."));
    CHECK(has(s, "read_file calls open."));
    CHECK(has(s, "read_file calls loads."));
    CHECK(has(s, "Reader is a class in sample.py."));
    CHECK(has(s, "read is a method of Reader."));
    CHECK(has(s, "read calls read_file."));
    CHECK(has(s, "count is a method of Reader."));
    CHECK(has(s, "count calls listdir."));
    CHECK(!std::ranges::contains(s, std::string{"read_file calls with."}));
}

TEST(cpp_definitions_uses_and_calls) {
    const std::vector<std::string> s = sentences_of(Code{}.read(sample("sample.cpp")));
    CHECK(has(s, "sample.cpp uses string."));
    CHECK(has(s, "sample.cpp uses larry/json.hpp."));
    CHECK(has(s, "Reader is a class in sample.cpp."));
    CHECK(has(s, "count_words is a function in sample.cpp."));
    CHECK(has(s, "read_file is a function in sample.cpp."));
    CHECK(has(s, "read_file calls to_string."));
    CHECK(has(s, "read_file calls count_words."));
    CHECK(!std::ranges::contains(s, std::string{"count_words calls static_cast."}));
}

TEST(javascript_definitions_uses_and_calls) {
    const std::vector<std::string> s = sentences_of(Code{}.read(sample("sample.js")));
    CHECK(has(s, "sample.js uses fs."));
    CHECK(has(s, "sample.js uses path."));
    CHECK(has(s, "readFile is a function in sample.js."));
    CHECK(has(s, "readFile calls readFileSync."));
    CHECK(has(s, "countWords is a function in sample.js."));
    CHECK(has(s, "Reader is a class in sample.js."));
    CHECK(has(s, "read is a method of Reader."));
    CHECK(has(s, "read calls countWords."));
}

TEST(csharp_definitions_uses_and_calls) {
    const std::vector<std::string> s = sentences_of(Code{}.read(sample("sample.cs")));
    CHECK(has(s, "sample.cs uses System."));
    CHECK(has(s, "sample.cs uses System.IO."));
    CHECK(has(s, "Reader is a class in sample.cs."));
    CHECK(has(s, "Read is a method of Reader."));
    CHECK(has(s, "Read calls ReadAllText."));
    CHECK(has(s, "CountWords is a method of Reader."));
    CHECK(has(s, "CountWords calls Split."));
}

int main() {
    return larry::test::run();
}
