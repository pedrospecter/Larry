// Tests for Assimilation: sentences, entities and describing.

#include "larry/assimilation.hpp"

#include "larry/atom_operations.hpp"
#include "larry/base_rules.hpp"
#include "larry/description.hpp"
#include "larry/dictionary.hpp"
#include "larry/memory.hpp"

#include "check.hpp"

#include <algorithm>
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
    // A word with two categories: the context chooses one as a guess ("the _ is"
    // holds nouns); without a context to choose, it stays open with both.
    const larry::Description open = assimilation().describe(ops().from_text("The run is tall."), &memory);
    CHECK(open.notes[1].source == larry::Source::Guess);
    CHECK(open.entities.entities[1].category == (Bytes{'n', 'o', 'u', 'n'}));
    const larry::Description alone = assimilation().describe(ops().from_text("Run"), &memory);
    CHECK(alone.notes[0].source == larry::Source::Open);
    CHECK(alone.notes[0].candidates.size() == 2);
    CHECK(alone.entities.entities[0].category.empty());
    // An unknown word is unknown.
    const larry::Description unknown = assimilation().describe(ops().from_text("The sky is azure."), &memory);
    CHECK(unknown.notes[3].source == larry::Source::Guess);  // from the context, see below
    CHECK(unknown.notes[0].source == larry::Source::Memory);
    CHECK(assimilation().describe(ops().from_text("Zorp"), &memory).notes[0].source ==
          larry::Source::Unknown);
    // Memory makes the question rule work: "Is" is an auxiliary verb.
    const larry::Description question = assimilation().describe(ops().from_text("Is the sky blue"), &memory);
    CHECK(question.category.bytes == (Bytes{'q', 'u', 'e', 's', 't', 'i', 'o', 'n'}));
}

namespace {

std::vector<std::string> types_of(const larry::Description& d, std::size_t i) {
    std::vector<std::string> out;
    for (const Bytes& t : d.entities.entities[i].types) {
        out.emplace_back(t.begin(), t.end());
    }
    return out;
}

std::string type_of(const larry::Description& d) {
    return {d.type.bytes.begin(), d.type.bytes.end()};
}

larry::Description taught(std::string_view text, std::vector<std::string_view> categories) {
    std::vector<Bytes> list;
    for (const std::string_view c : categories) {
        list.emplace_back(c.begin(), c.end());
    }
    return assimilation().describe(ops().from_text(text), nullptr, list);
}

}  // namespace

TEST(describe_guesses_an_unknown_word_from_its_context) {
    const std::filesystem::path file = std::filesystem::temp_directory_path() / "larry_test_guess.atoms";
    std::filesystem::remove(file);
    larry::Memory memory{file};
    const auto teach = [&](std::string_view text, std::vector<std::string_view> categories) {
        std::vector<Bytes> list;
        for (const std::string_view c : categories) {
            list.emplace_back(c.begin(), c.end());
        }
        const larry::Description d = assimilation().describe(ops().from_text(text), &memory, list);
        memory.store(d.atom, d.metadata);
    };
    teach("The sky is blue.", {"determiner", "noun", "auxiliary verb", "adjective"});
    teach("The sea is wide.", {"determiner", "noun", "auxiliary verb", "adjective"});
    teach("The grass is tall.", {"determiner", "noun", "auxiliary verb", "adjective"});
    teach("Snow is white.", {"noun", "auxiliary verb", "adjective"});
    teach("Birds fly.", {"noun", "verb"});
    // "azure" after "is" at the end: the words after "is" are adjectives.
    const larry::Description azure = assimilation().describe(ops().from_text("The sky is azure."), &memory);
    CHECK(azure.notes[3].source == larry::Source::Guess);
    CHECK(azure.entities.entities[3].category == (Bytes{'a', 'd', 'j', 'e', 'c', 't', 'i', 'v', 'e'}));
    CHECK(!azure.notes[3].candidates.empty());
    CHECK(azure.entities.entities[3].types.back() == (Bytes{'g', 'u', 'e', 's', 's', 'e', 'd'}));
    CHECK(azure.notes[0].source == larry::Source::Memory);
    // "moon" between "the" and "is": the words there are nouns.
    const larry::Description moon = assimilation().describe(ops().from_text("The moon is white."), &memory);
    CHECK(moon.notes[1].source == larry::Source::Guess);
    CHECK(moon.entities.entities[1].category == (Bytes{'n', 'o', 'u', 'n'}));
    // No context in memory: no guess.
    const larry::Description alone = assimilation().describe(ops().from_text("Zorp"), &memory);
    CHECK(alone.notes[0].source == larry::Source::Unknown);
    // A stored guess is no evidence: the word stays unknown to memory.
    CHECK(memory.store(azure.atom, azure.metadata) == larry::Stored::New);
    CHECK(memory.categories_of(Bytes{'a', 'z', 'u', 'r', 'e'}).empty());
    CHECK(memory.uses(Bytes{'a', 'z', 'u', 'r', 'e'}).size() == 1);
    CHECK(memory.uses(Bytes{'a', 'z', 'u', 'r', 'e'}).front().category.empty());
    // Its neighbour's context does not count it either.
    bool azure_after_is = false;
    for (const larry::WordUse& use : memory.uses(Bytes{'i', 's'})) {
        if (use.after == (Bytes{'a', 'z', 'u', 'r', 'e'})) {
            azure_after_is = true;
            CHECK(use.after_category.empty());
        }
    }
    CHECK(azure_after_is);
    const larry::Description again = assimilation().describe(ops().from_text("The sky is azure."), &memory);
    CHECK(again.notes[3].source == larry::Source::Guess);
    CHECK(std::ranges::contains(memory.words(), Bytes{'a', 'z', 'u', 'r', 'e'}));
    CHECK(memory.words().size() == 13);
}

TEST(describe_with_the_dictionary) {
    const larry::Dictionary dictionary{larry::Dictionary::file_for(larry::Language::English)};
    const larry::Assimilation with{rules(), &dictionary};
    const std::filesystem::path file = std::filesystem::temp_directory_path() / "larry_test_dictionary.atoms";
    std::filesystem::remove(file);
    larry::Memory memory{file};
    const auto teach = [&](std::string_view text, std::vector<std::string_view> categories) {
        std::vector<Bytes> list;
        for (const std::string_view c : categories) {
            list.emplace_back(c.begin(), c.end());
        }
        const larry::Description d = with.describe(ops().from_text(text), &memory, list);
        memory.store(d.atom, d.metadata);
    };
    teach("The sky is blue.", {"determiner", "noun", "auxiliary verb", "adjective"});
    teach("The sea is wide.", {"determiner", "noun", "auxiliary verb", "adjective"});
    // Memory first: "sky" is a noun by the lesson, whatever the dictionary says.
    CHECK(with.describe(ops().from_text("The sky."), &memory).notes[1].source == larry::Source::Memory);
    // The dictionary gives one category: it is the word's.
    const larry::Description oh = with.describe(ops().from_text("Oh, the sky."), &memory);
    CHECK(oh.notes[0].source == larry::Source::Dictionary);
    CHECK(oh.entities.entities[0].category == (Bytes{'i', 'n', 't', 'e', 'r', 'j', 'e', 'c', 't', 'i', 'o', 'n'}));
    CHECK(oh.entities.entities[0].types.back() != (Bytes{'g', 'u', 'e', 's', 's', 'e', 'd'}));
    // Several: the context chooses among them, as a guess.
    const larry::Description azure = with.describe(ops().from_text("The sky is azure."), &memory);
    CHECK(azure.notes[3].source == larry::Source::Guess);
    CHECK(azure.entities.entities[3].category == (Bytes{'a', 'd', 'j', 'e', 'c', 't', 'i', 'v', 'e'}));
    CHECK(azure.notes[3].candidates.front() == (Bytes{'a', 'd', 'j', 'e', 'c', 't', 'i', 'v', 'e'}));
    // Several and no context to choose: open, with the dictionary's candidates.
    const larry::Description alone = with.describe(ops().from_text("Azure"), &memory);
    CHECK(alone.notes[0].source == larry::Source::Open);
    CHECK(alone.notes[0].candidates.size() >= 2);
    // Nobody knows the word: unknown, with the words one slip away.
    const larry::Description slip = with.describe(ops().from_text("The skyy is blue."), &memory);
    CHECK(slip.notes[1].source == larry::Source::Unknown);
    CHECK(!slip.notes[1].near.empty() && slip.notes[1].near.front() == (Bytes{'s', 'k', 'y'}));  // known to memory: first
    CHECK(slip.entities.entities[1].category.empty());
    CHECK(with.describe(ops().from_text("Zqxjkv"), &memory).notes[0].near.empty());
    // Without the dictionary nothing changes.
    CHECK(assimilation().describe(ops().from_text("Oh, the sky."), &memory).notes[0].source == larry::Source::Unknown);
    CHECK(assimilation().describe(ops().from_text("The skyy is blue."), &memory).notes[1].near.empty());
    // The dictionary category is stored and counts as evidence; a guess does not.
    const larry::Description stored = with.describe(ops().from_text("Oh, the sky is azure."), &memory);
    memory.store(stored.atom, stored.metadata);
    CHECK(memory.categories_of(Bytes{'o', 'h'}).size() == 1);
    CHECK(memory.categories_of(Bytes{'a', 'z', 'u', 'r', 'e'}).empty());
}

TEST(types_features_and_roles) {
    using V = std::vector<std::string>;
    const larry::Description sky = taught("The sky is blue.", {"determiner", "noun", "auxiliary verb", "adjective"});
    CHECK(types_of(sky, 0) == V{"subject"});
    CHECK(types_of(sky, 1) == (V{"singular", "subject"}));
    CHECK(types_of(sky, 2) == (V{"third person", "singular", "present", "predicate"}));
    CHECK(types_of(sky, 3) == (V{"positive", "attribute"}));
    CHECK(type_of(sky) == "subject subject predicate attribute / neutral");

    const larry::Description birds = taught("Birds fly.", {"noun", "verb"});
    CHECK(types_of(birds, 0) == (V{"plural", "subject"}));
    CHECK(types_of(birds, 1) == (V{"base", "predicate"}));
    CHECK(type_of(birds) == "subject predicate / neutral");

    const larry::Description mary = taught("Mary went to the kitchen.", {"proper noun", "verb", "preposition", "determiner", "noun"});
    CHECK(types_of(mary, 0) == (V{"singular", "subject"}));
    CHECK(types_of(mary, 1) == (V{"past", "predicate"}));
    CHECK(types_of(mary, 2) == V{"complement"});
    CHECK(types_of(mary, 4) == (V{"singular", "complement"}));

    const larry::Description tom = taught("Tom has three apples.", {"proper noun", "verb", "numeral", "noun"});
    CHECK(types_of(tom, 1) == (V{"third person", "predicate"}));
    CHECK(types_of(tom, 2) == (V{"cardinal", "object"}));
    CHECK(types_of(tom, 3) == (V{"plural", "object"}));
    CHECK(type_of(tom) == "subject predicate object object / neutral");

    const larry::Description order = taught("Close the door.", {"verb", "determiner", "noun"});
    CHECK(types_of(order, 0) == (V{"base", "predicate"}));
    CHECK(types_of(order, 2) == (V{"singular", "object"}));
    CHECK(type_of(order) == "predicate object object / neutral");

    const larry::Description more = taught("I came 3rd and they were happier.", {"pronoun", "verb", "numeral", "conjunction", "pronoun", "auxiliary verb", "adjective"});
    CHECK(types_of(more, 0) == (V{"first person", "singular", "subject"}));
    CHECK(types_of(more, 1) == (V{"past", "predicate"}));
    CHECK(types_of(more, 2) == (V{"ordinal", "object"}));
    CHECK(types_of(more, 3) == V{"link"});
    CHECK(types_of(more, 4) == (V{"third person", "plural", "object"}));
    CHECK(types_of(more, 5) == (V{"past", "plural", "predicate"}));
    CHECK(types_of(more, 6) == (V{"comparative", "object"}));

    // Endings need the category and a long enough word; forms override them.
    const larry::Description forms = taught("The bus is running.", {"determiner", "noun", "auxiliary verb", "verb"});
    CHECK(types_of(forms, 1) == (V{"singular", "subject"}));
    CHECK(types_of(forms, 3) == (V{"progressive", "predicate"}));
    const larry::Description children = taught("Children sleep.", {"noun", "verb"});
    CHECK(types_of(children, 0) == (V{"plural", "subject"}));
    // Without a category there are no features, only the role.
    const larry::Description unknown = assimilation().describe(ops().from_text("Azure skies."), nullptr);
    CHECK(types_of(unknown, 0) == V{"subject"});
    CHECK(type_of(unknown) == "subject subject / neutral");
    CHECK(type_of(assimilation().describe(ops().from_text("..."), nullptr)) == "/ neutral");
}

TEST(types_emotion) {
    CHECK(type_of(taught("I am so happy today.", {"pronoun", "auxiliary verb", "adverb", "adjective", "adverb"})).ends_with("/ joy"));
    CHECK(type_of(taught("She was sad.", {"pronoun", "auxiliary verb", "adjective"})).ends_with("/ sadness"));
    CHECK(type_of(taught("I hate rain.", {"pronoun", "verb", "noun"})).ends_with("/ anger"));
    CHECK(type_of(taught("Wow, it works!", {"interjection", "pronoun", "verb"})).ends_with("/ surprise"));
    CHECK(type_of(taught("Oh great, it rained again.", {"interjection", "adjective", "pronoun", "verb", "adverb"})).ends_with("/ sarcasm"));
    CHECK(type_of(assimilation().describe(ops().from_text("Yeah right."), nullptr)) == "subject subject / sarcasm");
    CHECK(type_of(taught("The sky is blue.", {"determiner", "noun", "auxiliary verb", "adjective"})).ends_with("/ neutral"));
    // The first emotion word decides; a marker wins over it.
    CHECK(type_of(assimilation().describe(ops().from_text("Happy and sad."), nullptr)).ends_with("/ joy"));
    CHECK(type_of(assimilation().describe(ops().from_text("I am happy, yeah right."), nullptr)).ends_with("/ sarcasm"));
}

int main() {
    return larry::test::run();
}
