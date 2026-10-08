#include "larry/advisor.hpp"

#include "larry/json.hpp"

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <thread>

namespace larry {

namespace {

constexpr std::string_view endpoint = "https://api.anthropic.com/v1/messages";
constexpr std::string_view api_version = "2023-06-01";
constexpr std::string_view fallback_beta = "server-side-fallback-2026-07-01";
constexpr std::string_view oauth_beta = "oauth-2025-04-20";

std::string json_quoted(std::string_view text) {
    std::string out = "\"";
    for (const char ch : text) {
        const auto c = static_cast<unsigned char>(ch);
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (c < 0x20) {
                out += std::format("\\u{:04x}", c);
            } else {
                out.push_back(static_cast<char>(c));
            }
        }
    }
    out += "\"";
    return out;
}

std::string trimmed(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r' || text.front() == '\n')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r' || text.back() == '\n')) {
        text.remove_suffix(1);
    }
    return std::string{text};
}

std::vector<std::string> lines_of(std::string_view text) {
    std::vector<std::string> out;
    std::size_t from = 0;
    while (from <= text.size()) {
        const std::size_t at = text.find('\n', from);
        std::string line = trimmed(text.substr(from, at == std::string_view::npos ? std::string_view::npos : at - from));
        // A list mark or a number in front is not part of the sentence.
        while (!line.empty() && (line.front() == '-' || line.front() == '*' || line.front() == ' ')) {
            line.erase(line.begin());
        }
        std::size_t digits = 0;
        while (digits < line.size() && line[digits] >= '0' && line[digits] <= '9') {
            ++digits;
        }
        if (digits > 0 && digits < line.size() && (line[digits] == '.' || line[digits] == ')')) {
            line = trimmed(std::string_view{line}.substr(digits + 1));
        }
        if (!line.empty()) {
            out.push_back(std::move(line));
        }
        if (at == std::string_view::npos) {
            break;
        }
        from = at + 1;
    }
    return out;
}

std::string run(const std::string& command) {
    std::unique_ptr<FILE, int (*)(FILE*)> pipe{popen(command.c_str(), "r"), pclose};
    if (!pipe) {
        throw std::runtime_error("Advisor: cannot run curl");
    }
    std::string out;
    std::array<char, 4096> buffer{};
    for (std::size_t n = 0; (n = fread(buffer.data(), 1, buffer.size(), pipe.get())) > 0;) {
        out.append(buffer.data(), n);
    }
    return out;
}

std::string shell_quoted(std::string_view text) {
    std::string out = "'";
    for (const char c : text) {
        if (c == '\'') {
            out += "'\\''";
        } else {
            out.push_back(c);
        }
    }
    out += "'";
    return out;
}

const char* environment(const char* name) {
    const char* value = std::getenv(name);
    return value != nullptr && *value != '\0' ? value : nullptr;
}

/// A file only its owner reads, removed when it goes out of scope.
class PrivateFile {
public:
    explicit PrivateFile(std::string_view name) {
        path_ = std::filesystem::temp_directory_path() / std::format("larry_{}_{}", name,
                                                                     std::chrono::steady_clock::now().time_since_epoch().count());
        std::ofstream{path_, std::ios::binary};
        std::filesystem::permissions(path_, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                                     std::filesystem::perm_options::replace);
    }
    ~PrivateFile() {
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
    }
    PrivateFile(const PrivateFile&) = delete;
    PrivateFile& operator=(const PrivateFile&) = delete;
    void write(std::string_view text) const {
        std::ofstream out{path_, std::ios::binary};
        out << text;
    }
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

}  // namespace

Advisor::Advisor(std::string model, Transport transport)
    : model_(model.empty() ? default_model() : std::move(model)), transport_(std::move(transport)) {
    if (!transport_) {
        transport_ = [](const std::string& body) { return post(body); };
    }
}

std::string Advisor::default_model() {
    if (const char* named = environment("LARRY_LLM_MODEL")) {
        return named;
    }
    return "claude-opus-5-5";
}

std::string Advisor::source() const {
    return "llm:" + model_;
}

bool Advisor::key_in_environment() {
    return environment("ANTHROPIC_API_KEY") != nullptr || environment("ANTHROPIC_AUTH_TOKEN") != nullptr;
}

std::string Advisor::request_body(std::string_view model, std::string_view system, std::string_view user,
                                  int max_tokens) {
    return std::format(
        "{{\"model\":{},\"max_tokens\":{},\"fallbacks\":\"default\",\"thinking\":{{\"type\":\"adaptive\"}},"
        "\"system\":{},\"messages\":[{{\"role\":\"user\",\"content\":{}}}]}}",
        json_quoted(model), max_tokens, json_quoted(system), json_quoted(user));
}

std::string Advisor::read_answer(std::string_view body, bool* refused) {
    const Json answer = parse_json(body, "Advisor");
    if (const Json* error = answer.get("error")) {
        const Json* message = error->get("message");
        throw std::runtime_error(std::format("Advisor: the model's server answered with an error: {}",
                                             message && message->string() ? *message->string() : "no message"));
    }
    if (refused != nullptr) {
        const Json* stop = answer.get("stop_reason");
        *refused = stop != nullptr && stop->string() != nullptr && *stop->string() == "refusal";
    }
    std::string out;
    if (const Json* content = answer.get("content"); content && content->array()) {
        for (const Json& block : *content->array()) {
            const Json* type = block.get("type");
            const Json* text = block.get("text");
            if (type && type->string() && *type->string() == "text" && text && text->string()) {
                out += *text->string();
            }
        }
    }
    return out;
}

std::vector<std::string> Advisor::propose(std::string_view text, std::size_t limit) const {
    const std::string user = std::format(
        "List the facts this text states, as short sentences of the form \"X is Y.\" or \"X does Y.\", one per "
        "line, at most {}. Use only what the text states and add nothing. No numbering, no commentary.\n\nText:\n{}",
        limit, text);
    const std::string body = request_body(
        model_, "You help Larry, a small program that learns from sentences a person then checks. Reply only with what is asked, in plain short sentences.",
        user, 2048);
    bool refused = false;
    const std::string answer = read_answer(transport_(body), &refused);
    std::vector<std::string> out;
    if (refused) {
        return out;
    }
    for (std::string line : lines_of(answer)) {
        if (line.back() != '.' && line.back() != '!' && line.back() != '?') {
            line += '.';
        }
        out.push_back(std::move(line));
        if (out.size() >= limit) {
            break;
        }
    }
    return out;
}

Opinion Advisor::opinion(std::string_view claim) const {
    const std::string user = std::format(
        "Is this true, false, or unknown? Answer on the first line with one word: true, false or unknown. On the "
        "second line give the reason in one sentence.\n\n{}",
        claim);
    const std::string body = request_body(
        model_, "You give Larry, a small program that learns from sentences, a second opinion. Reply only as asked.",
        user, 1024);
    Opinion out;
    out.model = model_;
    const std::string answer = read_answer(transport_(body), &out.refused);
    if (out.refused) {
        return out;
    }
    const std::vector<std::string> lines = lines_of(answer);
    if (!lines.empty()) {
        std::string first = lines.front();
        for (char& c : first) {
            c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
        }
        for (const std::string_view word : {"true", "false", "unknown"}) {
            if (first.starts_with(word)) {
                out.verdict = word;
                std::string_view rest = std::string_view{first}.substr(word.size());
                while (!rest.empty() && (rest.front() == ':' || rest.front() == ',' || rest.front() == '.' ||
                                         rest.front() == '-' || rest.front() == ' ')) {
                    rest.remove_prefix(1);
                }
                out.reason = trimmed(rest);
                break;
            }
        }
        for (std::size_t i = out.verdict.empty() ? 0 : 1; i < lines.size(); ++i) {
            out.reason += (out.reason.empty() ? "" : " ") + lines[i];
        }
        if (out.verdict.empty()) {
            out.verdict = "unknown";
        }
    }
    return out;
}

std::string Advisor::ask(std::string_view question) const {
    const std::string body = request_body(
        model_, "You answer Larry, a small program that learns from sentences a person then checks. Answer in one short plain sentence.",
        std::format("Answer in one short sentence: {}", question), 1024);
    bool refused = false;
    const std::string answer = read_answer(transport_(body), &refused);
    return refused ? std::string{} : trimmed(answer);
}

std::string Advisor::post(const std::string& body) {
    const char* key = environment("ANTHROPIC_API_KEY");
    const char* token = environment("ANTHROPIC_AUTH_TOKEN");
    if (key == nullptr && token == nullptr) {
        throw std::runtime_error("Advisor: no key in the environment: set ANTHROPIC_API_KEY (or ANTHROPIC_AUTH_TOKEN) in .env");
    }
    // The key goes in a file only the owner reads, never on the command line.
    const PrivateFile headers{"headers"};
    const PrivateFile request{"request"};
    std::string lines = std::format("content-type: application/json\nanthropic-version: {}\n", api_version);
    if (key != nullptr) {
        lines += std::format("x-api-key: {}\nanthropic-beta: {}\n", key, fallback_beta);
    } else {
        lines += std::format("authorization: Bearer {}\nanthropic-beta: {},{}\n", token, oauth_beta, fallback_beta);
    }
    headers.write(lines);
    request.write(body);
    const std::string command =
        std::format("curl -sS --max-time 180 -H @{} --data-binary @{} -w '\\n%{{http_code}}' {} 2>&1",
                    shell_quoted(headers.path().string()), shell_quoted(request.path().string()), endpoint);
    for (int attempt = 0; attempt < 2; ++attempt) {
        std::string out = run(command);
        const std::size_t cut = out.rfind('\n');
        if (cut == std::string::npos) {
            throw std::runtime_error("Advisor: curl gave no answer");
        }
        const std::string code = trimmed(std::string_view{out}.substr(cut + 1));
        std::string answer = out.substr(0, cut);
        if (code == "200") {
            return answer;
        }
        if ((code == "429" || code == "529") && attempt == 0) {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            continue;
        }
        if (code == "000" || code.empty()) {
            throw std::runtime_error(std::format("Advisor: cannot reach the model's server: {}", trimmed(answer)));
        }
        // The server's own message, when the answer is JSON; else the code.
        try {
            (void)read_answer(answer);
        } catch (const std::runtime_error& e) {
            throw std::runtime_error(std::format("{} ({})", e.what(), code));
        }
        throw std::runtime_error(std::format("Advisor: the model's server answered {}", code));
    }
    throw std::runtime_error("Advisor: the model's server is busy: too many requests, twice");
}

}  // namespace larry
