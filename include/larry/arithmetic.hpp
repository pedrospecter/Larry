#pragma once

#include "larry/base_rules.hpp"
#include "larry/electron.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace larry {

/// A calculation Larry did (M1, M2): the expression as it read it, in
/// symbols, and the result.
struct Calculation {
    std::string expression;  ///< "2 + 3", "5 > 3", "3 hours in minutes".
    std::string result;      ///< "5", "3.5", "180 minutes", "undefined: division by zero".
    bool comparison = false; ///< Whether it was a comparison, with `holds` as its answer.
    bool holds = false;
    bool defined = true;
    /// What stands between the expression and the result in the rule: "="
    /// for a calculation, "gives" for an equation solved.
    std::string link = "=";
    /// The rule, for the reply: "arithmetic: 2 + 3 = 5".
    [[nodiscard]] std::string rule() const;
};

/// A unit of measure (M2), from units.txt.
struct Unit {
    std::string name;      ///< As written: "centimetres".
    std::string quantity;  ///< "length", "mass", "time", ...
    long double factor = 1;
    long double offset = 0;
    /// A value in this unit, in the base unit of its quantity, and back.
    [[nodiscard]] long double to_base(long double value) const { return (value - offset) * factor; }
    [[nodiscard]] long double from_base(long double base) const { return base / factor + offset; }
};

/// Arithmetic as cognition (M1, M2, the first steps of R7): a text made of
/// numbers, number words, fraction words, units, operator words and frame
/// words is calculated, with the words as data in
/// base_rules/<locale>/arithmetic.txt and units.txt. Anything else in the
/// text means it is not arithmetic.
class Arithmetic {
public:
    explicit Arithmetic(const BaseRules& rules);

    /// The calculation the text asks for, or nothing when the text is not
    /// arithmetic. A text with a frame ("what is") and a number alone is a
    /// calculation too; a bare number is not. A quantity ("3 hours") comes
    /// out in the unit asked for ("in minutes"), else in its own unit.
    [[nodiscard]] std::optional<Calculation> calculate(std::string_view text) const;

    /// A number as Larry writes it: no decimals for a whole number, up to six
    /// decimals otherwise.
    [[nodiscard]] static std::string number(long double value);

    /// The unit a word names, if any.
    [[nodiscard]] std::optional<Unit> unit(std::string_view word) const;

    /// A piece of a calculation as read: a number (with a unit when it is a
    /// quantity), an operator, a bracket, a comparison, or the unit asked for.
    struct Token {
        enum class Kind { Number, Operator, Open, Close, Compare, Target };
        Kind kind;
        long double value = 0;
        std::string symbol;         ///< Operator or comparison symbol.
        std::optional<Unit> unit;   ///< Number: its unit; Target: the unit asked for.
    };

    /// The tokens of a text, or nothing when a word is not arithmetic.
    [[nodiscard]] std::optional<std::vector<Token>> tokens(std::string_view text) const;

private:
    const BaseRules* rules_;
    std::vector<std::pair<std::string, std::string>> words_;  ///< Operator phrases, longest first.
    std::vector<std::string> frame_;
    std::vector<std::pair<std::string, long double>> fractions_;  ///< "third" and 3.
    std::vector<Unit> units_;
};

}  // namespace larry
