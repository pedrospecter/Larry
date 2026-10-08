// Tests for the measurement on a treebank (A6): reading CoNLL-U, aligning
// Larry's entities to its tokens, and the curve on a tiny set.

#include "larry/measure.hpp"

#include "larry/assimilation.hpp"
#include "larry/base_rules.hpp"
#include "larry/memory.hpp"

#include "check.hpp"

#include <filesystem>
#include <print>
#include <string>
#include <string_view>
#include <vector>

using larry::Bytes;
using larry::Score;
using larry::UdSentence;

namespace {

const larry::BaseRules& rules() {
    static const larry::BaseRules instance{larry::Language::English};
    return instance;
}

Bytes b(std::string_view text) {
    return Bytes(text.begin(), text.end());
}

std::filesystem::path tiny() {
    return std::filesystem::path{LARRY_TEST_DATA_DIR} / "ud" / "tiny.conllu";
}

}  // namespace

TEST(conllu_is_read_with_text_and_tokens) {
    const std::vector<UdSentence> sentences = larry::read_conllu(tiny());
    CHECK(sentences.size() == 5);
    if (sentences.size() == 5) {
        CHECK(sentences[0].text == "The sky is blue.");
        CHECK(sentences[0].tokens.size() == 5);
        CHECK(sentences[0].tokens[1].form == "sky" && sentences[0].tokens[1].upos == "NOUN");
        CHECK(sentences[1].tokens.size() == 8);  // the range line "1-2" is skipped
        CHECK(sentences[1].tokens[2].form == "n't" && sentences[1].tokens[2].upos == "PART");
    }
    CHECK_THROWS(larry::read_conllu("/no/such/file.conllu"), std::runtime_error);
    CHECK(larry::category_for("NOUN") == b("noun"));
    CHECK(larry::category_for("PROPN") == b("proper noun"));
    CHECK(larry::category_for("SCONJ") == b("conjunction"));
    CHECK(larry::category_for("PART") == b("particle"));
    CHECK(larry::category_for("PUNCT").empty());
    CHECK(larry::category_for("X").empty());
}

TEST(entities_align_to_token_runs) {
    const larry::Assimilation assimilation{rules()};
    const std::vector<UdSentence> sentences = larry::read_conllu(tiny());
    const std::optional<larry::Aligned> sky = larry::align(sentences[0], assimilation);
    CHECK(sky.has_value());
    if (sky) {
        CHECK(sky->categories == (std::vector<Bytes>{b("determiner"), b("noun"), b("auxiliary verb"), b("adjective")}));
    }
    // "don't" is one entity for Larry and two tokens here: the first token's category; "New York" is one entity.
    const std::optional<larry::Aligned> birds = larry::align(sentences[1], assimilation);
    CHECK(birds.has_value());
    if (birds) {
        CHECK(birds->categories == (std::vector<Bytes>{b("noun"), b("auxiliary verb"), b("verb"), b("preposition"), b("proper noun")}));
    }
    CHECK(larry::align(sentences[3], assimilation)->categories.size() == 4);
    // A symbol has no category: not aligned.
    CHECK(!larry::align(sentences[4], assimilation).has_value());
}

TEST(the_curve_on_the_tiny_set) {
    const std::vector<UdSentence> sentences = larry::read_conllu(tiny());
    const std::filesystem::path scratch = std::filesystem::temp_directory_path() / "larry_test_measure.atoms";
    std::vector<std::string> told;
    const std::vector<Score> curve = larry::measure(rules(), sentences, sentences, {0, 2, 4}, scratch, nullptr,
                                                    [&](std::string_view what) { told.emplace_back(what); });
    CHECK(curve.size() == 3);
    if (curve.size() == 3) {
        // Nothing taught: every word unknown but the irregular forms, which
        // their pairs give (A4): "is" twice and "has".
        CHECK(curve[0].taught == 0);
        CHECK(curve[0].scored == 17);
        CHECK(curve[0].unknown == 14);
        CHECK(curve[0].correct == 3);
        CHECK(curve[0].sentences == 4);
        CHECK(curve[0].skipped == 1);
        // Two sentences taught: their words are right when tested on themselves.
        CHECK(curve[1].taught == 2);
        CHECK(curve[1].taught_words == 9);
        CHECK(curve[1].correct >= 9);
        // Everything taught: everything right.
        CHECK(curve[2].taught == 4);
        CHECK(curve[2].correct == 17);
        CHECK(curve[2].accuracy() == 1.0);
        CHECK(curve[2].text().starts_with("taught 4 sentences (17 words): 100.0% of 17 test words right, 0.0% unknown"));
    }
    CHECK(told.size() == 6);
    CHECK(!std::filesystem::exists(scratch));
}

int main() {
    return larry::test::run();
}
