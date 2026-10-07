#pragma once

// Units of UTF-8 text. A unit is one code point, or one byte that is not
// valid UTF-8, so any input can be walked without a decoder failing.

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace larry::utf8 {

/// The size in bytes of the unit that starts at byte i.
[[nodiscard]] inline std::size_t unit_size(std::string_view s, std::size_t i) noexcept {
    const auto b = static_cast<std::uint8_t>(s[i]);
    std::size_t need = 0;
    if (b < 0x80) {
        return 1;
    }
    if ((b & 0xE0) == 0xC0) {
        need = 1;
    } else if ((b & 0xF0) == 0xE0) {
        need = 2;
    } else if ((b & 0xF8) == 0xF0) {
        need = 3;
    } else {
        return 1;
    }
    if (i + need >= s.size()) {
        return 1;
    }
    for (std::size_t k = 1; k <= need; ++k) {
        if ((static_cast<std::uint8_t>(s[i + k]) & 0xC0) != 0x80) {
            return 1;
        }
    }
    return need + 1;
}

[[nodiscard]] inline std::string_view unit_at(std::string_view s, std::size_t i) noexcept {
    return s.substr(i, unit_size(s, i));
}

/// The start of the unit that ends at `end`, not before `begin`.
[[nodiscard]] inline std::size_t last_unit_begin(std::string_view s, std::size_t begin,
                                                 std::size_t end) noexcept {
    std::size_t p = end - 1;
    std::size_t steps = 0;
    while (p > begin && steps < 3 && (static_cast<std::uint8_t>(s[p]) & 0xC0) == 0x80) {
        --p;
        ++steps;
    }
    if (p + unit_size(s, p) == end) {
        return p;
    }
    return end - 1;
}

/// ASCII white space, the no-break space, the Unicode spaces U+2000 to
/// U+200A, the line and paragraph separators, the ideographic space and the
/// byte order mark.
[[nodiscard]] inline bool is_space(std::string_view u) noexcept {
    if (u.size() == 1) {
        const char c = u[0];
        return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
    }
    if (u == "\xC2\xA0" || u == "\xE3\x80\x80" || u == "\xE2\x80\xA8" || u == "\xE2\x80\xA9" ||
        u == "\xEF\xBB\xBF") {
        return true;
    }
    if (u.size() == 3 && u[0] == '\xE2' && u[1] == '\x80') {
        const auto c = static_cast<std::uint8_t>(u[2]);
        return c >= 0x80 && c <= 0x8A;
    }
    return false;
}

[[nodiscard]] inline bool is_ascii_upper(std::string_view u) noexcept {
    return u.size() == 1 && u[0] >= 'A' && u[0] <= 'Z';
}

[[nodiscard]] inline bool is_ascii_lower(std::string_view u) noexcept {
    return u.size() == 1 && u[0] >= 'a' && u[0] <= 'z';
}

[[nodiscard]] inline bool is_ascii_digit(std::string_view u) noexcept {
    return u.size() == 1 && u[0] >= '0' && u[0] <= '9';
}

}  // namespace larry::utf8
