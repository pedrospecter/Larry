// Tests for Algebra: equations and expressions in one unknown (M3).

#include "larry/algebra.hpp"

#include "larry/atom_operations.hpp"
#include "larry/base_rules.hpp"
#include "larry/brain.hpp"
#include "larry/memory.hpp"

#include "check.hpp"

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <optional>
#include <print>
#include <string>
#include <string_view>

using larry::Algebra;
using larry::Calculation;
using larry::Poly;

namespace {

const larry::BaseRules& rules() {
    static const larry::BaseRules instance{larry::Language::English};
    return instance;
}

const Algebra& algebra() {
    static const Algebra instance{rules()};
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

}  // namespace

TEST(polynomials_are_written_plainly) {
    CHECK((Poly{{2, 3, 1}}.written() == "x^2 + 3x + 2"));
    CHECK((Poly{{0, 1, 0}}.written() == "x"));
    CHECK((Poly{{0, -1, 0}}.written() == "-x"));
    CHECK((Poly{{7, -1, 0}}.written() == "-x + 7"));
    CHECK((Poly{{0, 0, 0}}.written() == "0"));
    CHECK((Poly{{2.5, 0, 2}}.written() == "2x^2 + 2.5"));
    CHECK((Poly{{2, 3, 1}}.degree() == 2));
    CHECK((Poly{{2, 0, 0}}.degree() == 0));
    CHECK((Poly{{0, 0, 0}}.degree() == -1));
    CHECK((Poly{{1, 2, 3}}.at(2) == 17));
}

TEST(an_equation_names_its_rule) {
    const std::optional<Calculation> solved = algebra().calculate("Solve 2x + 3 = 11.");
    CHECK(solved.has_value());
    if (solved) {
        CHECK(solved->expression == "algebra: 2x + 3 = 11");
        CHECK(solved->result == "x = 4");
        CHECK(solved->rule() == "algebra: 2x + 3 = 11 gives x = 4");
    }
    const std::optional<Calculation> simplified = algebra().calculate("Simplify 2x + 3x + 2.");
    CHECK(simplified.has_value());
    if (simplified) {
        CHECK(simplified->rule() == "algebra: Simplify 2x + 3x + 2 is 5x + 2");
    }
    const std::optional<Calculation> valued = algebra().calculate("What is 2x + 3 when x = 4?");
    CHECK(valued.has_value());
    if (valued) {
        CHECK(valued->rule() == "algebra: 2x + 3 with x = 4 = 11");
    }
    CHECK(!algebra().calculate("What is 3 x 4?").has_value());
    CHECK(!algebra().calculate("x").has_value() || algebra().calculate("x")->result == "x");
    CHECK(!algebra().calculate("Solve x^3 = 8.").has_value());
    CHECK(!algebra().calculate("Solve 2x + = 3.").has_value());
    CHECK(!algebra().calculate("Solve 2x + 3 = 11 = 12.").has_value());
    const std::optional<Calculation> zero = algebra().calculate("Solve x / 0 = 1.");
    CHECK(zero.has_value() && !zero->defined);
}

TEST(the_suite_passes) {
    const std::filesystem::path suite = std::filesystem::path{LARRY_TEST_DATA_DIR} / "en" / "algebra.txt";
    std::ifstream in{suite, std::ios::binary};
    CHECK(static_cast<bool>(in));
    std::size_t cases = 0;
    std::size_t failed = 0;
    std::size_t number = 0;
    for (std::string line; std::getline(in, line);) {
        ++number;
        const std::string_view text = trim(line);
        if (text.empty() || text.front() == '#') {
            continue;
        }
        const std::size_t separator = text.find(" | ");
        CHECK(separator != std::string_view::npos);
        if (separator == std::string_view::npos) {
            continue;
        }
        const std::string expected{trim(text.substr(0, separator))};
        const std::string question{trim(text.substr(separator + 3))};
        ++cases;
        const std::optional<Calculation> calc = algebra().calculate(question);
        const std::string got = !calc ? "none" : calc->result;
        if (got != expected) {
            ++failed;
            std::println(stderr, "algebra.txt line {}: \"{}\" expected {}, got {}{}", number, question, expected, got,
                         calc ? " (" + calc->expression + ")" : "");
        }
    }
    CHECK(cases >= 25);
    CHECK(failed == 0);
}

TEST(the_brain_solves_and_stores_nothing) {
    const std::filesystem::path file = std::filesystem::temp_directory_path() / "larry_test_algebra_brain.atoms";
    std::filesystem::remove(file);
    larry::Memory cache{file};
    larry::Brain brain{rules(), cache};
    const larry::AtomOperations ops;
    const larry::Reply solved = brain.hear(ops.from_text("Solve 2x + 3 = 11."), "user:pedro");
    CHECK(solved.text == "x = 4");
    CHECK(solved.because == (std::vector<std::string>{"rule: algebra: 2x + 3 = 11 gives x = 4"}));
    CHECK(cache.count() == 0);
    CHECK(brain.answer(ops.from_text("What is 3 x 4?")).text == "12");  // the times sign, not the unknown
    CHECK(brain.answer(ops.from_text("Simplify x + x.")).text == "2x");
}

int main() {
    return larry::test::run();
}
