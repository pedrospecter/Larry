// Tests for BaseRules: the hex file reader and the English rule files.

#include "larry/base_rules.hpp"

#include "check.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

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

TEST(reads_pairs) {
    const std::filesystem::path file =
        write("larry_rules_pairs.txt", "# a=b\n61=6220632064\n\n6e6f=\n");
    const auto pairs = BaseRules::read_pairs(file);
    CHECK(pairs.size() == 2);
    CHECK(pairs.size() == 2 && pairs[0].first == b("a") && pairs[0].second == b("b c d"));
    CHECK(pairs.size() == 2 && pairs[1].first == b("no") && pairs[1].second.empty());
    CHECK_THROWS(BaseRules::read_pairs(write("larry_rules_pairs_bad.txt", "6162\n")), std::runtime_error);
    CHECK_THROWS(BaseRules::read_pairs(write("larry_rules_pairs_bad2.txt", "6g=61\n")), std::runtime_error);
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
    CHECK(has(rules.negation_words(), "not"));
    CHECK(has(rules.negation_words(), "never"));
    CHECK(!rules.contractions().empty());
    bool isnt = false;
    for (const auto& [contraction, expansion] : rules.contractions()) {
        CHECK(!contraction.empty() && !expansion.empty());
        isnt = isnt || (contraction == b("isn't") && expansion == b("is not"));
    }
    CHECK(isnt);
    CHECK(!rules.endings().empty());
    for (std::size_t i = 1; i < rules.endings().size(); ++i) {
        CHECK(rules.endings()[i - 1].first.size() >= rules.endings()[i].first.size());
    }
    CHECK(!rules.forms().empty() && !rules.pronouns().empty() && !rules.auxiliaries().empty());
    CHECK(!rules.emotions().empty());
    CHECK(has(rules.sarcasm(), "yeah right"));
    bool went = false;
    for (const auto& [word, value] : rules.forms()) {
        went = went || (word == b("went") && value == b("verb:past"));
    }
    CHECK(went);
    CHECK(!rules.exclusives().empty());
    bool colour = false;
    for (const auto& group : rules.exclusives()) {
        CHECK(group.words.size() >= 2);
        colour = colour || (group.name == b("colour") && has(group.words, "blue") && has(group.words, "green"));
    }
    CHECK(colour);
    bool three = false;
    for (const auto& [word, digits] : rules.number_words()) {
        three = three || (word == b("three") && digits == b("3"));
    }
    CHECK(three);
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
