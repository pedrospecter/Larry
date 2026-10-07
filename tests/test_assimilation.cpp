// Tests for Assimilation: sentences, entities and describing.

#include "larry/assimilation.hpp"

#include "larry/atom_operations.hpp"
#include "larry/base_rules.hpp"
#include "larry/description.hpp"
#include "larry/memory.hpp"

#include "check.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <print>
#include <string>
#include <string_view>
#include <vector>

using larry::Assimilation;
using larry::Bytes;

namespace {

const larry::BaseRules& rules() {
    static const larry::BaseRules instance{larry::Language::English};
    return instance;
}

const Assimilation& assimilation() {
    static const Assimilation instance{rules()};
    return instance;
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

// A suite line: fields separated by " | ". The first field keeps its spaces
// (only the separator is cut); the others are trimmed and empty ones dropped.
struct Case {
    std::string input;
    std::vector<std::string> expected;
    std::size_t line;
};

std::string unescape(std::string_view s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size() && s[i + 1] == 'n') {
            out.push_back('\n');
            ++i;
        } else if (s[i] == '\\' && i + 1 < s.size() && s[i + 1] == 'r') {
            out.push_back('\r');
            ++i;
        } else {
            out.push_back(s[i]);
        }
    }
    return out;
}

std::vector<Case> suite(std::string_view name, bool escapes) {
    const std::filesystem::path file = std::filesystem::path{LARRY_TEST_DATA_DIR} / "en" / name;
    std::ifstream in{file, std::ios::binary};
    CHECK(static_cast<bool>(in));
    std::vector<Case> out;
    std::size_t number = 0;
    for (std::string line; std::getline(in, line);) {
        ++number;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty() || line.front() == '#') {
            continue;
        }
        Case c{.input = {}, .expected = {}, .line = number};
        std::string_view rest = line;
        std::size_t separator = rest.find(" | ");
        if (separator == std::string_view::npos) {
            // "text |" with nothing after it, or the separator cut by a trimmed line.
            separator = rest.rfind(" |");
            if (separator != std::string_view::npos && separator + 2 == rest.size()) {
                rest = rest.substr(0, separator + 2);
            }
        }
        if (separator == std::string_view::npos) {
            c.input = std::string{rest};
            rest = {};
        } else {
            c.input = std::string{rest.substr(0, separator)};
            rest.remove_prefix(separator + 2);
        }
        while (!rest.empty()) {
            const std::size_t next = rest.find(" | ");
            const std::string_view field = trim(rest.substr(0, next));
            if (!field.empty()) {
                c.expected.emplace_back(escapes ? unescape(field) : std::string{field});
            }
            if (next == std::string_view::npos) {
                break;
            }
            rest.remove_prefix(next + 3);
        }
        if (escapes) {
            c.input = unescape(c.input);
        }
        out.push_back(std::move(c));
    }
    return out;
}

const larry::AtomOperations& ops() {
    static const larry::AtomOperations instance;
    return instance;
}

std::vector<std::string> words(const larry::EntitiesElectron& entities) {
    std::vector<std::string> out;
    for (const larry::Entity& e : entities.entities) {
        out.emplace_back(e.word.begin(), e.word.end());
    }
    return out;
}

std::string show(const std::vector<std::string>& items) {
    std::string out;
    for (const std::string& item : items) {
        out += '[';
        out += item;
        out += ']';
    }
    return out;
}

}  // namespace

TEST(entity_suite) {
    const std::vector<Case> cases = suite("entities.txt", false);
    CHECK(cases.size() >= 200);
    std::size_t failed = 0;
    for (const Case& c : cases) {
        const std::vector<std::string> got =
            words(assimilation().entities(ops().from_text(c.input)));
        if (got != c.expected) {
            ++failed;
            std::println(stderr, "entities.txt line {}: \"{}\"\n  expected {}\n  got      {}", c.line,
                         c.input, show(c.expected), show(got));
        }
    }
    CHECK(failed == 0);
}

TEST(sentence_suite) {
    const std::vector<Case> cases = suite("sentences.txt", true);
    CHECK(cases.size() >= 40);
    std::size_t failed = 0;
    for (const Case& c : cases) {
        std::vector<std::string> got;
        for (const larry::Sentence& s : assimilation().sentences(c.input)) {
            got.emplace_back(ops().text(s));
        }
        if (got != c.expected) {
            ++failed;
            std::println(stderr, "sentences.txt line {}: \"{}\"\n  expected {}\n  got      {}",
                         c.line, c.input, show(c.expected), show(got));
        }
    }
    CHECK(failed == 0);
}

TEST(sentences_are_atoms_made_of_the_text) {
    const std::string text = "The sky is blue. Is it? Yes!";
    const std::vector<larry::Sentence> found = assimilation().sentences(text);
    CHECK(found.size() == 3);
    std::size_t from = 0;
    for (const larry::Sentence& s : found) {
        CHECK(s.size() % 8 == 0 && s.size() > 0);
        const std::size_t at = text.find(ops().text(s), from);
        CHECK(at != std::string::npos);
        from = at + ops().text(s).size();
    }
}

TEST(random_bytes_never_crash) {
    // A small deterministic generator, so the run is the same every time.
    std::uint64_t state = 0x9E3779B97F4A7C15ULL;
    const auto next = [&]() {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return state;
    };
    const std::string_view alphabet = " \t\n\r.?!'\"-,:/()[]aAbBzZ09 \xC3\xA9\xE2\x80\x9C\xF0\x9F\x91\x8D\x80\xFF\xC2\xE2\xF0";
    std::size_t entities = 0;
    std::size_t sentences = 0;
    for (int round = 0; round < 3000; ++round) {
        std::string text;
        const std::size_t length = next() % 48;
        for (std::size_t i = 0; i < length; ++i) {
            if (next() % 4 == 0) {
                text.push_back(static_cast<char>(next() & 0xFF));
            } else {
                text.push_back(alphabet[next() % alphabet.size()]);
            }
        }
        for (const larry::Sentence& s : assimilation().sentences(text)) {
            ++sentences;
            CHECK(s.size() > 0);
            CHECK(text.find(ops().text(s)) != std::string::npos);
            for (const larry::Entity& e : assimilation().entities(s).entities) {
                ++entities;
                CHECK(!e.word.empty());
                CHECK(e.word.front() != ' ' && e.word.back() != ' ');
            }
        }
    }
    CHECK(sentences > 0);
    CHECK(entities > 0);
}

TEST(describe_without_memory) {
    const larry::Description d = assimilation().describe(ops().from_text("The sky."), nullptr);
    CHECK(d.entities.entities.size() == 2);
    CHECK(d.notes.size() == 2);
    CHECK(d.notes[0].source == larry::Source::Unknown);
    CHECK(d.entities.entities[0].category.empty());
    CHECK(d.image.bytes == (Bytes{'T', 'h', 'e', ' ', 's', 'k', 'y', '.'}));
    CHECK(d.atom.size() == 64);
    CHECK(d.category.bytes == (Bytes{'a', 'f', 'f', 'i', 'r', 'm', 'a', 't', 'i', 'o', 'n'}));
    CHECK(!d.metadata.bytes.empty());
    CHECK(ops().electrons(d.metadata).entities.entities.size() == 2);
}

TEST(describe_with_taught_categories) {
    const std::vector<Bytes> taught{Bytes{'d', 'e', 't', 'e', 'r', 'm', 'i', 'n', 'e', 'r'},
                                    Bytes{'n', 'o', 'u', 'n'}};
    const larry::Description d = assimilation().describe(ops().from_text("The sky."), nullptr, taught);
    CHECK(d.entities.entities[0].category == taught[0]);
    CHECK(d.entities.entities[1].category == taught[1]);
    CHECK(d.notes[0].source == larry::Source::Taught);
    CHECK(d.notes[1].source == larry::Source::Taught);
    const std::vector<Bytes> wrong_count{taught[0]};
    CHECK_THROWS(assimilation().describe(ops().from_text("The sky."), nullptr, wrong_count),
                 std::invalid_argument);
}

TEST(describe_from_memory) {
    const std::filesystem::path file = std::filesystem::temp_directory_path() / "larry_test_describe.atoms";
    std::filesystem::remove(file);
    larry::Memory memory{file};
    const auto teach = [&](std::string_view text, std::vector<std::string_view> categories) {
        std::vector<Bytes> taught;
        for (const std::string_view c : categories) {
            taught.emplace_back(c.begin(), c.end());
        }
        const larry::Description d = assimilation().describe(ops().from_text(text), &memory, taught);
        CHECK(memory.store(d.atom, d.metadata) == larry::Stored::New);
    };
    teach("The sky is blue.", {"determiner", "noun", "auxiliary verb", "adjective"});
    teach("The grass is tall.", {"determiner", "noun", "auxiliary verb", "adjective"});
    teach("Leaves are green.", {"noun", "auxiliary verb", "adjective"});
    teach("I run.", {"pronoun", "verb"});
    teach("A run.", {"determiner", "noun"});

    // A new sentence made only of taught words with one category each.
    const larry::Description d = assimilation().describe(ops().from_text("The grass is green."), &memory);
    CHECK(d.entities.entities.size() == 4);
    const Bytes determiner{'d', 'e', 't', 'e', 'r', 'm', 'i', 'n', 'e', 'r'};
    CHECK(d.entities.entities[0].category == determiner);
    CHECK(d.entities.entities[1].category == (Bytes{'n', 'o', 'u', 'n'}));
    CHECK(d.entities.entities[3].category == (Bytes{'a', 'd', 'j', 'e', 'c', 't', 'i', 'v', 'e'}));
    for (const larry::EntityNote& note : d.notes) {
        CHECK(note.source == larry::Source::Memory);
    }
    CHECK(d.category.bytes == (Bytes{'a', 'f', 'f', 'i', 'r', 'm', 'a', 't', 'i', 'o', 'n'}));
    // Capitals do not matter: "the" was taught as "The".
    CHECK(assimilation().describe(ops().from_text("the grass"), &memory).notes[0].source ==
          larry::Source::Memory);
    // A word with two categories stays open, with both as candidates.
    const larry::Description open = assimilation().describe(ops().from_text("The run is tall."), &memory);
    CHECK(open.notes[1].source == larry::Source::Open);
    CHECK(open.notes[1].candidates.size() == 2);
    CHECK(open.entities.entities[1].category.empty());
    // An unknown word is unknown.
    const larry::Description unknown = assimilation().describe(ops().from_text("The sky is azure."), &memory);
    CHECK(unknown.notes[3].source == larry::Source::Unknown);
    CHECK(unknown.notes[0].source == larry::Source::Memory);
    // Memory makes the question rule work: "Is" is an auxiliary verb.
    const larry::Description question = assimilation().describe(ops().from_text("Is the sky blue"), &memory);
    CHECK(question.category.bytes == (Bytes{'q', 'u', 'e', 's', 't', 'i', 'o', 'n'}));
}

int main() {
    return larry::test::run();
}
