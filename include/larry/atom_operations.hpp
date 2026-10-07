#pragma once

#include "larry/electron.hpp"
#include "larry/sentence.hpp"

#include <string>
#include <string_view>

namespace larry {

class AtomOperations {
public:
    [[nodiscard]] Sentence from_text(std::string_view text) const;

    /// Bits in reading order as '0' and '1': bit 0 is the most significant bit
    /// of the first byte.
    [[nodiscard]] std::string to_bits(const Sentence& sentence) const;

    /// The metadata: the atom's category and type, then each entity in sentence
    /// order with its word, category and types. Different electrons always give
    /// different metadata, and atoms that share leading parts sort next to each
    /// other.
    [[nodiscard]] MetadataElectron metadata(const CategoryElectron& category,
                                            const TypeElectron& type,
                                            const EntitiesElectron& entities) const;
};

}  // namespace larry
