#pragma once

#include <optional>
#include <string_view>

namespace larry {

enum class Language {
    English,
    Portuguese,  ///< A12: the second constellation, European Portuguese.
};

/// The locale code that names a language in base_rules/, lessons/ and the
/// database (PLAN.md, section 3, rule 3).
[[nodiscard]] constexpr std::string_view locale(Language language) noexcept {
    switch (language) {
    case Language::English:
        return "en";
    case Language::Portuguese:
        return "pt";
    }
    return "";
}

/// The language with this locale code ("en", "pt"), or nothing.
[[nodiscard]] constexpr std::optional<Language> language_named(std::string_view code) noexcept {
    if (code == "en") {
        return Language::English;
    }
    if (code == "pt") {
        return Language::Portuguese;
    }
    return std::nullopt;
}

/// The constellation: a language.
class Constellation {
public:
    constexpr explicit Constellation(Language language) noexcept : language_(language) {}

    [[nodiscard]] constexpr Language language() const noexcept { return language_; }

private:
    Language language_;
};

}  // namespace larry
