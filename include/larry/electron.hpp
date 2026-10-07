#pragma once

#include <cstdint>
#include <vector>

namespace larry {

using Bytes = std::vector<std::uint8_t>;

/// An atom has one electron of each kind.
enum class ElectronKind {
    Type,
    Category,
    Entities,
    Image,
    Metadata,
};

/// The type of the atom.
struct TypeElectron {
    static constexpr ElectronKind kind = ElectronKind::Type;
    Bytes bytes;
};

/// The category of the atom.
struct CategoryElectron {
    static constexpr ElectronKind kind = ElectronKind::Category;
    Bytes bytes;
};

/// An entity in the atom: a word in the sentence. It has one category, which is
/// one of the word categories of the atom's language.
struct Entity {
    Bytes word;
    Bytes category;
    std::vector<Bytes> types;
};

/// The entities in the atom: the words in the sentence.
struct EntitiesElectron {
    static constexpr ElectronKind kind = ElectronKind::Entities;
    std::vector<Entity> entities;
};

/// The image: the atom's representation.
struct ImageElectron {
    static constexpr ElectronKind kind = ElectronKind::Image;
    Bytes bytes;
};

/// The metadata: all of the electrons above, as bytes.
struct MetadataElectron {
    static constexpr ElectronKind kind = ElectronKind::Metadata;
    Bytes bytes;
};

}  // namespace larry
