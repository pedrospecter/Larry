// Tests for the bAbI runner: reading a page of stories, judging a reply, and
// a tiny task with the states of R3.

#include "larry/babi.hpp"

#include "larry/base_rules.hpp"
#include "larry/json.hpp"

#include "check.hpp"

#include <filesystem>
#include <print>
#include <string>
#include <string_view>
#include <vector>

using larry::BabiResult;
using larry::BabiStory;

namespace {

const larry::BaseRules& rules() {
    static const larry::BaseRules instance{larry::Language::English};
    return instance;
}

std::filesystem::path tiny() {
    return std::filesystem::path{LARRY_TEST_DATA_DIR} / "babi" / "tiny.json";
}

}  // namespace

TEST(json_values_are_read) {
    const larry::Json j = larry::parse_json(R"({"a": [1, 2.5, "x\u00e9\n"], "b": {"c": true, "d": null}, "e": -3})");
    CHECK(j.get("a") != nullptr && j.get("a")->array() != nullptr && j.get("a")->array()->size() == 3);
    CHECK(*(*j.get("a")->array())[1].number() == 2.5);
    CHECK(*(*j.get("a")->array())[2].string() == "x\xc3\xa9\n");
    CHECK(*j.get("b")->get("c")->boolean() == true);
    CHECK(j.get("b")->get("d")->string() == nullptr);
    CHECK(*j.get("e")->number() == -3);
    CHECK(j.get("nope") == nullptr);
    CHECK_THROWS(larry::parse_json("{\"a\": }", "test"), std::runtime_error);
    CHECK_THROWS(larry::parse_json("[1] x"), std::runtime_error);
}

TEST(a_page_of_stories_is_read) {
    const std::vector<BabiStory> stories = larry::read_babi(tiny());
    CHECK(stories.size() == 2);
    if (stories.size() == 2) {
        CHECK(stories[0].lines.size() == 6);
        CHECK(stories[0].lines[0].text == "Mary moved to the bathroom.");
        CHECK(!stories[0].lines[0].question);
        CHECK(stories[0].lines[2].question);
        CHECK(stories[0].lines[2].answer == "bathroom");
        CHECK(stories[0].lines[2].supporting == std::vector<int>{1});
        CHECK(stories[1].lines[2].answer == "kitchen");
    }
    CHECK_THROWS(larry::read_babi("/no/such/page.json"), std::runtime_error);
}

TEST(the_original_text_form_is_read_too) {
    const std::vector<BabiStory> stories =
        larry::read_babi_text(std::filesystem::path{LARRY_TEST_DATA_DIR} / "babi" / "tiny_test.txt");
    CHECK(stories.size() == 2);
    if (stories.size() == 2) {
        CHECK(stories[0].lines.size() == 6);
        CHECK(stories[0].lines[2].question);
        CHECK(stories[0].lines[2].text == "Where is Mary? ");
        CHECK(stories[0].lines[2].answer == "bathroom");
        CHECK(stories[0].lines[2].supporting == std::vector<int>{1});
        CHECK(!stories[0].lines[3].question);
        CHECK(stories[1].lines.size() == 3);
        CHECK(stories[1].lines[2].answer == "kitchen");
    }
    CHECK_THROWS(larry::read_babi_text("/no/such/file.txt"), std::runtime_error);
    // The same stories as the JSON page: the same result.
    const std::filesystem::path scratch = std::filesystem::temp_directory_path() / "larry_test_babi_text.atoms";
    CHECK(larry::run_babi(rules(), 1, stories, scratch).right == 3);
}

TEST(a_reply_is_judged_by_its_last_word_or_yes_no) {
    CHECK(larry::babi_right("Mary is in the bathroom.", "bathroom"));
    CHECK(larry::babi_right("Mary is in the Bathroom", "bathroom"));
    CHECK(!larry::babi_right("Mary moved to the bathroom. Mary went to the kitchen.", "bathroom"));
    CHECK(!larry::babi_right("I don't know.", "bathroom"));
    CHECK(larry::babi_right("Yes.", "yes"));
    CHECK(!larry::babi_right("Yes.", "no"));
    CHECK(larry::babi_right("No, it is not.", "no"));
    CHECK(larry::babi_right("Mary has the milk and the football.", "milk,football"));
    CHECK(!larry::babi_right("Mary has the milk.", "milk,football"));
}

TEST(the_tiny_task_is_answered_by_the_latest_state) {
    const std::vector<BabiStory> stories = larry::read_babi(tiny());
    const std::filesystem::path scratch = std::filesystem::temp_directory_path() / "larry_test_babi.atoms";
    const BabiResult result = larry::run_babi(rules(), 1, stories, scratch);
    CHECK(result.task == 1);
    CHECK(result.stories == 2);
    CHECK(result.asked == 3);
    for (const std::string& miss : result.misses) {
        std::println("  miss: {}", miss);
    }
    CHECK(result.right == 3);
    CHECK(result.accuracy() == 1.0);
    CHECK(result.text().starts_with("task 1: 100.0% of 3 questions right in 2 stories"));
    CHECK(!std::filesystem::exists(scratch));
}

int main() {
    return larry::test::run();
}
