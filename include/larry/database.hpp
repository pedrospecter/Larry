#pragma once

#include "larry/electron.hpp"
#include "larry/memory.hpp"
#include "larry/sentence.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <pqxx/connection>
#include <pqxx/transaction>

namespace larry {

/// The database: what the brain uses to get information. Parked (PLAN.md,
/// section 4): built only with LARRY_POSTGRES=ON. Memory is the local file
/// for now; the database can become its place of record (N2). It shares the
/// StoredAtom, WordUse, CategoryCount and Stored types with Memory.
class Database {
public:
    /// For example "host=192.168.10.133 port=5432 dbname=larry user=postgres".
    /// The password is read from ~/.pgpass.
    explicit Database(const std::string& connection);

    /// The connection string from the environment variable LARRY_DB, or
    /// "dbname=larry", the local server scripts/setup.sh prepares (F3).
    [[nodiscard]] static std::string connection_from_environment();

    /// Runs sql/schema.sql. Safe to run again.
    void apply_schema();

    /// Empties every table and restarts the ids, so that a rebuild gives the
    /// same contents byte for byte (F7).
    void clear();

    /// Store an atom under its metadata, which is where the neural network
    /// finds it, and index its words (N1).
    Stored store(const Sentence& atom, const MetadataElectron& metadata);

    /// The atom stored under this metadata.
    [[nodiscard]] std::optional<StoredAtom> find(const MetadataElectron& metadata);

    /// Every atom whose metadata starts with these bytes, in metadata order.
    [[nodiscard]] std::vector<StoredAtom> find_prefix(const Bytes& prefix);

    /// Every atom, in the order they were stored.
    [[nodiscard]] std::vector<StoredAtom> all();

    /// Where a word (as the index keys it, see AtomOperations::fold) is used,
    /// in atom and position order.
    [[nodiscard]] std::vector<WordUse> uses(const Bytes& word);

    /// The categories a word has been seen with, in category order.
    [[nodiscard]] std::vector<CategoryCount> categories_of(const Bytes& word);

    /// Rebuilds the word index from the stored atoms.
    void rebuild_index();

    [[nodiscard]] std::int64_t count();
    [[nodiscard]] std::int64_t count_words();

private:
    void index(pqxx::work& tx, std::int64_t id, const MetadataElectron& metadata);

    pqxx::connection connection_;
};

}  // namespace larry
