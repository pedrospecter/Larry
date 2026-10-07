// Tests for BaseRules: the hex file reader and the English rule files.

#include "larry/base_rules.hpp"

#include "check.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>

using larry::BaseRules;
using larry::Bytes;

namespace {

Bytes b(std::string_view text) {
    return Bytes(text.begin(), text.end());
}

bool has(const std::vector<Bytes>& list, std::string_view item) {
    return std::ranges::contains(list, b(item));
}

std::filesystem::path write(std::string_view name, std::string_view content) {
    const std::filesystem::path file = std::filesystem::temp_directory_path() / name;
    std::ofstream out{file, std::ios::binary};
    out << content;
    return file;
}

}  // namespace

TEST(reads_hex_lines) {
    const std::filesystem::path file = write("larry_rules_ok.txt", "6e6f756e\n\n# comment\n76657262\r\n00ff\n");
    const std::vector<Bytes> items = BaseRules::read(file);
    CHECK(items.size() == 3);
    CHECK(items[0] == b("noun"));
    CHECK(items[1] == b("verb"));
    CHECK((items[2] == Bytes{0x00, 0xFF}));
}

TEST(rejects_odd_length) {
    const std::filesystem::path file = write("larry_rules_odd.txt", "6e6f75\n6e6f756\n");
    CHECK_THROWS(BaseRules::read(file), std::runtime_error);
}

TEST(rejects_characters_that_are_not_hex) {
    const std::filesystem::path file = write("larry_rules_nonhex.txt", "6e6f756e\nzz6f\n");
    CHECK_THROWS(BaseRules::read(file), std::runtime_error);
    const std::filesystem::path spaced = write("larry_rules_spaced.txt", "6e 6f\n");
    CHECK_THROWS(BaseRules::read(spaced), std::runtime_error);
}

TEST(rejects_a_missing_file) {
    CHECK_THROWS(BaseRules::read(std::filesystem::temp_directory_path() / "larry_no_such_file.txt"),
                 std::runtime_error);
}

TEST(english_rules_load) {
    const BaseRules rules{larry::Language::English};
    CHECK(rules.language() == larry::Language::English);
    CHECK(rules.directory().filename() == "en");
    CHECK(rules.categories().size() == 13);
    CHECK(has(rules.categories(), "noun"));
    CHECK(has(rules.categories(), "auxiliary verb"));
    CHECK(has(rules.categories(), "proper noun"));
    CHECK(has(rules.punctuation(), "."));
    CHECK(has(rules.punctuation(), "\xE2\x80\x9C"));  // left double quotation mark
    CHECK(has(rules.sentence_ends(), "?"));
    CHECK(has(rules.closers(), ")"));
    CHECK(has(rules.joiners(), "'"));
    CHECK(has(rules.number_joiners(), ","));
    CHECK(has(rules.abbreviations(), "mr."));
    CHECK(has(rules.titles(), "dr."));
    CHECK(has(rules.question_words(), "what"));
    CHECK(has(rules.assumption_words(), "if"));
    CHECK(has(rules.expressions(), "hello"));
    CHECK(has(rules.expressions(), "thank you"));
    // Every abbreviation ends with its full stop and is in lower case.
    for (const Bytes& item : rules.abbreviations()) {
        CHECK(!item.empty() && item.back() == '.');
        CHECK(std::ranges::none_of(item, [](std::uint8_t c) { return c >= 'A' && c <= 'Z'; }));
    }
    // Every title is an abbreviation.
    for (const Bytes& item : rules.titles()) {
        CHECK(std::ranges::contains(rules.abbreviations(), item));
    }
    // Every sentence end and closer is punctuation.
    for (const Bytes& item : rules.sentence_ends()) {
        CHECK(std::ranges::contains(rules.punctuation(), item));
    }
    for (const Bytes& item : rules.closers()) {
        CHECK(std::ranges::contains(rules.punctuation(), item));
    }
}

int main() {
    return larry::test::run();
}
