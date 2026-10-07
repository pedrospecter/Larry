#pragma once

#include "larry/constellation.hpp"
#include "larry/description.hpp"
#include "larry/electron.hpp"
#include "larry/sentence.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace larry {

/// What a conception stands at: an atom Larry was told is proposed until it
/// is validated, by the user or by a second source, and withdrawn when it is
/// rejected (R2, first step).
enum class Status : std::uint8_t {
    Proposed,
    Validated,
    Withdrawn,
};

/// The name of a status: the bytes the database stores.
[[nodiscard]] std::string_view name(Status status) noexcept;

/// The status with this name, if any.
[[nodiscard]] std::optional<Status> status_from(std::string_view name) noexcept;

/// An atom as memory or the database holds it, with its electrons read back.
struct StoredAtom {
    std::int64_t id = 0;
    Description description;
    Status status = Status::Proposed;
    /// Where it came from: "lesson:<file>", "user", "read:<file>", "cloud".
    std::vector<std::string> sources;
};

/// One use of a word: the atom, the position of the entity in it, the
/// category the word has there, and its context: the words before and after,
/// with their categories.
struct WordUse {
    std::int64_t atom;
    std::size_t position;
    Bytes category;
    Bytes before;           ///< The word before it, or empty at the start of the atom.
    Bytes after;            ///< The word after it, or empty at the end.
    Bytes before_category;  ///< The category of the word before, or empty.
    Bytes after_category;   ///< The category of the word after, or empty.
};

/// A category a word has been seen with, and in how many atoms.
struct CategoryCount {
    Bytes category;
    std::int64_t count;
};

/// What store() did with an atom.
enum class Stored : std::uint8_t {
    New,       ///< The atom was stored.
    Same,      ///< The same atom was already there. Nothing changed.
    SameForm,  ///< An atom with the same metadata but different bits was already
               ///< there: the same words, with other capitals, spacing or
               ///< punctuation. The first one stays. Nothing changed.
};

/// Memory: what the machine keeps. The sentences Larry has met, their words
/// with category and context (the word index, N1), and a cache of recent
/// conceptions. It is a local file, a log of atoms with their status and
/// sources as hex. The cloud (Database) is the record of conceptions that
/// this cache answers for first (N2).
class Memory {
public:
    /// Opens the memory file and reads it when it exists. Throws
    /// std::runtime_error when the file is not a memory file.
    explicit Memory(std::filesystem::path file);

    /// The file from the environment variable LARRY_MEMORY, or
    /// memory/<locale>.atoms in the repository (F3).
    [[nodiscard]] static std::filesystem::path file_from_environment(Language language);

    [[nodiscard]] const std::filesystem::path& file() const noexcept { return file_; }

    /// Forgets everything and empties the file.
    void clear();

    /// Store an atom under its metadata, which is where the neural network
    /// finds it, index its words, and append it to the file. When it is
    /// already there, only a new source is added.
    Stored store(const Sentence& atom, const MetadataElectron& metadata,
                 Status status = Status::Proposed, std::string_view source = "");

    /// Changes the status of the atom with this metadata. False when it is not there.
    bool set_status(const MetadataElectron& metadata, Status status);

    /// The last atoms stored, newest first.
    [[nodiscard]] std::vector<StoredAtom> recent(std::int64_t count) const;

    /// The atoms at a status, in the order they were stored.
    [[nodiscard]] std::vector<StoredAtom> with_status(Status status) const;

    /// The atom stored under this metadata.
    [[nodiscard]] std::optional<StoredAtom> find(const MetadataElectron& metadata) const;

    /// The atom with this id.
    [[nodiscard]] std::optional<StoredAtom> find_id(std::int64_t id) const;

    /// Every atom whose metadata starts with these bytes, in metadata order.
    [[nodiscard]] std::vector<StoredAtom> find_prefix(const Bytes& prefix) const;

    /// Every atom, in the order they were stored.
    [[nodiscard]] std::vector<StoredAtom> all() const;

    /// Where a word (as the index keys it, see AtomOperations::fold) is used,
    /// in atom and position order.
    [[nodiscard]] std::vector<WordUse> uses(const Bytes& word) const;

    /// The categories a word has been seen with, in category order. Uses with
    /// no category do not count.
    [[nodiscard]] std::vector<CategoryCount> categories_of(const Bytes& word) const;

    /// The atoms that contain a word, in the order they were stored.
    [[nodiscard]] std::vector<StoredAtom> containing(const Bytes& word) const;

    [[nodiscard]] std::int64_t count() const noexcept { return static_cast<std::int64_t>(atoms_.size()); }
    [[nodiscard]] std::int64_t count_words() const noexcept { return word_uses_; }

private:
    struct Record {
        MetadataElectron metadata;
        Bytes bytes;
        Status status;
        std::vector<std::string> sources;
    };

    void append(const std::string& line);
    void index(std::int64_t id, const MetadataElectron& metadata);
    [[nodiscard]] StoredAtom read(std::int64_t id) const;

    std::filesystem::path file_;
    std::vector<Record> atoms_;  ///< The atom with id n is atoms_[n - 1].
    std::map<Bytes, std::int64_t> by_metadata_;
    std::map<Bytes, std::vector<WordUse>> words_;
    std::int64_t word_uses_ = 0;
};

}  // namespace larry
