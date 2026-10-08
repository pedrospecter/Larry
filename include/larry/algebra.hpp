#pragma once

#include "larry/arithmetic.hpp"
#include "larry/base_rules.hpp"

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace larry {

/// A polynomial in the unknown x of degree at most two (M3).
struct Poly {
    std::array<long double, 3> c{0, 0, 0};  ///< c[0] + c[1] x + c[2] x^2.

    [[nodiscard]] int degree() const noexcept;
    [[nodiscard]] bool constant() const noexcept { return degree() <= 0; }
    /// The polynomial as Larry writes it: "2x^2 - 3x + 1", "x", "0".
    [[nodiscard]] std::string written() const;
    [[nodiscard]] long double at(long double x) const noexcept { return c[0] + c[1] * x + c[2] * x * x; }
};

/// Algebra as cognition (M3): an equation in one unknown is solved, an
/// expression simplified or expanded, and evaluated when the unknown is
/// given. The unknown is x; expressions are polynomials of degree at most
/// two, so "x^3" and products of three x are beyond it. The operator words
/// are those of arithmetic.txt and the frame words those of algebra.txt.
class Algebra {
public:
    explicit Algebra(const BaseRules& rules);

    /// The answer the text asks for, or nothing when the text has no unknown
    /// or is beyond what Larry does: "x = 4", "x = -3 or x = 3", "no
    /// solution", "any x", "5x + 2", "10".
    [[nodiscard]] std::optional<Calculation> calculate(std::string_view text) const;

    struct Token {
        enum class Kind { Number, Unknown, Operator, Open, Close, Equals };
        Kind kind;
        long double value = 0;
        std::string symbol;
    };

    /// The tokens of a text, or nothing when a word is not algebra. The
    /// condition ("if x is 3") is split off into `given`.
    [[nodiscard]] std::optional<std::vector<Token>> tokens(std::string_view text,
                                                          std::optional<long double>& given,
                                                          bool& solve, bool& simplify) const;

private:
    const BaseRules* rules_;
    std::vector<std::pair<std::string, std::string>> words_;  ///< Operator phrases, longest first.
    std::vector<std::string> frame_;
};

}  // namespace larry
