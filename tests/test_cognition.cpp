// Tests for Cognition: categorizing and qualifying.

#include "larry/cognition.hpp"

#include "larry/assimilation.hpp"
#include "larry/atom_operations.hpp"
#include "larry/base_rules.hpp"
#include "larry/description.hpp"

#include "check.hpp"

#include <filesystem>
#include <fstream>
#include <print>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using larry::Bytes;
using larry::Cognition;
using larry::EntitiesElectron;
using larry::Entity;

namespace {

Bytes b(std::string_view text) {
    return Bytes(text.begin(), text.end());
}

EntitiesElectron words(std::vector<std::string_view> list) {
    EntitiesElectron out;
    for (const std::string_view word : list) {
        out.entities.push_back(Entity{.word = b(word), .category = {}, .types = {}});
    }
    return out;
}

std::vector<Bytes> categories(std::vector<std::string_view> list) {
    std::vector<Bytes> out;
    for (const std::string_view item : list) {
        out.push_back(b(item));
    }
    return out;
}

const larry::BaseRules& rules() {
    static const larry::BaseRules instance{larry::Language::English};
    return instance;
}

}  // namespace

TEST(categorize_stores_each_category) {
    const Cognition cognition;
    EntitiesElectron entities = words({"the", "sky", "is", "blue"});
    cognition.categorize(entities, categories({"determiner", "noun", "auxiliary verb", "adjective"}),
                         rules());
    CHECK(entities.entities[0].category == b("determiner"));
    CHECK(entities.entities[1].category == b("noun"));
    CHECK(entities.entities[2].category == b("auxiliary verb"));
    CHECK(entities.entities[3].category == b("adjective"));
}

TEST(categorize_rejects_the_wrong_count) {
    const Cognition cognition;
    EntitiesElectron entities = words({"the", "sky"});
    CHECK_THROWS(cognition.categorize(entities, categories({"determiner"}), rules()),
                 std::invalid_argument);
    CHECK_THROWS(cognition.categorize(entities, categories({"determiner", "noun", "noun"}), rules()),
                 std::invalid_argument);
    CHECK(entities.entities[0].category.empty());
}

TEST(categorize_rejects_an_unknown_category) {
    const Cognition cognition;
    EntitiesElectron entities = words({"the", "sky"});
    CHECK_THROWS(cognition.categorize(entities, categories({"determiner", "colour"}), rules()),
                 std::invalid_argument);
    CHECK_THROWS(cognition.categorize(entities, categories({"Determiner", "noun"}), rules()),
                 std::invalid_argument);
    CHECK(entities.entities[0].category.empty());
    CHECK(entities.entities[1].category.empty());
}

TEST(qualification_names) {
    CHECK(larry::name(larry::Qualification::Affirmation) == "affirmation");
    CHECK(larry::name(larry::Qualification::Question) == "question");
    CHECK(larry::name(larry::Qualification::Order) == "order");
    CHECK(larry::name(larry::Qualification::Assumption) == "assumption");
    CHECK(larry::name(larry::Qualification::Expression) == "expression");
}

namespace {

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) {
        s.remove_suffix(1);
    }
    return s;
}

std::vector<std::string> fields(std::string_view line) {
    std::vector<std::string> out;
    if (line.ends_with(" |")) {
        line.remove_suffix(2);  // an empty last field
    }
    while (true) {
        const std::size_t next = line.find(" | ");
        out.emplace_back(trim(line.substr(0, next)));
        if (next == std::string_view::npos) {
            break;
        }
        line.remove_prefix(next + 3);
    }
    if (out.size() > 1 && out.back().empty()) {
        out.pop_back();
    }
    return out;
}

std::vector<Bytes> split_categories(std::string_view text) {
    std::vector<Bytes> out;
    while (true) {
        const std::size_t comma = text.find(',');
        const std::string_view item = trim(text.substr(0, comma));
        out.emplace_back(item.begin(), item.end());
        if (comma == std::string_view::npos) {
            break;
        }
        text.remove_prefix(comma + 1);
    }
    return out;
}

larry::Qualification qualify(std::string_view sentence, std::string_view categories = "") {
    static const larry::Assimilation assimilation{rules()};
    const larry::AtomOperations ops;
    const Cognition cognition;
    const larry::Sentence atom = ops.from_text(sentence);
    EntitiesElectron entities = assimilation.entities(atom);
    if (!categories.empty()) {
        cognition.categorize(entities, split_categories(categories), rules());
    }
    return cognition.qualify(atom, entities, rules());
}

}  // namespace

TEST(qualify_by_the_proposed_rules) {
    using larry::Qualification;
    CHECK(qualify("The sky is blue.") == Qualification::Affirmation);
    CHECK(qualify("Is the sky blue?") == Qualification::Question);
    CHECK(qualify("Is the sky blue", "auxiliary verb, determiner, noun, adjective") ==
          Qualification::Question);
    CHECK(qualify("What is the sky?") == Qualification::Question);
    CHECK(qualify("The sky is blue?") == Qualification::Question);
    CHECK(qualify("Close the door.", "verb, determiner, noun") == Qualification::Order);
    CHECK(qualify("Please close the door.", "interjection, verb, determiner, noun") ==
          Qualification::Order);
    CHECK(qualify("Close the door.") == Qualification::Affirmation);  // no category: no order
    CHECK(qualify("If it rains, the street is wet.") == Qualification::Assumption);
    CHECK(qualify("Maybe.") == Qualification::Assumption);
    CHECK(qualify("What if it rains?") == Qualification::Question);  // "?" comes first
    CHECK(qualify("Hello!") == Qualification::Expression);
    CHECK(qualify("Thank you very much.") == Qualification::Expression);
    CHECK(qualify("Oh no.", "interjection, interjection") == Qualification::Expression);
    CHECK(qualify("...") == Qualification::Expression);  // no entity at all
    CHECK(qualify("Hello?") == Qualification::Question);
    CHECK(qualify("Hello is a greeting.", "noun, auxiliary verb, determiner, noun") ==
          Qualification::Affirmation);
}

TEST(qualification_suite) {
    const std::filesystem::path file =
        std::filesystem::path{LARRY_TEST_DATA_DIR} / "en" / "qualification.txt";
    std::ifstream in{file, std::ios::binary};
    CHECK(static_cast<bool>(in));
    std::size_t cases = 0;
    std::size_t failed = 0;
    std::size_t number = 0;
    for (std::string line; std::getline(in, line);) {
        ++number;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty() || line.front() == '#') {
            continue;
        }
        const std::vector<std::string> f = fields(line);
        CHECK(f.size() == 2 || f.size() == 3);
        if (f.size() < 2) {
            continue;
        }
        ++cases;
        std::string got;
        try {
            got = larry::name(qualify(f[1], f.size() == 3 ? f[2] : ""));
        } catch (const std::exception& e) {
            got = e.what();
        }
        if (got != f[0]) {
            ++failed;
            std::println(stderr, "qualification.txt line {}: \"{}\" expected {}, got {}", number,
                         f[1], f[0], got);
        }
    }
    CHECK(cases >= 300);
    CHECK(failed == 0);
}

namespace {

larry::Description describe(std::string_view sentence, std::string_view categories) {
    static const larry::Assimilation assimilation{rules()};
    const larry::AtomOperations ops;
    std::vector<Bytes> taught;
    if (!trim(categories).empty()) {
        taught = split_categories(categories);
    }
    return assimilation.describe(ops.from_text(sentence), nullptr, taught);
}

std::string text(const Bytes& bytes) {
    return {bytes.begin(), bytes.end()};
}

// Runs one comparison suite: see tests/comparisons/README.md.
void run_suite(std::string_view file_name, larry::ComparisonKind kind,
               larry::Comparison (Cognition::*compare)(const larry::Description&,
                                                      const larry::Description&) const) {
    const std::filesystem::path file =
        std::filesystem::path{LARRY_TEST_DATA_DIR} / ".." / "comparisons" / file_name;
    std::ifstream in{file, std::ios::binary};
    CHECK(static_cast<bool>(in));
    const Cognition cognition;
    std::size_t cases = 0;
    std::size_t failed = 0;
    std::size_t number = 0;
    for (std::string line; std::getline(in, line);) {
        ++number;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty() || line.front() == '#') {
            continue;
        }
        std::vector<std::string> f = fields(line);
        f.resize(5);
        ++cases;
        std::string got;
        try {
            const larry::Description a = describe(f[1], f[3]);
            const larry::Description b = describe(f[2], f[4]);
            const larry::Comparison result = (cognition.*compare)(a, b);
            CHECK(result.kind == kind);
            got = f[0].starts_with('C') ? text(result.bytes()) : (result.holds ? "yes" : "no");
        } catch (const std::exception& e) {
            got = e.what();
        }
        if (got != f[0]) {
            ++failed;
            std::println(stderr, "{} line {}: \"{}\" / \"{}\"\n  expected {}\n  got      {}",
                         file_name, number, f[1], f[2], f[0], got);
        }
    }
    CHECK(cases >= 10);
    CHECK(failed == 0);
}

}  // namespace

TEST(c1_identity) {
    run_suite("c1_identity.txt", larry::ComparisonKind::Identity, &Cognition::identity);
}

TEST(c2_same_form) {
    run_suite("c2_same_form.txt", larry::ComparisonKind::SameForm, &Cognition::same_form);
}

TEST(c3_alignment) {
    run_suite("c3_alignment.txt", larry::ComparisonKind::Alignment, &Cognition::align);
}

TEST(c4_difference) {
    run_suite("c4_difference.txt", larry::ComparisonKind::Difference, &Cognition::difference);
}

TEST(c5_same_structure) {
    run_suite("c5_same_structure.txt", larry::ComparisonKind::SameStructure,
              &Cognition::same_structure);
}

TEST(comparison_results_as_bytes) {
    // The result as bytes is deterministic and names the kind, the answer,
    // the matches, what matched nothing, and the pattern.
    const larry::Description a = describe("the sky is blue", "determiner, noun, auxiliary verb, adjective");
    const larry::Description b = describe("the sea is blue", "determiner, noun, auxiliary verb, adjective");
    const Cognition cognition;
    CHECK(text(cognition.identity(a, b).bytes()) == "C1 no 0=0 1~1 2=2 3=3");
    CHECK(text(cognition.same_form(a, b).bytes()) == "C2 no 0=0 1~1 2=2 3=3");
    CHECK(text(cognition.align(a, b).bytes()) == "C3 yes 0=0 1~1 2=2 3=3");
    CHECK(text(cognition.difference(a, b).bytes()) == "C4 yes 0=0 1~1 2=2 3=3 pattern:the [noun] is blue");
    CHECK(text(cognition.same_structure(a, b).bytes()) == "C5 yes 0=0 1~1 2=2 3=3");
    CHECK(text(cognition.difference(a, b).bytes()) == text(cognition.difference(a, b).bytes()));
}

int main() {
    return larry::test::run();
}
