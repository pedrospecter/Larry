#pragma once

#include "larry/electron.hpp"
#include "larry/sentence.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace larry {

/// The electrons the metadata is made of, read back from it.
struct Electrons {
    CategoryElectron category;
    TypeElectron type;
    EntitiesElectron entities;
};

class AtomOperations {
public:
    [[nodiscard]] Sentence from_text(std::string_view text) const;

    /// The atom's bytes: the sentence's UTF-8.
    [[nodiscard]] std::span<const std::uint8_t> bytes(const Sentence& sentence) const noexcept;

    /// The atom's bytes read as text.
    [[nodiscard]] std::string_view text(const Sentence& sentence) const noexcept;

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

    /// The leading bytes of the metadata of every atom with this category and
    /// type whose first entities are these. Atoms that share them sort next to
    /// each other, so the database finds them by this prefix.
    [[nodiscard]] Bytes metadata_prefix(const CategoryElectron& category, const TypeElectron& type,
                                        std::span<const Entity> leading) const;

    /// The electrons read back from metadata. Throws std::invalid_argument when
    /// the bytes were not written by metadata().
    [[nodiscard]] Electrons electrons(const MetadataElectron& metadata) const;

    /// A word as the word index keys it: ASCII letters in lower case, every
    /// other byte as it is.
    [[nodiscard]] Bytes fold(std::span<const std::uint8_t> word) const;
};

}  // namespace larry
