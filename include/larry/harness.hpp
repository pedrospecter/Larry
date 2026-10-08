#pragma once

#include "larry/base_rules.hpp"
#include "larry/description.hpp"
#include "larry/electron.hpp"
#include "larry/memory.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace larry {

class Database;

/// A relation between two words of a sentence, from their roles (K4): the
/// attribute of a thing ("the sea is wide": wide, attribute of sea), the
/// subject of a verb ("birds fly": birds, subject of fly), the object of a
/// verb ("Tom has apples": apples, object of has), the modifier of a noun
/// ("the old dog": old, modifier of dog), the complement of a preposition
/// ("to the kitchen": kitchen, complement of to).
struct Relation {
    Bytes kind;  ///< "attribute of", "subject of", "object of", "modifier of", "complement of".
    Bytes head;  ///< The word the dependent is said of, in lower case.
    Bytes dependent;
    std::size_t head_index = 0;
    std::size_t dependent_index = 0;
};

/// A word seen in a relation, in a conception.
struct Seen {
    Bytes word;
    Status status = Status::Proposed;
    std::int64_t atom = 0;
    bool from_cloud = false;
};

/// How a relation stands against the conceptions.
enum class Standing : std::uint8_t {
    Known,      ///< A conception has this pair.
    Plausible,  ///< The dependent is known in this relation, of other heads.
    Unusual,    ///< The dependent was never seen in this relation.
};

[[nodiscard]] std::string_view name(Standing standing) noexcept;

/// The judgement of one relation.
struct Judgement {
    Relation relation;
    Standing standing = Standing::Unusual;
    /// Conceptions that hold the exact pair: how many validated, how many proposed.
    std::size_t validated = 0;
    std::size_t proposed = 0;
    /// What is known of the head in this relation: "of the sea I know: vast, deep".
    std::vector<Seen> of_head;
    /// What the dependent is known of: "wide is said of: road, river".
    std::vector<Seen> of_dependent;
    /// Whether the cloud was searched because the cache had nothing.
    bool from_cloud = false;

    /// The judgement as one line: "\"wide\" was never said of the sea; of the
    /// sea I know: vast; \"wide\" is said of nothing I know".
    [[nodiscard]] std::string text() const;
};

/// The report on a sentence: one judgement per relation.
struct Report {
    std::vector<Judgement> judgements;
    /// The lines for what is unusual, for say, ask and show.
    [[nodiscard]] std::vector<std::string> unusual() const;
};

/// The context harness (K4): is each word used with the words it is known
/// with? For each relation of a sentence, the conceptions in the cache
/// (and in the cloud when the cache has nothing with the head) decide:
/// known when a conception has the pair, plausible when the dependent is
/// known in that relation of other things, unusual when it never was. The
/// report names what is known of the thing instead and what the word is
/// known of. Validated conceptions are the standard; proposed ones are
/// marked as such, and withdrawn ones count for nothing. Larry cannot know
/// that "vast" fits the sea better than "wide" until a conception says so,
/// but it can say that "wide" was never said of the sea.
class Harness {
public:
    explicit Harness(const BaseRules& rules);

    /// The relations of a described sentence, from its roles.
    [[nodiscard]] std::vector<Relation> relations(const Description& d) const;

    /// The report on a described sentence against the conceptions.
    [[nodiscard]] Report judge(const Description& d, Memory* memory, Database* cloud = nullptr) const;

private:
    const BaseRules* rules_;
};

}  // namespace larry
