#pragma once

#include "larry/base_rules.hpp"
#include "larry/electron.hpp"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace larry {

/// A4: a word read as a form of another: "skies" is a form of "sky".
struct Form {
    Bytes word;      ///< As the index keys it: "skies".
    Bytes base;      ///< "sky".
    Bytes category;  ///< The category the ending or the pair gives the form: "noun".
    Bytes feature;   ///< "plural", "past", "comparative", ...
    std::string rule;  ///< How: "ending ies (y + ies)", "irregular pair".
    bool operator==(const Form&) const = default;
};

/// A4: the forms of words. A regular form is a word with an ending of
/// endings.txt; its base is the word before the ending, with "y" back for
/// "ies", "e" back for "loved" and a doubled consonant undone for "stopped",
/// which of these the one that is known. An irregular form is a pair of
/// irregular.txt ("went" and "go"). The relation is kept as a "form of"
/// bond between the two entities by whoever stores it (the brain).
class Forms {
public:
    explicit Forms(const BaseRules& rules);

    /// Whether a base with a category is known: memory, the dictionary, or
    /// a list in a test.
    using Known = std::function<bool(const Bytes& base, const Bytes& category)>;

    /// The forms a word may be, by its endings, the longest ending first and
    /// for each the bases in the order tried; the irregular pair last. A
    /// word must be at least two bytes longer than the ending (endings.txt).
    [[nodiscard]] std::vector<Form> candidates(const Bytes& word) const;

    /// The form a word is: the first candidate whose base is known with the
    /// category the ending gives, else the irregular pair, else nothing. A
    /// word that is a base itself ("run" as a participle) gives nothing.
    [[nodiscard]] std::optional<Form> base_of(const Bytes& word, const Known& known) const;

    /// The category an ending alone proposes, when every entry of
    /// endings.txt for that ending gives the same category ("ing" is a verb);
    /// nothing when the ending is ambiguous ("s": a noun or a verb) or the
    /// word has no ending.
    [[nodiscard]] std::optional<Form> by_ending(const Bytes& word) const;

private:
    const BaseRules* rules_;
};

}  // namespace larry
