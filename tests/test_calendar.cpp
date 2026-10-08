// Tests for Calendar: dates as cognition (M2).

#include "larry/calendar.hpp"

#include "larry/base_rules.hpp"

#include "check.hpp"

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <optional>
#include <print>
#include <string>
#include <string_view>

using larry::Calendar;
using larry::Calculation;
using larry::CivilDate;

namespace {

const larry::BaseRules& rules() {
    static const larry::BaseRules instance{larry::Language::English};
    return instance;
}

const Calendar& calendar() {
    static const Calendar instance{rules()};
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

TEST(the_civil_calendar_counts_days) {
    CHECK(Calendar::days_from_civil(1970, 1, 1) == 0);
    CHECK(Calendar::days_from_civil(1970, 1, 2) == 1);
    CHECK(Calendar::days_from_civil(1969, 12, 31) == -1);
    CHECK(Calendar::days_from_civil(2000, 3, 1) - Calendar::days_from_civil(2000, 2, 28) == 2);  // a leap day
    CHECK(Calendar::days_from_civil(1900, 3, 1) - Calendar::days_from_civil(1900, 2, 28) == 1);  // no leap day
    for (const std::int64_t days : {0LL, 1LL, -1LL, 20000LL, -20000LL, 1000000LL}) {
        const CivilDate d = Calendar::civil_from_days(days);
        CHECK(Calendar::days_from_civil(d.year, d.month, d.day) == days);
    }
    CHECK(Calendar::weekday_of(Calendar::days_from_civil(1970, 1, 1)) == 4);  // Thursday
    CHECK(Calendar::weekday_of(Calendar::days_from_civil(2000, 1, 1)) == 6);  // Saturday
    CHECK(Calendar::weekday_of(Calendar::days_from_civil(1969, 12, 28)) == 0);  // Sunday
    CHECK(Calendar::valid(2024, 2, 29));
    CHECK(!Calendar::valid(2023, 2, 29));
    CHECK(!Calendar::valid(2026, 13, 1));
    CHECK(!Calendar::valid(2026, 4, 31));
    CHECK(calendar().weekday_name(Calendar::days_from_civil(2026, 10, 8)) == "Thursday");
    CHECK(calendar().written(Calendar::days_from_civil(2026, 10, 8)) == "Thursday, 8 October 2026");
    CHECK(calendar().month_name(2) == "February");
    CHECK(Calendar::today() > Calendar::days_from_civil(2026, 1, 1));
}

TEST(dates_are_read_in_three_forms) {
    const auto read = [](std::vector<std::string> words) -> std::optional<std::int64_t> {
        std::size_t at = 0;
        const std::optional<std::int64_t> date = calendar().read_date(words, at);
        if (date && at != words.size()) {
            return std::nullopt;
        }
        return date;
    };
    const std::int64_t expected = Calendar::days_from_civil(2026, 10, 8);
    CHECK(read({"2026-10-08"}) == expected);
    CHECK(read({"8", "october", "2026"}) == expected);
    CHECK(read({"8th", "october", "2026"}) == expected);
    CHECK(read({"october", "8", "2026"}) == expected);
    CHECK(read({"oct", "8", "2026"}) == expected);
    CHECK(!read({"2026-13-08"}).has_value());
    CHECK(!read({"32", "october", "2026"}).has_value());
    CHECK(!read({"sky"}).has_value());
    CHECK(read({"today"}) == Calendar::today());
}

TEST(the_suite_passes) {
    const std::filesystem::path suite = std::filesystem::path{LARRY_TEST_DATA_DIR} / "en" / "dates.txt";
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
        const std::optional<Calculation> calc = calendar().calculate(question);
        const std::string got = !calc ? "none" : calc->result;
        if (got != expected) {
            ++failed;
            std::println(stderr, "dates.txt line {}: \"{}\" expected {}, got {}{}", number, question, expected, got,
                         calc ? " (" + calc->expression + ")" : "");
        }
        if (calc) {
            CHECK(calc->rule().starts_with("calendar: "));
        }
    }
    CHECK(cases >= 18);
    CHECK(failed == 0);
    // Today's questions answer with today.
    const std::optional<Calculation> today = calendar().calculate("What is today's date?");
    CHECK(today.has_value());
    if (today) {
        CHECK(today->result == calendar().written(Calendar::today()));
    }
    const std::optional<Calculation> day = calendar().calculate("What day is it today?");
    CHECK(day.has_value() && day->result == calendar().weekday_name(Calendar::today()));
    const std::optional<Calculation> in_ten = calendar().calculate("What date is it in ten days?");
    CHECK(in_ten.has_value() && in_ten->result == calendar().written(Calendar::today() + 10));
}

int main() {
    return larry::test::run();
}
