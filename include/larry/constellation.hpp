#pragma once

namespace larry {

enum class Language {
    English,
};

/// The constellation: a language.
class Constellation {
public:
    constexpr explicit Constellation(Language language) noexcept : language_(language) {}

    [[nodiscard]] constexpr Language language() const noexcept { return language_; }

private:
    Language language_;
};

}  // namespace larry
