#pragma once

#include "larry/base_rules.hpp"
#include "larry/electron.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace larry {

/// How a sentence fits the grammar (K2).
struct Fit {
    /// Whether a pattern takes every word of the sentence.
    bool fits = false;
    /// The name of the pattern that fits, or, when none does, of the pattern
    /// that took the most words before the sentence broke.
    std::string pattern;
    /// When it fits: the role of each word, in order.
    std::vector<Bytes> roles;
    /// When it does not fit: the first word no pattern could take, as an
    /// index, or the number of words when the sentence ended too early.
    std::size_t breaks_at = 0;
    /// When it does not fit: the categories a pattern expected at that place;
    /// "end" when a pattern expected the sentence to end there.
    std::vector<Bytes> expected;
};

/// The grammar of a language: patterns of categories with the role of each
/// place, read from grammar.txt in the base rules (K2). A pattern is data: a
/// sequence of places, each a set of categories with a role and a mark
/// (optional, any number, one or more), with groups and named groups. The
/// matcher says which pattern a sequence of categories fits and gives the
/// roles; when none fits, where the sentence breaks and what was expected.
/// Validated conceptions add patterns of their own at run time (learn).
class Grammar {
public:
    /// Reads the patterns of the language's base rules. Throws
    /// std::runtime_error naming the line when a pattern is malformed.
    explicit Grammar(const BaseRules& rules);

    /// The pattern the categories fit, with the roles, or where they break.
    /// The first pattern that fits wins, in file order, base patterns before
    /// learned ones; for a question, the question patterns are tried first,
    /// else last ("What is the sky?" and "He is here." have the same
    /// categories). Empty categories never fit: a word of unknown category
    /// has no place.
    [[nodiscard]] Fit fit(std::span<const Bytes> categories, bool question = false) const;

    /// Adds a pattern from an example: the categories as places, one each,
    /// with these roles. The name says where it came from ("example: The sky
    /// is blue."); a question example is a question pattern. False when the
    /// grammar already reads those categories that way.
    bool learn(std::string_view name, std::span<const Bytes> categories,
               std::span<const Bytes> roles, bool question = false);

    /// The names of the patterns, in the order they are tried.
    [[nodiscard]] std::vector<std::string> names() const;

    /// The definition of a pattern as it was written, or empty.
    [[nodiscard]] std::string text(std::string_view name) const;

    /// How many patterns there are, and how many of them were learned.
    [[nodiscard]] std::size_t size() const noexcept { return patterns_.size(); }
    [[nodiscard]] std::size_t learned() const noexcept { return learned_; }

    /// The roles a place may have.
    [[nodiscard]] static const std::vector<Bytes>& roles();

    struct Node {
        enum class Kind { Place, Sequence, Alternation };
        Kind kind = Kind::Place;
        std::vector<Bytes> categories;  ///< Place: the categories it takes.
        std::vector<Node> children;     ///< Sequence or Alternation.
        Bytes role;                     ///< Empty: the role of the group.
        std::size_t min = 1;
        std::size_t max = 1;  ///< 0: no limit.
    };

    struct Pattern {
        std::string name;
        std::string text;
        Node body;
        bool learned = false;
        /// A pattern of questions: its name says "question", or it was
        /// learned from one.
        bool question = false;
    };

    [[nodiscard]] const std::vector<Pattern>& patterns() const noexcept { return patterns_; }

private:
    struct Macro {
        std::string name;
        Node body;
    };

    [[nodiscard]] Node parse(std::string_view text, std::string_view where) const;

    const BaseRules* rules_;
    std::vector<Macro> macros_;
    std::vector<Pattern> patterns_;
    std::size_t learned_ = 0;
};

}  // namespace larry
