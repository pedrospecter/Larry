// Tests for Arithmetic: calculations as cognition (M1).

#include "larry/arithmetic.hpp"

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

using larry::Arithmetic;
using larry::Calculation;

namespace {

const larry::BaseRules& rules() {
    static const larry::BaseRules instance{larry::Language::English};
    return instance;
}

const Arithmetic& arithmetic() {
    static const Arithmetic instance{rules()};
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

TEST(numbers_are_written_plainly) {
    CHECK(Arithmetic::number(2) == "2");
    CHECK(Arithmetic::number(3.5L) == "3.5");
    CHECK(Arithmetic::number(0.1L) == "0.1");
    CHECK(Arithmetic::number(-1) == "-1");
    CHECK(Arithmetic::number(1000000) == "1000000");
    CHECK(Arithmetic::number(1.0L / 3) == "0.333333");
}

TEST(a_calculation_names_its_rule) {
    const std::optional<Calculation> sum = arithmetic().calculate("What is two plus three?");
    CHECK(sum.has_value());
    if (sum) {
        CHECK(sum->expression == "2 + 3");
        CHECK(sum->result == "5");
        CHECK(!sum->comparison);
        CHECK(sum->rule() == "arithmetic: 2 + 3 = 5");
    }
    const std::optional<Calculation> compare = arithmetic().calculate("Is 5 bigger than 3?");
    CHECK(compare.has_value());
    if (compare) {
        CHECK(compare->comparison);
        CHECK(compare->holds);
        CHECK(compare->expression == "5 > 3");
        CHECK(compare->rule() == "arithmetic: 5 > 3 holds");
    }
    const std::optional<Calculation> zero = arithmetic().calculate("What is 1 divided by 0?");
    CHECK(zero.has_value());
    if (zero) {
        CHECK(!zero->defined);
        CHECK(zero->rule() == "arithmetic: 1 / 0 is undefined: division by zero");
    }
    CHECK(!arithmetic().calculate("The sky is blue.").has_value());
    CHECK(!arithmetic().calculate("").has_value());
    CHECK(!arithmetic().calculate("2 +").has_value());
    CHECK(!arithmetic().calculate("(2 + 3").has_value());
}

TEST(the_suite_passes) {
    const std::filesystem::path suite = std::filesystem::path{LARRY_TEST_DATA_DIR} / "en" / "arithmetic.txt";
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
        const std::optional<Calculation> calc = arithmetic().calculate(question);
        const std::string got = !calc ? "none" : !calc->defined ? "undefined" : calc->result;
        if (got != expected) {
            ++failed;
            std::println(stderr, "arithmetic.txt line {}: \"{}\" expected {}, got {}{}", number, question, expected, got,
                         calc ? " (" + calc->expression + ")" : "");
        }
    }
    CHECK(cases >= 30);
    CHECK(failed == 0);
}

TEST(the_brain_calculates_and_stores_nothing) {
    const std::filesystem::path file =
        std::filesystem::temp_directory_path() / "larry_test_arithmetic_brain.atoms";
    std::filesystem::remove(file);
    larry::Memory cache{file};
    larry::Brain brain{rules(), cache};
    const larry::AtomOperations ops;
    const larry::Reply sum = brain.hear(ops.from_text("What is 1+1?"), "user:pedro");
    CHECK(sum.text == "2");
    CHECK(sum.because == (std::vector<std::string>{"rule: arithmetic: 1 + 1 = 2"}));
    CHECK(!sum.stored);
    CHECK(cache.count() == 0);
    CHECK(brain.hear(ops.from_text("Is 2 plus 2 five?"), "user:pedro").text == "No.");
    CHECK(brain.answer(ops.from_text("Is 2 plus 2 four?")).text == "true");
    CHECK(brain.answer(ops.from_text("seven times eight")).text == "56");
    CHECK(brain.hear(ops.from_text("What is 1 divided by 0?"), "user:pedro").text == "That is undefined: division by zero.");
    // A sentence with numbers that is not arithmetic goes its usual way.
    CHECK(brain.hear(ops.from_text("Tom has 3 apples."), "user:pedro").text.starts_with("Noted."));
    CHECK(cache.count() == 1);
}

int main() {
    return larry::test::run();
}
