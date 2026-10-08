#pragma once

#include "larry/electron.hpp"
#include "larry/memory.hpp"
#include "larry/sentence.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct pg_conn;

namespace larry {

/// The database: the cloud, where the conceptions are on record. PostgreSQL,
/// reached through libpq. The machine keeps the language and a cache of
/// recent conceptions in Memory; what the cache cannot answer, the brain
/// searches here (N2). It shares its types with Memory.
class Database {
public:
    /// For example "host=192.168.10.133 port=5432 dbname=larry user=postgres".
    /// The password is read from ~/.pgpass. Throws std::runtime_error when
    /// the connection fails.
    explicit Database(const std::string& connection);
    ~Database();
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    /// The connection string from the environment variable LARRY_DB, or empty
    /// when there is no cloud.
    [[nodiscard]] static std::string connection_from_environment();

    /// Opens the cloud, and when the server has no database of that name yet
    /// (a new Azure server has only "postgres"), creates it first through the
    /// "postgres" database, then applies the schema. Throws std::runtime_error
    /// when the server cannot be reached or the database cannot be made.
    [[nodiscard]] static std::unique_ptr<Database> open(const std::string& connection);

    /// Runs sql/schema.sql, then fills the identity of conceptions from
    /// before it existed, merging rows that are one conception (Q28). Safe
    /// to run again.
    void apply_schema();

    /// Runs SQL as it is, for setup and tests.
    void run(const std::string& sql);

    /// Empties every table and restarts the ids.
    void clear();

    /// Store a conception under its identity (Q28: its qualification and
    /// its words, from the metadata), index its words, and record its
    /// source. When it is already there, only the source is added: the
    /// stored description stays, see redescribe().
    Stored store(const Sentence& atom, const MetadataElectron& metadata, Status status,
                 std::string_view source);

    /// Gives a conception this metadata as its description, when it differs
    /// from the stored one, and indexes its words anew. False when it is the
    /// same or the conception is not there.
    bool redescribe(std::int64_t id, const MetadataElectron& metadata);

    /// The conception with this metadata's identity: the same sentence with
    /// the same qualification, whatever its types.
    [[nodiscard]] std::optional<StoredAtom> find(const MetadataElectron& metadata);

    /// The conception with this id.
    [[nodiscard]] std::optional<StoredAtom> find_id(std::int64_t id);

    /// Every conception whose metadata starts with these bytes, in metadata order.
    [[nodiscard]] std::vector<StoredAtom> find_prefix(const Bytes& prefix);

    /// Every conception, in the order they were stored.
    [[nodiscard]] std::vector<StoredAtom> all();

    /// The last conceptions stored, newest first.
    [[nodiscard]] std::vector<StoredAtom> recent(std::int64_t count);

    /// The conceptions at a status, in the order they were stored.
    [[nodiscard]] std::vector<StoredAtom> with_status(Status status);

    void set_status(std::int64_t id, Status status, std::string_view by = "");

    /// The standing of every conception, in one query (N2c): what the cloud,
    /// the record, says a conception is, for the cache to follow.
    struct Standing {
        Bytes identity;
        Status status = Status::Proposed;
        std::string decided_by;
        std::string reading;
    };
    [[nodiscard]] std::vector<Standing> standings();

    /// Notes how a conception was read when it was stored (Q29).
    void set_reading(std::int64_t id, std::string_view reading);

    /// The validators: the only people who validate or withdraw a conception.
    [[nodiscard]] std::vector<std::string> validators();
    void add_validator(std::string_view name);

    /// Where a word (as the index keys it) is used, in conception and position order.
    [[nodiscard]] std::vector<WordUse> uses(const Bytes& word);

    /// The categories a word has been seen with, in category order. Uses with
    /// no category do not count.
    [[nodiscard]] std::vector<CategoryCount> categories_of(const Bytes& word);

    /// The conceptions that contain a word, in the order they were stored.
    [[nodiscard]] std::vector<StoredAtom> containing(const Bytes& word);

    [[nodiscard]] std::int64_t count();
    [[nodiscard]] std::int64_t count_words();

    /// The conception with this identity (Q28), if any.
    [[nodiscard]] std::optional<StoredAtom> find_identity(const Bytes& identity);

    /// N3: records a bond. True when it is new; the same kind and ends
    /// again only add their origins.
    bool bond(const Bond& bond);

    /// N3: the bonds from an end, to an end, either way, and all of them,
    /// in the order they were recorded.
    [[nodiscard]] std::vector<Bond> bonds_from(const BondEnd& end);
    [[nodiscard]] std::vector<Bond> bonds_to(const BondEnd& end);
    [[nodiscard]] std::vector<Bond> bonds_of(const BondEnd& end);
    [[nodiscard]] std::vector<Bond> bonds();
    [[nodiscard]] std::int64_t count_bonds();

    /// N4: appends a conception to a molecule, made when it is not there;
    /// the position it got, from 0.
    std::size_t join(const Bytes& molecule, const Bytes& identity, std::string_view who, std::string_view when);

    /// N4: the molecule with this name, if any; the names of every molecule,
    /// in the order they were made; how many there are.
    [[nodiscard]] std::optional<Molecule> molecule(const Bytes& name);
    [[nodiscard]] std::vector<Bytes> molecules();
    [[nodiscard]] std::int64_t count_molecules();

    /// One parameter of a query: bytes sent as they are, or as text.
    struct Param {
        Bytes bytes;
        bool binary;
    };

private:
    class Result;
    [[nodiscard]] Result exec(const char* sql, const std::vector<Param>& params = {});
    [[nodiscard]] std::vector<StoredAtom> read_atoms(const Result& result);
    [[nodiscard]] std::vector<Bond> read_bonds(const Result& result);
    void index(std::int64_t id, const MetadataElectron& metadata);

    pg_conn* connection_;
};

}  // namespace larry
