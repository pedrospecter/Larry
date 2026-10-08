// The database without libpq: Larry builds with the cache alone, and the
// cloud says why it is not there. CMake uses this file when libpq is missing.

#include "larry/database.hpp"

#include <memory>
#include <stdexcept>

namespace larry {

namespace {

[[noreturn]] void absent() {
    throw std::runtime_error(
        "Larry was built without the cloud: install libpq (apt: libpq-dev, brew: libpq) "
        "and build again");
}

}  // namespace

class Database::Result {};

Database::Database(const std::string& /*connection*/) : connection_(nullptr) {
    absent();
}

Database::~Database() = default;

std::string Database::connection_from_environment() {
    return "";
}

std::unique_ptr<Database> Database::open(const std::string& /*connection*/) {
    absent();
}

void Database::apply_schema() {
    absent();
}

void Database::run(const std::string& /*sql*/) {
    absent();
}

void Database::clear() {
    absent();
}

Stored Database::store(const Sentence& /*atom*/, const MetadataElectron& /*metadata*/,
                       Status /*status*/, std::string_view /*source*/) {
    absent();
}

bool Database::redescribe(std::int64_t /*id*/, const MetadataElectron& /*metadata*/) {
    absent();
}

std::optional<StoredAtom> Database::find(const MetadataElectron& /*metadata*/) {
    absent();
}

std::optional<StoredAtom> Database::find_id(std::int64_t /*id*/) {
    absent();
}

std::vector<StoredAtom> Database::find_prefix(const Bytes& /*prefix*/) {
    absent();
}

std::vector<StoredAtom> Database::all() {
    absent();
}

std::vector<StoredAtom> Database::recent(std::int64_t /*count*/) {
    absent();
}

std::vector<StoredAtom> Database::with_status(Status /*status*/) {
    absent();
}

void Database::set_status(std::int64_t /*id*/, Status /*status*/, std::string_view /*by*/) {
    absent();
}

std::vector<std::string> Database::validators() {
    absent();
}

void Database::add_validator(std::string_view /*name*/) {
    absent();
}

std::vector<WordUse> Database::uses(const Bytes& /*word*/) {
    absent();
}

std::vector<CategoryCount> Database::categories_of(const Bytes& /*word*/) {
    absent();
}

std::vector<StoredAtom> Database::containing(const Bytes& /*word*/) {
    absent();
}

std::int64_t Database::count() {
    absent();
}

std::int64_t Database::count_words() {
    absent();
}

}  // namespace larry
