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
    Guess,    ///< No stored atom contains the word; the category is what known
              ///< words have in the same context (A6, first step). Stored as a
              ///< guess, never as evidence.
    Dictionary,  ///< No stored atom contains the word; the dictionary gives it
                 ///< one category (A2b).
};

/// What describing a sentence noted about one entity.
struct EntityNote {
    Source source = Source::Unknown;
    /// The categories memory or the dictionary gives the word, when the source
    /// is Open, or the categories the context voted for, most votes first,
    /// when it is Guess.
    std::vector<Bytes> candidates;
    /// For a word neither memory nor the dictionary knows: the dictionary
    /// words one typing slip away, as a spell checker suggests them.
    std::vector<Bytes> near;
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
