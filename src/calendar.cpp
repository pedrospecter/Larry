#include "larry/calendar.hpp"

#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace larry {

namespace {

std::string text_of(const Bytes& bytes) {
    return std::string(bytes.begin(), bytes.end());
}

std::vector<std::string> split(std::string_view text, char separator) {
    std::vector<std::string> out;
    std::string current;
    for (const char c : text) {
        if (c == separator) {
            if (!current.empty()) {
                out.push_back(std::move(current));
                current.clear();
            }
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty()) {
        out.push_back(std::move(current));
    }
    return out;
}

// The words in lower case, with the marks at their ends stripped; "2026-10-08"
// stays one word.
std::vector<std::string> words_of(std::string_view text) {
    std::vector<std::string> out;
    for (std::string word : split(text, ' ')) {
        for (char& c : word) {
            if (c >= 'A' && c <= 'Z') {
                c = static_cast<char>(c - 'A' + 'a');
            }
        }
        const auto is_mark = [](char c) {
            return c == '?' || c == '!' || c == ',' || c == '.' || c == ';' || c == ':' || c == '"' || c == '\'' ||
                   c == '(' || c == ')' || c == '\n' || c == '\r' || c == '\t';
        };
        while (!word.empty() && is_mark(word.front())) {
            word.erase(word.begin());
        }
        while (!word.empty() && is_mark(word.back())) {
            word.pop_back();
        }
        if (word.ends_with("'s") || word.ends_with("\xE2\x80\x99s")) {
            word.erase(word.size() - (word.ends_with("'s") ? 2 : 4));  // "today's" is "today"
        }
        if (!word.empty()) {
            out.push_back(std::move(word));
        }
    }
    return out;
}

bool all_digits(std::string_view s) {
    return !s.empty() && std::ranges::all_of(s, [](char c) { return c >= '0' && c <= '9'; });
}

// A day number with an ending: "8", "8th", "1st", "22nd", "3rd".
std::optional<unsigned> day_of(std::string_view word) {
    std::string_view digits = word;
    for (const char* ending : {"st", "nd", "rd", "th"}) {
        if (digits.size() > 2 && digits.ends_with(ending)) {
            digits.remove_suffix(2);
            break;
        }
    }
    if (!all_digits(digits) || digits.size() > 2) {
        return std::nullopt;
    }
    const unsigned day = static_cast<unsigned>(std::atoi(std::string{digits}.c_str()));
    return day >= 1 && day <= 31 ? std::optional<unsigned>{day} : std::nullopt;
}

std::optional<int> year_of(std::string_view word) {
    if (!all_digits(word) || word.size() != 4) {
        return std::nullopt;
    }
    return std::atoi(std::string{word}.c_str());
}

bool has(const std::vector<std::string>& words, std::string_view word) {
    return std::ranges::contains(words, std::string{word});
}

}  // namespace

Calendar::Calendar(const BaseRules& rules) : rules_(&rules) {
    weekdays_.assign(7, "");
    for (const auto& [key, value] : rules.dates()) {
        const std::string k = text_of(key);
        const std::string v = text_of(value);
        if (k.starts_with("month ")) {
            months_.emplace_back(k.substr(6), static_cast<unsigned>(std::atoi(v.c_str())));
        } else if (k.starts_with("weekday ")) {
            const int index = std::atoi(k.substr(8).c_str());
            if (index >= 0 && index < 7) {
                weekdays_[static_cast<std::size_t>(index)] = v;
            }
        } else if (k == "frame") {
            for (const std::string& f : split(v, ';')) {
                frame_.push_back(f);
            }
        } else if (k.starts_with("unit ")) {
            units_.emplace_back(k.substr(5), std::atoll(v.c_str()));
        }
    }
}

std::int64_t Calendar::days_from_civil(int y, unsigned m, unsigned d) noexcept {
    // Howard Hinnant's algorithm: days since 1970-01-01 of a proleptic Gregorian date.
    y -= m <= 2;
    const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
    const auto yoe = static_cast<unsigned>(y - static_cast<int>(era) * 400);
    const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<std::int64_t>(doe) - 719468;
}

CivilDate Calendar::civil_from_days(std::int64_t z) noexcept {
    z += 719468;
    const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const auto doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const auto y = static_cast<std::int64_t>(yoe) + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    const unsigned d = doy - (153 * mp + 2) / 5 + 1;
    const unsigned m = mp < 10 ? mp + 3 : mp - 9;
    return CivilDate{static_cast<int>(y + (m <= 2)), m, d};
}

unsigned Calendar::weekday_of(std::int64_t days) noexcept {
    // 1970-01-01 was a Thursday: 4.
    const std::int64_t w = (days + 4) % 7;
    return static_cast<unsigned>(w < 0 ? w + 7 : w);
}

bool Calendar::valid(int year, unsigned month, unsigned day) noexcept {
    if (month < 1 || month > 12 || day < 1) {
        return false;
    }
    static constexpr unsigned lengths[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    unsigned length = lengths[month - 1];
    const bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    if (month == 2 && leap) {
        length = 29;
    }
    return day <= length;
}

std::int64_t Calendar::today() noexcept {
    const std::time_t now = std::time(nullptr);
    std::tm parts{};
#ifdef _WIN32
    gmtime_s(&parts, &now);
#else
    gmtime_r(&now, &parts);
#endif
    return days_from_civil(parts.tm_year + 1900, static_cast<unsigned>(parts.tm_mon + 1),
                           static_cast<unsigned>(parts.tm_mday));
}

std::string Calendar::weekday_name(std::int64_t days) const {
    std::string name = weekdays_[weekday_of(days)];
    if (!name.empty()) {
        name.front() = static_cast<char>(name.front() - 'a' + 'A');
    }
    return name;
}

std::string Calendar::month_name(unsigned month) const {
    for (const auto& [name, number] : months_) {
        if (number == month && name.size() > 3) {
            std::string out = name;
            out.front() = static_cast<char>(out.front() - 'a' + 'A');
            return out;
        }
    }
    return std::to_string(month);
}

std::string Calendar::written(std::int64_t days) const {
    const CivilDate d = civil_from_days(days);
    return std::format("{}, {} {} {}", weekday_name(days), d.day, month_name(d.month), d.year);
}

std::optional<std::int64_t> Calendar::read_date(const std::vector<std::string>& words, std::size_t& at) const {
    if (at >= words.size()) {
        return std::nullopt;
    }
    const std::string& word = words[at];
    if (word == "today" || word == "now") {
        ++at;
        return today();
    }
    // "2026-10-08".
    const std::vector<std::string> parts = split(word, '-');
    if (parts.size() == 3 && year_of(parts[0]) && all_digits(parts[1]) && all_digits(parts[2])) {
        const int y = *year_of(parts[0]);
        const auto m = static_cast<unsigned>(std::atoi(parts[1].c_str()));
        const auto d = static_cast<unsigned>(std::atoi(parts[2].c_str()));
        if (valid(y, m, d)) {
            ++at;
            return days_from_civil(y, m, d);
        }
        return std::nullopt;
    }
    const auto month_of = [&](const std::string& w) -> std::optional<unsigned> {
        for (const auto& [name, number] : months_) {
            if (name == w) {
                return number;
            }
        }
        return std::nullopt;
    };
    const CivilDate now = civil_from_days(today());
    // "8 October 2026", "8 October".
    if (const std::optional<unsigned> d = day_of(word); d && at + 1 < words.size() && month_of(words[at + 1])) {
        const unsigned m = *month_of(words[at + 1]);
        int y = now.year;
        std::size_t next = at + 2;
        if (next < words.size() && year_of(words[next])) {
            y = *year_of(words[next]);
            ++next;
        }
        if (valid(y, m, *d)) {
            at = next;
            return days_from_civil(y, m, *d);
        }
        return std::nullopt;
    }
    // "October 8, 2026", "October 8".
    if (const std::optional<unsigned> m = month_of(word); m && at + 1 < words.size() && day_of(words[at + 1])) {
        const unsigned d = *day_of(words[at + 1]);
        int y = now.year;
        std::size_t next = at + 2;
        if (next < words.size() && year_of(words[next])) {
            y = *year_of(words[next]);
            ++next;
        }
        if (valid(y, *m, d)) {
            at = next;
            return days_from_civil(y, *m, d);
        }
        return std::nullopt;
    }
    return std::nullopt;
}

std::optional<Calculation> Calendar::calculate(std::string_view text) const {
    const std::vector<std::string> words = words_of(text);
    if (words.empty()) {
        return std::nullopt;
    }
    // The pieces: dates, numbers of days, and the frame; any other word is
    // not a date question.
    std::vector<std::int64_t> dates;
    std::vector<std::int64_t> spans;  ///< In days.
    std::vector<std::string> frame;
    const auto number_word = [&](const std::string& w) -> std::optional<long long> {
        if (all_digits(w)) {
            return std::atoll(w.c_str());
        }
        for (const auto& [name, digits] : rules_->number_words()) {
            if (text_of(name) == w) {
                return std::atoll(text_of(digits).c_str());
            }
        }
        return std::nullopt;
    };
    const auto unit_of = [&](const std::string& w) -> std::optional<std::int64_t> {
        for (const auto& [name, days] : units_) {
            if (name == w) {
                return days;
            }
        }
        return std::nullopt;
    };
    for (std::size_t i = 0; i < words.size();) {
        if (const std::optional<std::int64_t> date = read_date(words, i)) {
            dates.push_back(*date);
            continue;
        }
        const std::string& w = words[i];
        if (const std::optional<long long> n = number_word(w); n && i + 1 < words.size() && unit_of(words[i + 1])) {
            spans.push_back(*n * *unit_of(words[i + 1]));
            frame.push_back(words[i + 1]);
            i += 2;
            continue;
        }
        if (w == "a" && i + 1 < words.size() && unit_of(words[i + 1])) {
            spans.push_back(*unit_of(words[i + 1]));  // "a week"
            frame.push_back(words[i + 1]);
            i += 2;
            continue;
        }
        if (std::ranges::contains(frame_, w)) {
            frame.push_back(w);
            ++i;
            continue;
        }
        return std::nullopt;
    }
    const auto said = [&](std::string_view w) { return has(frame, w); };
    Calculation out;
    const auto answer = [&](std::string expression, std::string result) {
        out.expression = "calendar: " + std::move(expression);
        out.result = std::move(result);
        return out;
    };
    const auto iso = [](std::int64_t days) {
        const CivilDate d = civil_from_days(days);
        return std::format("{:04}-{:02}-{:02}", d.year, d.month, d.day);
    };
    // Two dates: the days between them, or which comes first.
    if (dates.size() == 2 && spans.empty()) {
        if (said("before") || said("after")) {
            out.comparison = true;
            out.holds = said("before") ? dates[0] < dates[1] : dates[0] > dates[1];
            out.expression = std::format("calendar: {} {} {}", iso(dates[0]), said("before") ? "before" : "after", iso(dates[1]));
            out.result = out.holds ? "yes" : "no";
            return out;
        }
        const std::int64_t between = dates[1] > dates[0] ? dates[1] - dates[0] : dates[0] - dates[1];
        return answer(std::format("the days from {} to {}", iso(dates[0]), iso(dates[1])),
                      std::format("{} day{}", between, between == 1 ? "" : "s"));
    }
    // A date and a span: the date before or after.
    if (dates.size() == 1 && spans.size() == 1) {
        const bool before = said("before") || said("ago") || said("earlier");
        const std::int64_t target = before ? dates[0] - spans[0] : dates[0] + spans[0];
        return answer(std::format("{} days {} {}", spans[0], before ? "before" : "after", iso(dates[0])),
                      written(target));
    }
    // A span alone: from today ("in 10 days", "10 days ago").
    if (dates.empty() && spans.size() == 1 && (said("ago") || said("in") || said("from") || said("after") || said("before"))) {
        const bool before = said("ago") || said("before");
        const std::int64_t target = before ? today() - spans[0] : today() + spans[0];
        return answer(std::format("{} days {} today", spans[0], before ? "before" : "after"), written(target));
    }
    // One date: the days until it or since it, or its day of the week, or the date in full.
    if (dates.size() == 1 && spans.empty()) {
        if (said("until") || said("till") || said("since")) {
            const std::int64_t now = today();
            const std::int64_t between = said("since") ? now - dates[0] : dates[0] - now;
            return answer(std::format("the days {} {}", said("since") ? "since" : "until", iso(dates[0])),
                          std::format("{} day{}", between, between == 1 || between == -1 ? "" : "s"));
        }
        if (said("day") || said("weekday")) {
            if (!said("date")) {
                return answer("the day of the week of " + iso(dates[0]), weekday_name(dates[0]));
            }
        }
        return answer("the date " + iso(dates[0]), written(dates[0]));
    }
    // No date: today.
    if (dates.empty() && spans.empty() && (said("today") || said("date") || said("day") || said("now"))) {
        if (said("day") && !said("date") && !said("today")) {
            return answer("the day of the week today", weekday_name(today()));
        }
        return answer("today", written(today()));
    }
    return std::nullopt;
}

}  // namespace larry
