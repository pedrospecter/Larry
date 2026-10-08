#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace larry {

/// What a model thinks of a claim (W5): true, false or unknown, and why.
struct Opinion {
    std::string verdict;  ///< "true", "false" or "unknown"; empty when the model declined.
    std::string reason;
    std::string model;
    bool refused = false;
};

/// W5: a language model as a source. It proposes facts from a text, gives a
/// second opinion on a claim and answers a question; what it says is a
/// proposal from the source "llm:<model>", never a validation: only the user
/// validates (R2b, Q24). The transport is the curl command on the machine
/// with the key from the environment (ANTHROPIC_API_KEY, or
/// ANTHROPIC_AUTH_TOKEN), which is never printed, or a function the tests
/// give with recorded answers. The model is LARRY_LLM_MODEL, else
/// claude-opus-5-5.
class Advisor {
public:
    /// A function from the JSON body of a request to the JSON body of the
    /// answer; throws on failure.
    using Transport = std::function<std::string(const std::string& body)>;

    explicit Advisor(std::string model = {}, Transport transport = {});

    [[nodiscard]] static std::string default_model();
    [[nodiscard]] const std::string& model() const noexcept { return model_; }
    /// "llm:<model>": the source of what the model proposes.
    [[nodiscard]] std::string source() const;
    /// Whether a key is in the environment (ANTHROPIC_API_KEY or ANTHROPIC_AUTH_TOKEN).
    [[nodiscard]] static bool key_in_environment();

    /// The JSON body of a request: the model, the budget, the system text
    /// and one user message, with the server's fallbacks on.
    [[nodiscard]] static std::string request_body(std::string_view model, std::string_view system,
                                                  std::string_view user, int max_tokens);
    /// The text the model answered, from the JSON body of the answer: its
    /// text blocks joined; `refused` is set when the model declined. Throws
    /// std::runtime_error on an error answer, with its message.
    [[nodiscard]] static std::string read_answer(std::string_view body, bool* refused = nullptr);

    /// The facts a text states, as short sentences, at most `limit`.
    [[nodiscard]] std::vector<std::string> propose(std::string_view text, std::size_t limit = 20) const;
    /// A second opinion on a claim.
    [[nodiscard]] Opinion opinion(std::string_view claim) const;
    /// An answer to a question, in one sentence; empty when the model declined.
    [[nodiscard]] std::string ask(std::string_view question) const;

    /// Runs curl against the Messages API with the key from the environment
    /// in a header file, never on the command line, and gives the answer's
    /// body. Throws std::runtime_error when there is no key, curl is not
    /// there, or the server answers with an error.
    [[nodiscard]] static std::string post(const std::string& body);

private:
    std::string model_;
    Transport transport_;
};

}  // namespace larry
