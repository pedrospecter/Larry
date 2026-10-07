#pragma once

#include "larry/electron.hpp"
#include "larry/sentence.hpp"

#include <string>

#include <pqxx/connection>

namespace larry {

/// The database: what the brain uses to get information.
class Database {
public:
    /// For example "host=192.168.10.133 port=5432 dbname=larry user=postgres".
    /// The password is read from ~/.pgpass.
    explicit Database(const std::string& connection);

    /// Store an atom under its metadata, which is where the neural network finds it.
    void store(const Sentence& atom, const MetadataElectron& metadata);

private:
    pqxx::connection connection_;
};

}  // namespace larry
