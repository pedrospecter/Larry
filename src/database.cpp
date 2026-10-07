#include "larry/database.hpp"

#include <span>

#include <pqxx/pqxx>

namespace larry {

Database::Database(const std::string& connection) : connection_{connection} {}

void Database::store(const Sentence& atom, const MetadataElectron& metadata) {
    pqxx::work tx{connection_};
    const pqxx::bytes_view metadata_bytes = std::as_bytes(std::span{metadata.bytes});
    const pqxx::bytes_view atom_bytes = std::as_bytes(std::span{atom.bytes_});
    tx.exec("insert into atoms (metadata, bytes) values ($1, $2)",
            pqxx::params{tx, metadata_bytes, atom_bytes})
        .no_rows();
    tx.commit();
}

}  // namespace larry
