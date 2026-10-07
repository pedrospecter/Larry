// Tests for Dictionary: the English word list on the machine.

#include "larry/dictionary.hpp"

#include "check.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <print>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using larry::Bytes;
using larry::Dictionary;

namespace {

Bytes b(std::string_view text) {
    return Bytes(text.begin(), text.end());
}

bool has(const std::vector<Bytes>& list, std::string_view item) {
    return std::ranges::contains(list, b(item));
}

const Dictionary& english() {
    static const Dictionary instance{Dictionary::file_for(larry::Language::English)};
    return instance;
}

std::filesystem::path write(std::string_view name, std::string_view content) {
    const std::filesystem::path file = std::filesystem::temp_directory_path() / name;
    std::ofstream out{file, std::ios::binary};
    out << content;
    return file;
}

}  // namespace

TEST(the_english_dictionary_loads_fast_and_is_large) {
    const auto start = std::chrono::steady_clock::now();
    const Dictionary dictionary{Dictionary::file_for(larry::Language::English)};
    const auto took = std::chrono::steady_clock::now() - start;
    std::println("dictionary: {} words in {} ms", dictionary.size(),
                 std::chrono::duration_cast<std::chrono::milliseconds>(took).count());
    CHECK(dictionary.size() > 150000);
    CHECK(took < std::chrono::seconds{2});
}

TEST(words_and_their_categories) {
    const Dictionary& d = english();
    CHECK(d.contains(b("sky")));
    CHECK(d.contains(b("azure")));
    CHECK(!d.contains(b("Sky")));  // keys are in lower case
    CHECK(!d.contains(b("zorp")));
    CHECK(!d.contains(b("")));
    CHECK(has(d.categories(b("sky")), "noun"));
    CHECK(has(d.categories(b("azure")), "adjective"));
    CHECK(has(d.categories(b("azure")), "noun"));
    CHECK(d.categories(b("oh")) == std::vector<Bytes>{b("interjection")});
    CHECK(has(d.categories(b("the")), "determiner"));
    CHECK(has(d.categories(b("is")), "auxiliary verb"));
    CHECK(has(d.categories(b("can")), "auxiliary verb"));
    CHECK(has(d.categories(b("london")), "proper noun"));
    CHECK(has(d.categories(b("quickly")), "adverb"));
    CHECK(has(d.categories(b("and")), "conjunction"));
    CHECK(has(d.categories(b("under")), "preposition"));
    CHECK(d.categories(b("zorp")).empty());
    // Every category is one of Larry's 13.
    const std::vector<std::string_view> larrys = {
        "noun", "pronoun", "verb", "adjective", "adverb", "preposition", "conjunction",
        "determiner", "interjection", "numeral", "auxiliary verb", "particle", "proper noun"};
    for (const std::string_view word : {"sky", "run", "a", "three", "new", "york"}) {
        for (const Bytes& category : d.categories(b(word))) {
            CHECK(std::ranges::contains(larrys, std::string_view{reinterpret_cast<const char*>(category.data()), category.size()}));
        }
    }
}

TEST(near_words_one_slip_away) {
    const Dictionary& d = english();
    CHECK(has(d.near(b("skyy")), "sky"));
    CHECK(has(d.near(b("teh")), "the"));  // "eth" and "the": swaps come first
    CHECK(has(d.near(b("azur")), "azure"));
    CHECK(has(d.near(b("blue")), "blur"));  // a known word has near words too
    CHECK(!has(d.near(b("blue")), "blue"));
    CHECK(d.near(b("zqxjkv")).empty());
    CHECK(d.near(b("skyy"), 1).size() == 1);
    CHECK(d.near(b("skyy")).size() <= 20);
    CHECK(d.near(b("cat"), 3).size() == 3);
}

TEST(a_small_dictionary_file) {
    const std::filesystem::path file = write("larry_dictionary_small.txt", "# words\nsky\tnoun,verb\nblue\tadjective\r\n\nazure\tnoun,adjective\n");
    const Dictionary d{file};
    CHECK(d.size() == 3);
    CHECK(d.categories(b("sky")) == (std::vector<Bytes>{b("noun"), b("verb")}));
    CHECK(d.categories(b("blue")) == std::vector<Bytes>{b("adjective")});
    CHECK(d.near(b("sk")) == std::vector<Bytes>{b("sky")});
    CHECK_THROWS(Dictionary{write("larry_dictionary_bad.txt", "sky\n")}, std::runtime_error);
    CHECK_THROWS(Dictionary{write("larry_dictionary_bad2.txt", "\tnoun\n")}, std::runtime_error);
    CHECK_THROWS(Dictionary{std::filesystem::temp_directory_path() / "larry_no_dictionary.txt"},
                 std::runtime_error);
    CHECK(Dictionary::file_for(larry::Language::English).filename() == "words.txt");
}

int main() {
    return larry::test::run();
}
