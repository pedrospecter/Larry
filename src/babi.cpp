#include "larry/babi.hpp"

#include "larry/atom_operations.hpp"
#include "larry/brain.hpp"
#include "larry/grammar.hpp"
#include "larry/json.hpp"
#include "larry/memory.hpp"

#include <algorithm>
#include <chrono>
#include <format>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace larry {

namespace {

std::string lower(std::string_view s) {
    std::string out;
    for (const char c : s) {
        out.push_back(static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c));
    }
    return out;
}

std::vector<std::string> words_of(std::string_view text) {
    std::vector<std::string> out;
    std::string current;
    for (const char c : text) {
        const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                            c == '\'' || static_cast<unsigned char>(c) >= 0x80;
        if (letter) {
            current.push_back(c);
        } else if (!current.empty()) {
            out.push_back(lower(current));
            current.clear();
        }
    }
    if (!current.empty()) {
        out.push_back(lower(current));
    }
    return out;
}

std::vector<std::string> split(std::string_view text, char separator) {
    std::vector<std::string> out;
    std::string current;
    for (const char c : text) {
        if (c == separator) {
            out.push_back(current);
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    out.push_back(current);
    return out;
}

}  // namespace

std::vector<BabiStory> read_babi(const std::filesystem::path& file) {
    std::ifstream in{file, std::ios::binary};
    if (!in) {
        throw std::runtime_error(std::format("babi: cannot read {}", file.string()));
    }
    const std::string text{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    const Json page = parse_json(text, "babi");
    const Json* rows = page.get("rows");
    if (rows == nullptr || rows->array() == nullptr) {
        throw std::runtime_error(std::format("babi: {} is not a page of rows", file.string()));
    }
    std::vector<BabiStory> out;
    for (const Json& row : *rows->array()) {
        const Json* r = row.get("row");
        const Json* story = r != nullptr ? r->get("story") : nullptr;
        if (story == nullptr) {
            continue;
        }
        const Json* texts = story->get("text");
        const Json* types = story->get("type");
        const Json* answers = story->get("answer");
        const Json* supporting = story->get("supporting_ids");
        if (texts == nullptr || texts->array() == nullptr || types == nullptr || types->array() == nullptr) {
            continue;
        }
        BabiStory s;
        const JsonArray& list = *texts->array();
        for (std::size_t i = 0; i < list.size(); ++i) {
            BabiLine line;
            if (const std::string* t = list[i].string()) {
                line.text = *t;
            }
            if (i < types->array()->size()) {
                if (const double* kind = (*types->array())[i].number()) {
                    line.question = *kind == 1;
                }
            }
            if (answers != nullptr && answers->array() != nullptr && i < answers->array()->size()) {
                if (const std::string* a = (*answers->array())[i].string()) {
                    line.answer = *a;
                }
            }
            if (supporting != nullptr && supporting->array() != nullptr && i < supporting->array()->size()) {
                if (const JsonArray* ids = (*supporting->array())[i].array()) {
                    for (const Json& id : *ids) {
                        if (const std::string* n = id.string()) {
                            line.supporting.push_back(std::atoi(n->c_str()));
                        }
                    }
                }
            }
            s.lines.push_back(std::move(line));
        }
        out.push_back(std::move(s));
    }
    return out;
}

std::vector<BabiStory> read_babi_text(const std::filesystem::path& file) {
    std::ifstream in{file, std::ios::binary};
    if (!in) {
        throw std::runtime_error(std::format("babi: cannot read {}", file.string()));
    }
    std::vector<BabiStory> out;
    BabiStory current;
    for (std::string line; std::getline(in, line);) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            continue;
        }
        const std::size_t space = line.find(' ');
        if (space == std::string::npos) {
            continue;
        }
        const int number = std::atoi(line.substr(0, space).c_str());
        if (number == 1 && !current.lines.empty()) {
            out.push_back(std::move(current));
            current = BabiStory{};
        }
        const std::vector<std::string> fields = split(line.substr(space + 1), '\t');
        BabiLine l;
        l.text = fields[0];
        if (fields.size() >= 2) {
            l.question = true;
            l.answer = fields[1];
            if (fields.size() >= 3) {
                for (const std::string& id : split(fields[2], ' ')) {
                    if (!id.empty()) {
                        l.supporting.push_back(std::atoi(id.c_str()));
                    }
                }
            }
        }
        current.lines.push_back(std::move(l));
    }
    if (!current.lines.empty()) {
        out.push_back(std::move(current));
    }
    return out;
}

bool babi_right(std::string_view reply, std::string_view answer) {
    const std::vector<std::string> words = words_of(reply);
    const std::string wanted = lower(answer);
    if (wanted == "yes" || wanted == "no") {
        return !words.empty() && words.front() == wanted;
    }
    if (wanted.find(',') != std::string::npos) {
        for (const std::string& item : split(wanted, ',')) {
            if (!std::ranges::contains(words, item)) {
                return false;
            }
        }
        return true;
    }
    return !words.empty() && words.back() == wanted;
}

std::string BabiResult::text() const {
    return std::format("task {}: {:.1f}% of {} questions right in {} stories, in {:.1f} s", task, accuracy() * 100.0,
                       asked, stories, seconds);
}

BabiResult run_babi(const BaseRules& rules, int task, const std::vector<BabiStory>& stories,
                    const std::filesystem::path& scratch, const std::function<void(std::string_view)>& progress) {
    using Clock = std::chrono::steady_clock;
    const Clock::time_point start = Clock::now();
    const AtomOperations ops;
    Grammar grammar{rules};
    BabiResult out;
    out.task = task;
    for (const BabiStory& story : stories) {
        ++out.stories;
        std::filesystem::remove(scratch);
        Memory memory{scratch};
        Brain brain{rules, memory, nullptr, nullptr, &grammar};
        const std::string name = std::format("babi:{}:{}", task, out.stories);
        brain.molecule(Bytes(name.begin(), name.end()));
        for (const BabiLine& line : story.lines) {
            const Sentence sentence = ops.from_text(line.text);
            if (ops.bytes(sentence).empty()) {
                continue;
            }
            if (!line.question) {
                (void)brain.hear(sentence, "babi");
                continue;
            }
            const Reply reply = brain.answer(sentence);
            ++out.asked;
            if (babi_right(reply.text, line.answer)) {
                ++out.right;
            } else if (out.misses.size() < 10) {
                out.misses.push_back(std::format("{} expected {}, got: {}", line.text, line.answer, reply.text));
            }
        }
        if (progress && out.stories % 50 == 0) {
            progress(std::format("task {}: {} stories, {} of {} right so far", task, out.stories, out.right, out.asked));
        }
    }
    std::filesystem::remove(scratch);
    out.seconds = std::chrono::duration<double>(Clock::now() - start).count();
    return out;
}

}  // namespace larry
