#include "larry/database.hpp"

#include "larry/atom_operations.hpp"

#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <span>
#include <sstream>
#include <stdexcept>

#include <pqxx/pqxx>

namespace larry {

namespace {

pqxx::bytes_view view(const Bytes& bytes) {
    return std::as_bytes(std::span{bytes});
}

Bytes from(const pqxx::bytes& bytes) {
    Bytes out;
    out.reserve(bytes.size());
    for (const std::byte b : bytes) {
        out.push_back(std::to_integer<std::uint8_t>(b));
    }
    return out;
}

const char* const select_atoms = "select id, metadata, bytes from atoms";

}  // namespace

Database::Database(const std::string& connection) : connection_{connection} {}

std::string Database::connection_from_environment() {
    const char* const from_environment = std::getenv("LARRY_DB");
    if (from_environment != nullptr && *from_environment != '\0') {
        return from_environment;
    }
    return "dbname=larry";
}

void Database::apply_schema() {
    const std::filesystem::path file = std::filesystem::path{LARRY_SQL_DIR} / "schema.sql";
    std::ifstream in{file};
    if (!in) {
        throw std::runtime_error(std::format("Database: cannot read {}", file.string()));
    }
    const std::string sql{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    pqxx::work tx{connection_};
    std::stringstream statements{sql};
    for (std::string statement; std::getline(statements, statement, ';');) {
        if (statement.find_first_not_of(" \t\r\n") == std::string::npos) {
            continue;
        }
        tx.exec(statement);
    }
    tx.commit();
}

void Database::clear() {
    pqxx::work tx{connection_};
    tx.exec("truncate atoms, words restart identity cascade");
    tx.commit();
}

void Database::index(pqxx::work& tx, std::int64_t id, const MetadataElectron& metadata) {
    const AtomOperations ops;
    const Electrons electrons = ops.electrons(metadata);
    int position = 0;
    for (const Entity& entity : electrons.entities.entities) {
        const Bytes word = ops.fold(entity.word);
        tx.exec("insert into words (word, atom, position, category) values ($1, $2, $3, $4)",
                pqxx::params{tx, view(word), static_cast<long long>(id), position,
                             view(entity.category)})
            .no_rows();
        ++position;
    }
}

Stored Database::store(const Sentence& atom, const MetadataElectron& metadata) {
    pqxx::work tx{connection_};
    const pqxx::result inserted =
        tx.exec("insert into atoms (metadata, bytes) values ($1, $2) "
                "on conflict (metadata) do nothing returning id",
                pqxx::params{tx, view(metadata.bytes), view(atom.bytes_)});
    if (inserted.empty()) {
        const pqxx::row existing =
            tx.exec("select bytes from atoms where metadata = $1",
                    pqxx::params{tx, view(metadata.bytes)})
                .one_row();
        const Bytes stored = from(existing[0].as<pqxx::bytes>());
        return stored == atom.bytes_ ? Stored::Same : Stored::SameForm;
    }
    const auto id = inserted[0][0].as<long long>();
    index(tx, id, metadata);
    tx.commit();
    return Stored::New;
}

namespace {

StoredAtom read_atom(pqxx::row_ref row) {
    const AtomOperations ops;
    StoredAtom out;
    out.id = row[0].as<long long>();
    out.description.metadata.bytes = from(row[1].as<pqxx::bytes>());
    const Bytes bytes = from(row[2].as<pqxx::bytes>());
    out.description.atom = ops.from_text(
        std::string_view{reinterpret_cast<const char*>(bytes.data()), bytes.size()});
    Electrons electrons = ops.electrons(out.description.metadata);
    out.description.category = std::move(electrons.category);
    out.description.type = std::move(electrons.type);
    out.description.entities = std::move(electrons.entities);
    out.description.image.bytes = bytes;
    return out;
}

std::vector<StoredAtom> read_atoms(const pqxx::result& rows) {
    std::vector<StoredAtom> out;
    out.reserve(rows.size());
    for (const pqxx::row_ref row : rows) {
        out.push_back(read_atom(row));
    }
    return out;
}

}  // namespace

std::optional<StoredAtom> Database::find(const MetadataElectron& metadata) {
    pqxx::read_transaction tx{connection_};
    const pqxx::result rows = tx.exec(std::string{select_atoms} + " where metadata = $1",
                                      pqxx::params{tx, view(metadata.bytes)});
    if (rows.empty()) {
        return std::nullopt;
    }
    return read_atom(rows[0]);
}

std::vector<StoredAtom> Database::find_prefix(const Bytes& prefix) {
    // Every key with the prefix lies in [prefix, upper), where upper is the
    // prefix with its last byte raised. A prefix of only 0xFF has no upper.
    Bytes upper = prefix;
    while (!upper.empty() && upper.back() == 0xFF) {
        upper.pop_back();
    }
    pqxx::read_transaction tx{connection_};
    if (upper.empty()) {
        return read_atoms(tx.exec(std::string{select_atoms} + " where metadata >= $1 order by metadata",
                                  pqxx::params{tx, view(prefix)}));
    }
    ++upper.back();
    return read_atoms(tx.exec(std::string{select_atoms} +
                                  " where metadata >= $1 and metadata < $2 order by metadata",
                              pqxx::params{tx, view(prefix), view(upper)}));
}

std::vector<StoredAtom> Database::all() {
    pqxx::read_transaction tx{connection_};
    return read_atoms(tx.exec(std::string{select_atoms} + " order by id"));
}

std::vector<WordUse> Database::uses(const Bytes& word) {
    pqxx::read_transaction tx{connection_};
    const pqxx::result rows =
        tx.exec("select atom, position, category from words where word = $1 "
                "order by atom, position",
                pqxx::params{tx, view(word)});
    std::vector<WordUse> out;
    out.reserve(rows.size());
    for (const pqxx::row_ref row : rows) {
        out.push_back({.atom = row[0].as<long long>(),
                       .position = static_cast<std::size_t>(row[1].as<int>()),
                       .category = from(row[2].as<pqxx::bytes>()),
                       .before = {},
                       .after = {}});
    }
    return out;
}

std::vector<CategoryCount> Database::categories_of(const Bytes& word) {
    pqxx::read_transaction tx{connection_};
    const pqxx::result rows =
        tx.exec("select category, count(*) from words where word = $1 "
                "group by category order by category",
                pqxx::params{tx, view(word)});
    std::vector<CategoryCount> out;
    out.reserve(rows.size());
    for (const pqxx::row_ref row : rows) {
        out.push_back({.category = from(row[0].as<pqxx::bytes>()), .count = row[1].as<long long>()});
    }
    return out;
}

void Database::rebuild_index() {
    pqxx::work tx{connection_};
    tx.exec("truncate words restart identity");
    const pqxx::result rows = tx.exec("select id, metadata from atoms order by id");
    for (const pqxx::row_ref row : rows) {
        MetadataElectron metadata;
        metadata.bytes = from(row[1].as<pqxx::bytes>());
        index(tx, row[0].as<long long>(), metadata);
    }
    tx.commit();
}

std::int64_t Database::count() {
    pqxx::read_transaction tx{connection_};
    return tx.exec("select count(*) from atoms").one_field().as<long long>();
}

std::int64_t Database::count_words() {
    pqxx::read_transaction tx{connection_};
    return tx.exec("select count(*) from words").one_field().as<long long>();
}

}  // namespace larry
