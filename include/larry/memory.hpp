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
    /// Where it came from: "lesson:<file>", "user:<name>", "read:<file>", "cloud".
    std::vector<std::string> sources;
    /// Who validated or withdrew it: a validator's name, or empty.
    std::string decided_by;
    /// Q29: the sentence as Larry read it when it was stored, when the
    /// reading differed from what was said ("The sky is blue." for "Sky is
    /// blue."); empty when it was read as said. A conception with a reading
    /// teaches no usage and no grammar.
    std::string reading;
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

/// One end of a bond (N3): a conception, by its identity (Q28: its
/// qualification and its words, so a description corrected later is the
/// same end), or an entity, by its word as the index keys it.
struct BondEnd {
    enum class Kind : std::uint8_t { Atom, Entity };
    Kind kind = Kind::Entity;
    Bytes bytes;
    bool operator==(const BondEnd&) const = default;
    /// The conception this metadata describes.
    [[nodiscard]] static BondEnd atom(const MetadataElectron& metadata);
    /// The entity with this word, in any case.
    [[nodiscard]] static BondEnd entity(std::string_view word);
};

/// The name of an end's kind, "atom" or "entity": the bytes the file and the
/// database store; and the kind with this name.
[[nodiscard]] std::string_view name(BondEnd::Kind kind) noexcept;
[[nodiscard]] std::optional<BondEnd::Kind> bond_end_from(std::string_view name) noexcept;

/// A typed link between two ends (N3): its kind, its two ends, and where it
/// came from: taught ("user:pedro"), or the rule or comparison that
/// produced it ("rule: ..."). The same kind and ends are one bond, with
/// every origin.
struct Bond {
    Bytes kind;  ///< "conflicts with", "form of", "answers", "is a kind of".
    BondEnd from;
    BondEnd to;
    std::vector<std::string> origins;
    [[nodiscard]] bool same(const Bond& other) const noexcept {
        return kind == other.kind && from == other.from && to == other.to;
    }
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

    /// Store an atom under its identity (Q28: its qualification and its words,
    /// taken from the metadata), index its words, and append it to the file.
    /// When it is already there, only a new source is added: the stored
    /// description stays, see redescribe().
    Stored store(const Sentence& atom, const MetadataElectron& metadata,
                 Status status = Status::Proposed, std::string_view source = "");

    /// Gives the conception with this metadata's identity this metadata as
    /// its description, when it differs from the stored one: the types or
    /// roles were corrected. Its words are indexed anew. False when the
    /// conception is not there or the description is the same.
    bool redescribe(const MetadataElectron& metadata);

    /// Changes the status of the atom with this metadata's identity and
    /// records who decided. False when it is not there.
    bool set_status(const MetadataElectron& metadata, Status status, std::string_view by = "");

    /// Notes how the atom with this metadata's identity was read (Q29), when
    /// the reading differed from what was said. False when it is not there.
    bool set_reading(const MetadataElectron& metadata, std::string_view reading);

    /// The validators: the only people who validate or withdraw a conception
    /// (the user's safeguard). Kept in the memory file.
    [[nodiscard]] std::vector<std::string> validators() const;
    void add_validator(std::string_view name);

    /// The last atoms stored, newest first.
    [[nodiscard]] std::vector<StoredAtom> recent(std::int64_t count) const;

    /// The atoms at a status, in the order they were stored.
    [[nodiscard]] std::vector<StoredAtom> with_status(Status status) const;

    /// The atom stored under this metadata's identity: the same sentence with
    /// the same qualification, whatever its types.
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
    /// no category, and guessed ones, do not count.
    [[nodiscard]] std::vector<CategoryCount> categories_of(const Bytes& word) const;

    /// Every word in the index, in byte order: the vocabulary.
    [[nodiscard]] std::vector<Bytes> words() const;

    /// The atoms that contain a word, in the order they were stored.
    [[nodiscard]] std::vector<StoredAtom> containing(const Bytes& word) const;

    [[nodiscard]] std::int64_t count() const noexcept { return static_cast<std::int64_t>(atoms_.size()); }
    [[nodiscard]] std::int64_t count_words() const noexcept { return word_uses_; }

    /// The atom stored under this identity (Q28), if any.
    [[nodiscard]] std::optional<StoredAtom> find_identity(const Bytes& identity) const;

    /// N3: records a bond, in the file too. True when it is new; the same
    /// kind and ends again only add their origins. Throws
    /// std::invalid_argument for a bond without a kind or an end.
    bool bond(const Bond& bond);

    /// N3: the bonds from an end, to an end, and either way, in the order
    /// they were recorded.
    [[nodiscard]] std::vector<Bond> bonds_from(const BondEnd& end) const;
    [[nodiscard]] std::vector<Bond> bonds_to(const BondEnd& end) const;
    [[nodiscard]] std::vector<Bond> bonds_of(const BondEnd& end) const;
    [[nodiscard]] const std::vector<Bond>& bonds() const noexcept { return bonds_; }
    [[nodiscard]] std::int64_t count_bonds() const noexcept { return static_cast<std::int64_t>(bonds_.size()); }

private:
    struct Record {
        MetadataElectron metadata;
        Bytes bytes;
        Status status;
        std::vector<std::string> sources;
        std::string decided_by;
        Bytes identity;
        std::string reading;
    };

    void append(const std::string& line);
    /// Adds a bond to the maps, and to the file when `write` is set.
    bool add_bond(const Bond& bond, bool write);
    void index(std::int64_t id, const MetadataElectron& metadata);
    void unindex(std::int64_t id);
    /// Replaces the description of a record, in the maps and the index.
    void describe(std::int64_t id, const MetadataElectron& metadata);
    [[nodiscard]] StoredAtom read(std::int64_t id) const;

    std::filesystem::path file_;
    std::vector<Record> atoms_;  ///< The atom with id n is atoms_[n - 1].
    std::map<Bytes, std::int64_t> by_identity_;
    std::map<Bytes, std::int64_t> by_metadata_;
    std::map<Bytes, std::vector<WordUse>> words_;
    std::int64_t word_uses_ = 0;
    std::vector<std::string> validators_;
    std::vector<Bond> bonds_;
    std::map<Bytes, std::vector<std::size_t>> bonds_from_;  ///< By end key: the bonds from it.
    std::map<Bytes, std::vector<std::size_t>> bonds_to_;
};

}  // namespace larry
