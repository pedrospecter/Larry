#pragma once

// Bytes written as two hex digits each: the form of the base rule files
// (PLAN.md, section 3, rule 4) and of the memory file.

#include "larry/electron.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace larry::hex {

[[nodiscard]] inline std::string encode(std::span<const std::uint8_t> bytes) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (const std::uint8_t b : bytes) {
        out.push_back(digits[b >> 4]);
        out.push_back(digits[b & 0x0F]);
    }
    return out;
}

[[nodiscard]] inline int digit(char c) noexcept {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

/// The bytes the text spells, or nothing when it is not hex digits in pairs.
[[nodiscard]] inline std::optional<Bytes> decode(std::string_view text) {
    if (text.size() % 2 != 0) {
        return std::nullopt;
    }
    Bytes out;
    out.reserve(text.size() / 2);
    for (std::size_t i = 0; i < text.size(); i += 2) {
        const int high = digit(text[i]);
        const int low = digit(text[i + 1]);
        if (high < 0 || low < 0) {
            return std::nullopt;
        }
        out.push_back(static_cast<std::uint8_t>(high * 16 + low));
    }
    return out;
}

}  // namespace larry::hex
