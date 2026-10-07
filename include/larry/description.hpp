#pragma once

#include "larry/electron.hpp"
#include "larry/sentence.hpp"

#include <cstdint>
#include <vector>

namespace larry {

/// Where the category of an entity came from when its sentence was described.
enum class Source : std::uint8_t {
    Taught,   ///< Given with the sentence (assimilation level 0).
    Memory,   ///< The one category the stored atoms give the word.
    Open,     ///< The stored atoms give the word several categories. None is chosen.
    Unknown,  ///< No stored atom contains the word.
};

/// What describing a sentence noted about one entity.
struct EntityNote {
    Source source = Source::Unknown;
    /// The categories memory gives the word, when the source is Open.
    std::vector<Bytes> candidates;
};

/// A sentence with all of its electrons: what assimilation produces, what the
/// database stores and finds back, and what cognition compares.
struct Description {
    Sentence atom;
    CategoryElectron category;
    TypeElectron type;
    EntitiesElectron entities;
    ImageElectron image;
    MetadataElectron metadata;
    /// One note per entity. Empty for an atom read back from the database.
    std::vector<EntityNote> notes;
};

}  // namespace larry
