// Tests for the second constellation (A12): Portuguese, with the base rules
// and the lessons of base_rules/pt/ and lessons/pt/ alone. The suites of A1
// (entities, sentences), A2 (describing from memory), A3 (qualification),
// A4 (forms), A6a (a guess from context), A10 (a defining sentence as a
// bond), R4 (a chain) and M1 (arithmetic in Portuguese words).

#include "larry/assimilation.hpp"
#include "larry/atom_operations.hpp"
#include "larry/base_rules.hpp"
#include "larry/brain.hpp"
#include "larry/cognition.hpp"
#include "larry/lesson.hpp"
#include "larry/memory.hpp"

#include "check.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <print>
#include <string>
#include <string_view>
#include <vector>

using larry::Bytes;

namespace {

const larry::BaseRules& rules() {
    static const larry::BaseRules instance{larry::Language::Portuguese};
    return instance;
}

const larry::Assimilation& assimilation() {
    static const larry::Assimilation instance{rules()};
    return instance;
}

Bytes b(std::string_view text) {
    return Bytes(text.begin(), text.end());
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

std::string unescape(std::string_view s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size() && s[i + 1] == 'n') {
            out.push_back('\n');
            ++i;
        } else {
            out.push_back(s[i]);
        }
    }
    return out;
}

struct Case {
    std::string input;
    std::vector<std::string> expected;
    std::size_t line;
};

std::vector<Case> suite(std::string_view name) {
    std::ifstream in{std::filesystem::path{LARRY_TEST_DATA_DIR} / "pt" / name, std::ios::binary};
    CHECK(static_cast<bool>(in));
    std::vector<Case> out;
    std::size_t number = 0;
    for (std::string line; std::getline(in, line);) {
        ++number;
        if (line.empty() || line.front() == '#') {
            continue;
        }
        Case c{.input = {}, .expected = {}, .line = number};
        std::string_view rest = line;
        const std::size_t separator = rest.find(" | ");
        c.input = unescape(rest.substr(0, separator));
        if (separator != std::string_view::npos) {
            rest.remove_prefix(separator + 3);
            while (!rest.empty()) {
                const std::size_t next = rest.find(" | ");
                c.expected.emplace_back(unescape(trim(rest.substr(0, next))));
                if (next == std::string_view::npos) {
                    break;
                }
                rest.remove_prefix(next + 3);
            }
        }
        out.push_back(std::move(c));
    }
    return out;
}

// A memory taught the Portuguese lessons, in a scratch file.
larry::Memory& memory() {
    static larry::Memory* instance = [] {
        const std::filesystem::path file = std::filesystem::temp_directory_path() / "larry_test_pt.atoms";
        std::filesystem::remove(file);
        auto* m = new larry::Memory{file};
        const larry::AtomOperations ops;
        for (const std::filesystem::path& lesson_file : larry::lesson_files(larry::Language::Portuguese)) {
            for (const larry::Lesson& lesson : larry::read_lessons(lesson_file)) {
                const larry::Description d =
                    assimilation().describe(ops.from_text(lesson.sentence), m, lesson.categories);
                m->store(d.atom, d.metadata, larry::Status::Proposed, "lesson:" + lesson_file.filename().string());
            }
        }
        return m;
    }();
    return *instance;
}

std::string category_of(const larry::Description& d, std::size_t i) {
    return {d.entities.entities[i].category.begin(), d.entities.entities[i].category.end()};
}

}  // namespace

TEST(the_portuguese_rules_load) {
    CHECK(rules().categories().size() == 13);
    CHECK(std::ranges::contains(rules().question_words(), b("quem")));
    CHECK(std::ranges::contains(rules().negation_words(), b("não")));
    CHECK(std::ranges::contains(rules().conjunctions(), b("e")));
    CHECK(!rules().number_words().empty());
    CHECK(!rules().states().empty());
    CHECK(larry::locale(larry::Language::Portuguese) == "pt");
    CHECK(larry::language_named("pt") == larry::Language::Portuguese);
    CHECK(!larry::language_named("xx").has_value());
}

TEST(entities_and_sentences_in_portuguese) {
    const larry::AtomOperations ops;
    int failed = 0;
    for (const Case& c : suite("entities.txt")) {
        std::vector<std::string> got;
        for (const larry::Entity& e : assimilation().entities(ops.from_text(c.input)).entities) {
            got.emplace_back(e.word.begin(), e.word.end());
        }
        if (got != c.expected) {
            ++failed;
            std::println("entities.txt (pt) line {}: \"{}\" gave {} entities, expected {}", c.line, c.input, got.size(),
                         c.expected.size());
        }
    }
    for (const Case& c : suite("sentences.txt")) {
        std::vector<std::string> got;
        for (const larry::Sentence& s : assimilation().sentences(c.input)) {
            got.emplace_back(ops.text(s));
        }
        if (got != c.expected) {
            ++failed;
            std::println("sentences.txt (pt) line {}: \"{}\" gave {} sentences, expected {}", c.line, c.input,
                         got.size(), c.expected.size());
        }
    }
    CHECK(failed == 0);
}

TEST(the_lessons_teach_and_memory_describes) {
    const larry::AtomOperations ops;
    larry::Memory& m = memory();
    CHECK(m.count() == 21);
    // A2: a new sentence of taught words is described from memory.
    const larry::Description d = assimilation().describe(ops.from_text("O mar é azul."), &m);
    CHECK(category_of(d, 0) == "determiner");
    CHECK(category_of(d, 1) == "noun");
    CHECK(category_of(d, 2) == "auxiliary verb");
    CHECK(category_of(d, 3) == "adjective");
    CHECK(d.notes[1].source == larry::Source::Memory);
    // A3: the qualification, by the Portuguese rule files.
    const larry::Cognition cognition;
    const auto qualify = [&](std::string_view text) {
        const larry::Description q = assimilation().describe(ops.from_text(text), &m);
        return cognition.qualify(q.atom, q.entities, rules());
    };
    CHECK(qualify("O céu é azul.") == larry::Qualification::Affirmation);
    CHECK(qualify("O céu está azul?") == larry::Qualification::Question);
    CHECK(qualify("Quem fechou a porta?") == larry::Qualification::Question);
    CHECK(qualify("Fecha a porta.") == larry::Qualification::Order);
    CHECK(qualify("Olá!") == larry::Qualification::Expression);
    CHECK(qualify("Suponha que o céu é verde.") == larry::Qualification::Assumption);
    // A6a: an unknown word takes the category of the words around it, as a guess.
    const larry::Description guess = assimilation().describe(ops.from_text("O zorp é azul."), &m);
    CHECK(category_of(guess, 1) == "noun");
    CHECK(guess.notes[1].source == larry::Source::Guess);
    // A4: a plural by the ending, the base known.
    const std::optional<larry::Form> gatos = assimilation().form_of(b("gatos"), &m);
    CHECK(gatos.has_value());
    if (gatos) {
        CHECK(gatos->base == b("gato"));
        CHECK(gatos->feature == b("plural"));
    }
    CHECK(assimilation().form_of(b("foi"), &m)->base == b("ir"));
    // A7: the features from the Portuguese endings, auxiliaries and pronouns.
    const larry::Description ela = assimilation().describe(ops.from_text("Ela é alta."), &m);
    CHECK(std::ranges::contains(ela.entities.entities[0].types, b("third person")));
    CHECK(std::ranges::contains(ela.entities.entities[1].types, b("present")));
}

TEST(the_brain_thinks_in_portuguese_rules) {
    const larry::AtomOperations ops;
    const std::filesystem::path file = std::filesystem::temp_directory_path() / "larry_test_pt_brain.atoms";
    std::filesystem::remove(file);
    larry::Memory cache{file};
    larry::Brain brain{rules(), cache};
    for (const larry::StoredAtom& atom : memory().all()) {
        (void)brain.remember(atom.description, atom.status, "lesson:pt");  // the bonds of A4 and A10 come with it
    }
    // R1: true, false, unknown, by the Portuguese negation words.
    CHECK(brain.answer(ops.from_text("O céu é azul.")).text == "verdadeiro");
    CHECK(brain.answer(ops.from_text("O céu não é azul.")).text == "falso");
    CHECK(brain.answer(ops.from_text("Os pinguins voam.")).text == "falso");
    // K1: an exclusive attribute from exclusives.txt.
    CHECK(brain.answer(ops.from_text("O céu é verde.")).text == "falso");
    // A10 and R4: the defining sentences of the lesson bond the kinds, and the chain answers.
    CHECK(cache.bonds_from(larry::BondEnd::entity("pardal")).size() >= 1);
    CHECK(brain.chain(b("pardal"), b("animal")) == (std::vector<Bytes>{b("pardal"), b("pássaro"), b("animal")}));
    CHECK(brain.answer(ops.from_text("Um pardal é um animal?")).text == "Sim.");
    // M1: arithmetic in Portuguese words, from arithmetic.txt and number_words.txt.
    CHECK(brain.answer(ops.from_text("Quanto é dois mais três?")).text == "5");
    CHECK(brain.answer(ops.from_text("Quanto é dez vezes dez?")).text == "100");
    // G3: an affirmation is heard and stored; a new word is asked about.
    const larry::Reply heard = brain.hear(ops.from_text("O mar é verde."), "user:pedro");
    CHECK(heard.stored);
    CHECK(cache.count() == 22);
    // A11 with gender (Q37): "ela" takes the latest feminine name and "ele" the latest
    // masculine one, from references.txt and names.txt.
    (void)brain.hear(ops.from_text("A Maria foi ao jardim."), "user:pedro");
    (void)brain.hear(ops.from_text("O João foi ao escritório."), "user:pedro");
    const larry::Reply ela = brain.hear(ops.from_text("Ela foi à cozinha."), "user:pedro");
    CHECK(std::ranges::contains(ela.because, std::string{"read as: A Maria foi à cozinha."}));
    const larry::Reply ele = brain.hear(ops.from_text("Ele está cansado."), "user:pedro");
    CHECK(std::ranges::contains(ele.because, std::string{"read as: O João está cansado."}));
    // A9: the image from the Portuguese files: the article dropped, "é" as "ser".
    const larry::Description azul = assimilation().describe(ops.from_text("O céu é azul."), &cache);
    CHECK(std::string(azul.image.bytes.begin(), azul.image.bytes.end()) ==
          "affirmation | subject: céu | predicate: ser | attribute: azul");
    CHECK(larry::Cognition{}.same_meaning(azul, assimilation().describe(ops.from_text("Céu é azul."), &cache)).holds);
    // G1: back from the image with the Portuguese files: the article memory saw,
    // "ser" as "é", and the negation word before the predicate.
    CHECK(assimilation().sentence_of(azul.image, &cache) == "O céu é azul.");
    const larry::Description nao = assimilation().describe(ops.from_text("O céu não é azul."), &cache);
    CHECK(assimilation().sentence_of(nao.image, &cache) == "O céu não é azul.");
    // S1: the goal of an order from the Portuguese goals.txt.
    CHECK(brain.goal_of(assimilation().describe(ops.from_text("Fecha a porta."), &cache)) ==
          std::optional<std::string>{"A porta está fechada."});
    // G7 (first step): word by word on the image, said by the other constellation.
    const larry::BaseRules english{larry::Language::English};
    const larry::Assimilation en{english};
    const std::filesystem::path en_file = std::filesystem::temp_directory_path() / "larry_test_pt_en.atoms";
    std::filesystem::remove(en_file);
    larry::Memory en_cache{en_file};
    {
        const std::vector<Bytes> taught = {b("determiner"), b("noun"), b("auxiliary verb"), b("adjective")};
        const larry::Description d = en.describe(ops.from_text("The sky is blue."), &en_cache, taught);
        en_cache.store(d.atom, d.metadata, larry::Status::Proposed, "lesson:test");
    }
    const larry::ImageElectron to_pt = en.translate(en.describe(ops.from_text("The sky is blue."), &en_cache).image,
                                                    english.translations("pt"));
    CHECK(std::string(to_pt.bytes.begin(), to_pt.bytes.end()) == "affirmation | subject: céu | predicate: ser | attribute: azul");
    CHECK(assimilation().sentence_of(to_pt, &cache) == "O céu é azul.");
    std::vector<Bytes> missing;
    const larry::ImageElectron to_en = assimilation().translate(azul.image, rules().translations("en"), &missing);
    CHECK(en.sentence_of(to_en, &en_cache) == "The sky is blue.");
    CHECK(missing.empty());
}

int main() {
    return larry::test::run();
}
