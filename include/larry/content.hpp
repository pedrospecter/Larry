#pragma once

#include "larry/base_rules.hpp"
#include "larry/description.hpp"
#include "larry/electron.hpp"
#include "larry/sentence.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace larry {

class Brain;

/// What a sentence of content is (W2): not everything is a conception.
enum class ContentClass : std::uint8_t {
    Fact,         ///< An affirmation in the third person that the grammar reads: a candidate conception.
    Context,      ///< It hangs on what came before: a pronoun, an adverb of place or time, a connective.
    Question,     ///< A question.
    Instruction,  ///< An order.
    Speech,       ///< First or second person, a quotation, an opinion marker, an expression.
    Heading,      ///< No end mark, or a word that is a heading by itself.
    Reference,    ///< A citation, a web address, a reading list.
    Fragment,     ///< No verb, too short or too long, beyond the tolerance, or words the machine cannot place.
};

[[nodiscard]] std::string_view name(ContentClass c) noexcept;

/// One sentence of content with its class and the reason.
struct Piece {
    Sentence sentence;
    ContentClass what = ContentClass::Fragment;
    std::string reason;
    /// The description it was judged on, for whoever stores it.
    Description description;
};

/// The count of each class in a text.
struct ContentTally {
    std::size_t facts = 0;
    std::size_t context = 0;
    std::size_t questions = 0;
    std::size_t instructions = 0;
    std::size_t speech = 0;
    std::size_t headings = 0;
    std::size_t references = 0;
    std::size_t fragments = 0;
    [[nodiscard]] std::size_t total() const noexcept {
        return facts + context + questions + instructions + speech + headings + references + fragments;
    }
    void add(ContentClass c) noexcept;
};

/// The classifier of content (W2). It uses the brain for the description of
/// each sentence (categories, qualification, the reading within the
/// tolerance) and the markers in base_rules/<locale>/content.txt. Every
/// class comes with its reason.
class Content {
public:
    explicit Content(const BaseRules& rules);

    /// The sentences of a text, each with its class and reason.
    [[nodiscard]] std::vector<Piece> classify(std::string_view text, const Brain& brain) const;

    /// One sentence.
    [[nodiscard]] Piece classify(const Sentence& sentence, const Brain& brain) const;

    [[nodiscard]] std::size_t least_words() const noexcept { return least_; }
    [[nodiscard]] std::size_t most_words() const noexcept { return most_; }

private:
    const BaseRules* rules_;
    std::vector<Bytes> reference_;
    std::vector<Bytes> speech_;
    std::vector<Bytes> context_;
    std::vector<Bytes> heading_;
    std::size_t least_ = 3;
    std::size_t most_ = 40;
};

}  // namespace larry
