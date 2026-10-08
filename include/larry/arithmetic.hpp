#pragma once

#include "larry/base_rules.hpp"
#include "larry/electron.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace larry {

/// A calculation Larry did (M1): the expression as it read it, in symbols,
/// and the result.
struct Calculation {
    std::string expression;  ///< "2 + 3", "5 > 3".
    std::string result;      ///< "5", "3.5", "undefined: division by zero".
    bool comparison = false; ///< Whether it was a comparison, with `holds` as its answer.
    bool holds = false;
    bool defined = true;
    /// The rule, for the reply: "arithmetic: 2 + 3 = 5".
    [[nodiscard]] std::string rule() const;
};

/// Arithmetic as cognition (M1, the first step of R7): a text made of
/// numbers, number words, operator words and frame words is calculated,
/// with the operator words as data in base_rules/<locale>/arithmetic.txt.
/// Anything else in the text means it is not arithmetic.
class Arithmetic {
public:
    explicit Arithmetic(const BaseRules& rules);

    /// The calculation the text asks for, or nothing when the text is not
    /// arithmetic. A text with a frame ("what is") and a number alone is a
    /// calculation too; a bare number is not.
    [[nodiscard]] std::optional<Calculation> calculate(std::string_view text) const;

    /// A number as Larry writes it: no decimals for a whole number, up to six
    /// decimals otherwise.
    [[nodiscard]] static std::string number(long double value);

    /// A piece of a calculation as read: a number, an operator, a bracket or
    /// a comparison.
    struct Token {
        enum class Kind { Number, Operator, Open, Close, Compare };
        Kind kind;
        long double value = 0;
        std::string symbol;  ///< Operator or comparison symbol.
    };

    /// The tokens of a text, or nothing when a word is not arithmetic.
    [[nodiscard]] std::optional<std::vector<Token>> tokens(std::string_view text) const;

private:

    const BaseRules* rules_;
    std::vector<std::pair<std::string, std::string>> words_;  ///< Operator phrases, longest first.
    std::vector<std::string> frame_;
};

}  // namespace larry
