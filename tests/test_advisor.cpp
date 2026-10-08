// Tests for the Advisor (W5): a language model as a source, on recorded
// answers. The live server is never called here: no key is needed.
#include "larry/advisor.hpp"

#include "check.hpp"

#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>

using larry::Advisor;
using larry::Opinion;

namespace {

std::string answer_with(std::string_view text, std::string_view stop = "end_turn") {
    std::string escaped;
    for (const char c : text) {
        if (c == '\n') {
            escaped += "\\n";
        } else if (c == '"') {
            escaped += "\\\"";
        } else {
            escaped.push_back(c);
        }
    }
    return "{\"id\":\"msg_1\",\"type\":\"message\",\"role\":\"assistant\",\"model\":\"test-model\","
           "\"content\":[{\"type\":\"thinking\",\"thinking\":\"...\"},{\"type\":\"text\",\"text\":\"" +
           escaped + "\"}],\"stop_reason\":\"" + std::string{stop} + "\",\"usage\":{\"input_tokens\":10,\"output_tokens\":5}}";
}

}  // namespace

TEST(the_request_body_names_the_model_and_quotes_the_text) {
    const std::string body = Advisor::request_body("test-model", "sys", "Say \"hi\"\nnow", 99);
    CHECK(body.find("\"model\":\"test-model\"") != std::string::npos);
    CHECK(body.find("\"max_tokens\":99") != std::string::npos);
    CHECK(body.find("\"fallbacks\":\"default\"") != std::string::npos);
    CHECK(body.find("\"thinking\":{\"type\":\"adaptive\"}") != std::string::npos);
    CHECK(body.find("\"content\":\"Say \\\"hi\\\"\\nnow\"") != std::string::npos);
    CHECK(body.find("\"role\":\"user\"") != std::string::npos);
}

TEST(the_answer_is_read_from_the_text_blocks) {
    bool refused = true;
    CHECK(Advisor::read_answer(answer_with("The sky is blue."), &refused) == "The sky is blue.");
    CHECK(!refused);
    CHECK(Advisor::read_answer(answer_with("No.", "refusal"), &refused) == "No.");
    CHECK(refused);
    CHECK_THROWS(Advisor::read_answer("{\"type\":\"error\",\"error\":{\"type\":\"invalid_request_error\",\"message\":\"bad\"}}"),
                 std::runtime_error);
    CHECK_THROWS(Advisor::read_answer("not json"), std::runtime_error);
}

TEST(the_model_proposes_facts_gives_opinions_and_answers) {
    std::vector<std::string> bodies;
    std::string next;
    const Advisor::Transport transport = [&](const std::string& body) {
        bodies.push_back(body);
        return next;
    };
    const Advisor advisor{"test-model", transport};
    CHECK(advisor.model() == "test-model");
    CHECK(advisor.source() == "llm:test-model");
    // Facts, one per line, with list marks and numbers dropped and full stops added.
    next = answer_with("1. The sky is blue\n- The sea is wide.\n\nBirds fly.");
    const std::vector<std::string> facts = advisor.propose("Some text about the sky, the sea and birds.", 10);
    CHECK(facts == (std::vector<std::string>{"The sky is blue.", "The sea is wide.", "Birds fly."}));
    CHECK(bodies.size() == 1);
    CHECK(bodies.front().find("Some text about the sky") != std::string::npos);
    CHECK(bodies.front().find("at most 10") != std::string::npos);
    // The limit holds.
    next = answer_with("A.\nB.\nC.");
    CHECK(advisor.propose("text", 2).size() == 2);
    // An opinion: the verdict on the first line, the reason after.
    next = answer_with("True\nThe sky scatters blue light.");
    const Opinion yes = advisor.opinion("The sky is blue.");
    CHECK(yes.verdict == "true");
    CHECK(yes.reason == "The sky scatters blue light.");
    CHECK(yes.model == "test-model");
    CHECK(!yes.refused);
    next = answer_with("false: penguins cannot fly.");
    const Opinion no = advisor.opinion("Penguins fly.");
    CHECK(no.verdict == "false");
    CHECK(no.reason == "penguins cannot fly.");
    next = answer_with("I would rather not say.");
    CHECK(advisor.opinion("Something.").verdict == "unknown");
    // A refusal is no opinion and no fact.
    next = answer_with("", "refusal");
    CHECK(advisor.opinion("Something.").refused);
    CHECK(advisor.propose("text").empty());
    CHECK(advisor.ask("What?").empty());
    // An answer.
    next = answer_with("  The sky is blue.  ");
    CHECK(advisor.ask("What colour is the sky?") == "The sky is blue.");
    // An error from the server is thrown, with its message.
    next = "{\"type\":\"error\",\"error\":{\"type\":\"authentication_error\",\"message\":\"invalid x-api-key\"}}";
    CHECK_THROWS(advisor.ask("What?"), std::runtime_error);
}

TEST(the_key_is_read_from_the_environment_and_never_shown) {
    // The tests run without a key: the live transport refuses before curl.
    if (std::getenv("ANTHROPIC_API_KEY") == nullptr && std::getenv("ANTHROPIC_AUTH_TOKEN") == nullptr) {
        CHECK(!Advisor::key_in_environment());
        CHECK_THROWS(Advisor::post("{}"), std::runtime_error);
    } else {
        CHECK(Advisor::key_in_environment());
    }
    CHECK(Advisor::default_model() == (std::getenv("LARRY_LLM_MODEL") != nullptr ? std::string{std::getenv("LARRY_LLM_MODEL")}
                                                                                 : std::string{"claude-opus-5-5"}));
    CHECK(Advisor{}.source().starts_with("llm:"));
}

int main() {
    return larry::test::run();
}
