#pragma once

#include "larry/electron.hpp"
#include "larry/memory.hpp"
#include "larry/sentence.hpp"

#include <cstdint>
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

    /// Runs sql/schema.sql. Safe to run again.
    void apply_schema();

    /// Runs SQL as it is, for setup and tests.
    void run(const std::string& sql);

    /// Empties every table and restarts the ids.
    void clear();

    /// Store a conception under its metadata, index its words, and record
    /// its source. When it is already there, only the source is added.
    Stored store(const Sentence& atom, const MetadataElectron& metadata, Status status,
                 std::string_view source);

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

    void set_status(std::int64_t id, Status status);

    /// Where a word (as the index keys it) is used, in conception and position order.
    [[nodiscard]] std::vector<WordUse> uses(const Bytes& word);

    /// The categories a word has been seen with, in category order. Uses with
    /// no category do not count.
    [[nodiscard]] std::vector<CategoryCount> categories_of(const Bytes& word);

    /// The conceptions that contain a word, in the order they were stored.
    [[nodiscard]] std::vector<StoredAtom> containing(const Bytes& word);

    [[nodiscard]] std::int64_t count();
    [[nodiscard]] std::int64_t count_words();

    /// One parameter of a query: bytes sent as they are, or as text.
    struct Param {
        Bytes bytes;
        bool binary;
    };

private:
    class Result;
    [[nodiscard]] Result exec(const char* sql, const std::vector<Param>& params = {});
    [[nodiscard]] std::vector<StoredAtom> read_atoms(const Result& result);
    void index(std::int64_t id, const MetadataElectron& metadata);

    pg_conn* connection_;
};

}  // namespace larry
