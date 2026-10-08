// Tests for Content: what each sentence of content is (W2).

#include "larry/content.hpp"

#include "larry/assimilation.hpp"
#include "larry/atom_operations.hpp"
#include "larry/base_rules.hpp"
#include "larry/brain.hpp"
#include "larry/description.hpp"
#include "larry/grammar.hpp"
#include "larry/memory.hpp"
#include "larry/web.hpp"

#include "check.hpp"

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <optional>
#include <print>
#include <string>
#include <string_view>
#include <vector>

using larry::Bytes;
using larry::Content;
using larry::ContentClass;
using larry::Piece;

namespace {

const larry::BaseRules& rules() {
    static const larry::BaseRules instance{larry::Language::English};
    return instance;
}

larry::Grammar& grammar() {
    static larry::Grammar instance{rules()};
    return instance;
}

// A brain over a memory that knows the words of the suite.
larry::Brain& brain() {
    static larry::Brain* instance = [] {
        const std::filesystem::path file =
            std::filesystem::temp_directory_path() / "larry_test_content.atoms";
        std::filesystem::remove(file);
        auto* m = new larry::Memory{file};
        const larry::Assimilation assimilation{rules(), nullptr, &grammar()};
        const larry::AtomOperations ops;
        const auto teach = [&](std::string_view text, std::vector<std::string_view> categories) {
            std::vector<Bytes> taught;
            for (const std::string_view c : categories) {
                taught.emplace_back(c.begin(), c.end());
            }
            const larry::Description d = assimilation.describe(ops.from_text(text), m, taught);
            m->store(d.atom, d.metadata);
        };
        teach("The sky is blue.", {"determiner", "noun", "auxiliary verb", "adjective"});
        teach("The sea is deep.", {"determiner", "noun", "auxiliary verb", "adjective"});
        teach("The sea is wide.", {"determiner", "noun", "auxiliary verb", "adjective"});
        teach("The road is long.", {"determiner", "noun", "auxiliary verb", "adjective"});
        teach("The river is long.", {"determiner", "noun", "auxiliary verb", "adjective"});
        teach("The old dog is slow.", {"determiner", "adjective", "noun", "auxiliary verb", "adjective"});
        teach("Birds fly.", {"noun", "verb"});
        teach("Water boils at 100 degrees.", {"noun", "verb", "preposition", "numeral", "noun"});
        teach("Mars is a planet.", {"proper noun", "auxiliary verb", "determiner", "noun"});
        teach("The sea is deep and the sky is blue.", {"determiner", "noun", "auxiliary verb", "adjective", "conjunction", "determiner", "noun", "auxiliary verb", "adjective"});
        teach("The street is wet.", {"determiner", "noun", "auxiliary verb", "adjective"});
        teach("It rains.", {"pronoun", "verb"});
        teach("Close the door.", {"verb", "determiner", "noun"});
        teach("Please close the window.", {"interjection", "verb", "determiner", "noun"});
        teach("I like the sea.", {"pronoun", "verb", "determiner", "noun"});
        teach("You are late.", {"pronoun", "auxiliary verb", "adjective"});
        teach("Tom said the sky is blue.", {"proper noun", "verb", "determiner", "noun", "auxiliary verb", "adjective"});
        teach("I think the sea is deep.", {"pronoun", "verb", "determiner", "noun", "auxiliary verb", "adjective"});
        teach("This is the sea.", {"pronoun", "auxiliary verb", "determiner", "noun"});
        teach("However, the sea is deep.", {"adverb", "determiner", "noun", "auxiliary verb", "adjective"});
        teach("There is a door.", {"adverb", "auxiliary verb", "determiner", "noun"});
        teach("What is the sky?", {"pronoun", "auxiliary verb", "determiner", "noun"});
        teach("Hello.", {"interjection"});
        teach("The cat is small.", {"determiner", "noun", "auxiliary verb", "adjective"});
        teach("The window is open.", {"determiner", "noun", "auxiliary verb", "adjective"});
        teach("Fish live in the sea.", {"noun", "verb", "preposition", "determiner", "noun"});
        teach("Whales are mammals.", {"noun", "auxiliary verb", "noun"});
        teach("Many people think so.", {"determiner", "noun", "verb", "adverb"});
        teach("I love the sea.", {"pronoun", "verb", "determiner", "noun"});
        teach("The word comes from Old English.", {"determiner", "noun", "verb", "preposition", "proper noun"});
        teach("It is salty.", {"pronoun", "auxiliary verb", "adjective"});
        teach("The sea is the body of water that covers most of the Earth.",
              {"determiner", "noun", "auxiliary verb", "determiner", "noun", "preposition", "noun", "pronoun", "verb", "determiner", "preposition", "determiner", "proper noun"});
        teach("Birds fly over the sea.", {"noun", "verb", "preposition", "determiner", "noun"});
        return new larry::Brain{rules(), *m, nullptr, nullptr, &grammar()};
    }();
    return *instance;
}

const Content& content() {
    static const Content instance{rules()};
    return instance;
}

Piece classify(std::string_view text) {
    const larry::AtomOperations ops;
    return content().classify(ops.from_text(text), brain());
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) {
        s.remove_suffix(1);
    }
    return s;
}

}  // namespace

TEST(the_markers_come_from_the_base_rules) {
    CHECK(content().least_words() == 2);
    CHECK(content().most_words() == 40);
    CHECK(larry::name(ContentClass::Fact) == "fact");
    CHECK(larry::name(ContentClass::Reference) == "reference");
}

TEST(every_class_names_its_reason) {
    const Piece fact = classify("The sky is blue.");
    CHECK(fact.what == ContentClass::Fact);
    CHECK(fact.reason == "an affirmation the grammar reads as \"statement\"");
    CHECK(fact.description.category.bytes == Bytes(std::string_view{"affirmation"}.begin(), std::string_view{"affirmation"}.end()));
    CHECK(classify("It is blue.").reason == "opens with \"It\"");
    CHECK(classify("Is the sky blue?").what == ContentClass::Question);
    CHECK(classify("Is the sky blue?").reason.starts_with("rule 1"));
    CHECK(classify("Close the door.").reason.starts_with("rule 6"));
    CHECK(classify("I like the sea.").reason == "\"I\" is the first person");
    CHECK(classify("You are late.").reason == "\"You\" is the second person");
    CHECK(classify("Tom said \"the sky is blue\".").reason == "a quotation");
    CHECK(classify("History").reason == "\"history\" by itself");
    CHECK(classify("The sea and the sky").reason == "no end mark on a short line");
    CHECK(classify("== Etymology ==").reason == "a line of = around a title");
    CHECK(classify("Contents").reason == "\"contents\" by itself");
    CHECK(classify("Retrieved 3 May 2024.").reason == "contains \"retrieved\"");
    CHECK(classify("The sky is blue.[1]").reason == "has a citation mark like [1]");
    CHECK(classify("12 + 7 = 19.").reason == "mostly symbols or digits");
    CHECK(classify("Sky.").reason == "1 word: too short");
    CHECK(classify("The sky.").reason == "no verb");
    CHECK(classify("Smith, J.").reason == "2 of 2 words unknown");
    CHECK(classify("Blue the is sky.").reason.starts_with("no pattern fits"));
    CHECK(classify("Sky blue sea deep road wide river long.").reason == "no verb");
    // A fact with a word to learn, and one read within the tolerance.
    const Piece zorp = classify("The zorp is blue.");
    CHECK(zorp.what == ContentClass::Fact);
    CHECK(zorp.reason == "an affirmation the grammar reads as \"statement\", with 1 word to learn");
    const Piece read = classify("Sky are blue.");
    CHECK(read.what == ContentClass::Fact);
    CHECK(read.reason.find("read as \"The sky is blue.\"") != std::string::npos);
}

TEST(the_suite_passes) {
    const std::filesystem::path suite = std::filesystem::path{LARRY_TEST_DATA_DIR} / "en" / "content.txt";
    std::ifstream in{suite, std::ios::binary};
    CHECK(static_cast<bool>(in));
    std::size_t cases = 0;
    std::size_t failed = 0;
    std::size_t number = 0;
    for (std::string line; std::getline(in, line);) {
        ++number;
        const std::string_view text = trim(line);
        if (text.empty() || text.front() == '#') {
            continue;
        }
        const std::size_t separator = text.find(" | ");
        CHECK(separator != std::string_view::npos);
        if (separator == std::string_view::npos) {
            continue;
        }
        const std::string expected{trim(text.substr(0, separator))};
        const std::string sentence{trim(text.substr(separator + 3))};
        ++cases;
        const Piece piece = classify(sentence);
        if (larry::name(piece.what) != expected) {
            ++failed;
            std::println(stderr, "content.txt line {}: \"{}\" expected {}, got {} ({})", number, sentence, expected,
                         larry::name(piece.what), piece.reason);
        }
        CHECK(!piece.reason.empty());
    }
    CHECK(cases >= 30);
    CHECK(failed == 0);
}

TEST(an_article_comes_out_as_facts_headings_and_references) {
    const std::filesystem::path file = std::filesystem::path{LARRY_TEST_DATA_DIR} / "en" / "article.txt";
    const std::optional<larry::Page> page = larry::Web::read_page(file);
    CHECK(page.has_value());
    if (!page) {
        return;
    }
    CHECK(page->title == "Sea");
    larry::ContentTally tally;
    std::vector<std::string> facts;
    const larry::AtomOperations ops;
    for (const Piece& piece : content().classify(page->text, brain())) {
        tally.add(piece.what);
        std::println(stderr, "  {:<11} {}  ({})", larry::name(piece.what), ops.text(piece.sentence), piece.reason);
        if (piece.what == ContentClass::Fact) {
            facts.emplace_back(ops.text(piece.sentence));
        }
    }
    CHECK(tally.total() == 20);
    CHECK(facts == (std::vector<std::string>{"The sea is the body of water that covers most of the Earth.",
                                             "The sea is deep.", "The word comes from Old English.",
                                             "Fish live in the sea.", "Birds fly over the sea.",
                                             "Whales are mammals.", "Many people think so."}));
    CHECK(tally.facts == 7);
    CHECK(tally.context == 1);     // It is salty.
    CHECK(tally.questions == 1);   // Is the sea blue?
    CHECK(tally.speech == 1);      // I love the sea.
    CHECK(tally.headings == 3);    // == Etymology ==, == Life ==, "Ocean Water"
    CHECK(tally.references == 4);  // == See also ==, == References ==, Retrieved ..., wide.[2]
    CHECK(tally.fragments == 3);   // Smith, J. / (2001). / The Sea.
}

int main() {
    return larry::test::run();
}
