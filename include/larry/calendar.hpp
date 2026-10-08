#pragma once

#include "larry/arithmetic.hpp"
#include "larry/base_rules.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace larry {

/// A date of the civil calendar.
struct CivilDate {
    int year = 1970;
    unsigned month = 1;
    unsigned day = 1;
};

/// Dates as cognition (M2): a question about a date is answered by the
/// civil calendar, with no library: the day of the week of a date, the days
/// between two dates, a date some days or weeks before or after another,
/// whether one date is before another, and today's date. The months, the
/// days of the week and the frame words are data in dates.txt.
class Calendar {
public:
    explicit Calendar(const BaseRules& rules);

    /// The answer the text asks for, or nothing when the text is not about
    /// dates. A result that is a date reads "Thursday, 8 October 2026".
    [[nodiscard]] std::optional<Calculation> calculate(std::string_view text) const;

    /// Days since 1970-01-01 of a civil date, and back.
    [[nodiscard]] static std::int64_t days_from_civil(int year, unsigned month, unsigned day) noexcept;
    [[nodiscard]] static CivilDate civil_from_days(std::int64_t days) noexcept;
    /// The day of the week of a day count: 0 is Sunday.
    [[nodiscard]] static unsigned weekday_of(std::int64_t days) noexcept;
    [[nodiscard]] static bool valid(int year, unsigned month, unsigned day) noexcept;

    /// Today in UTC, as days since 1970-01-01.
    [[nodiscard]] static std::int64_t today() noexcept;

    /// A date as Larry writes it: "Thursday, 8 October 2026".
    [[nodiscard]] std::string written(std::int64_t days) const;
    [[nodiscard]] std::string weekday_name(std::int64_t days) const;
    [[nodiscard]] std::string month_name(unsigned month) const;

    /// A date read from words at `at`: "2026-10-08", "8 October 2026",
    /// "October 8, 2026", "today"; `at` moves past it.
    [[nodiscard]] std::optional<std::int64_t> read_date(const std::vector<std::string>& words,
                                                        std::size_t& at) const;

private:
    const BaseRules* rules_;
    std::vector<std::pair<std::string, unsigned>> months_;
    std::vector<std::string> weekdays_;  ///< Index 0 is Sunday.
    std::vector<std::string> frame_;
    std::vector<std::pair<std::string, std::int64_t>> units_;  ///< "weeks" and 7.
};

}  // namespace larry
