#pragma once

#include "larry/constellation.hpp"
#include "larry/electron.hpp"

#include <vector>

namespace larry {

/// The base rules of a language, read from its directory in base_rules/, which
/// is named by the language's locale ("en").
class BaseRules {
public:
    explicit BaseRules(Language language);

    /// The categories a word can have, from categories.txt. Each line of the
    /// file is one category as bytes, written as two hex digits for each byte.
    [[nodiscard]] const std::vector<Bytes>& categories() const noexcept { return categories_; }

private:
    std::vector<Bytes> categories_;
};

}  // namespace larry
