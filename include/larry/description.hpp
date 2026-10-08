#pragma once

#include "larry/electron.hpp"
#include "larry/sentence.hpp"

#include <cstdint>
#include <string>
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
    Rule,        ///< The base rules give it: a pronoun of pronouns.txt, a number
                 ///< word of number_words.txt (A11, M1).
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
    /// A4: when the category came from the word's form: "skies is a form of
    /// sky (ending ies (y + ies))". The source is Guess then.
    std::string form;
    /// A6: for a word memory knows with several categories (source Open), how
    /// the most specific context chose the one the entity carries: "the words
    /// on both sides", "the word before", "the word after", "the categories on
    /// both sides", "the category before", "the category after" or "the most
    /// used". Empty when nothing chose and the category is empty.
    std::string context;
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
    /// The name of the grammar pattern the sentence fits, which gave the
    /// roles (K2); empty when none fits or there is no grammar, and the
    /// roles came from the position heuristic. Not part of the metadata.
    std::string pattern;
};

}  // namespace larry
