#pragma once

#include <string_view>

namespace larry {

enum class Language {
    English,
};

/// The locale code that names a language in base_rules/, lessons/ and the
/// database (PLAN.md, section 3, rule 3).
[[nodiscard]] constexpr std::string_view locale(Language language) noexcept {
    switch (language) {
    case Language::English:
        return "en";
    }
    return "";
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
