#include "larry/database.hpp"

#include "larry/atom_operations.hpp"
#include "larry/connection.hpp"
#include "larry/hex.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <libpq-fe.h>

namespace larry {

/// A query result, freed when it goes out of scope.
class Database::Result {
public:
    explicit Result(PGresult* result) : result_(result) {}
    ~Result() { PQclear(result_); }
    Result(const Result&) = delete;
    Result& operator=(const Result&) = delete;
    Result(Result&& other) noexcept : result_(std::exchange(other.result_, nullptr)) {}

    [[nodiscard]] PGresult* get() const noexcept { return result_; }
    [[nodiscard]] int rows() const noexcept { return PQntuples(result_); }
    [[nodiscard]] bool null(int row, int column) const noexcept {
        return PQgetisnull(result_, row, column) != 0;
    }
    [[nodiscard]] Bytes bytes(int row, int column) const {
        const char* value = PQgetvalue(result_, row, column);
        const int length = PQgetlength(result_, row, column);
        return Bytes(reinterpret_cast<const std::uint8_t*>(value),
                     reinterpret_cast<const std::uint8_t*>(value) + length);
    }
    /// A binary integer column: big-endian, 8 or 4 bytes.
    [[nodiscard]] std::int64_t integer(int row, int column) const {
        const Bytes b = bytes(row, column);
        std::uint64_t value = 0;
        for (const std::uint8_t byte : b) {
            value = (value << 8) | byte;
        }
        if (b.size() == 4) {
            return static_cast<std::int32_t>(static_cast<std::uint32_t>(value));
        }
        return static_cast<std::int64_t>(value);
    }

private:
    PGresult* result_;
};

namespace {

Database::Param binary(const Bytes& bytes) {
    return {bytes, true};
}

Database::Param binary(std::span<const std::uint8_t> bytes) {
    return {Bytes(bytes.begin(), bytes.end()), true};
}

// libpq reads a text parameter as a C string, so it ends with a zero byte.
Database::Param text(std::string_view text) {
    Bytes bytes(text.begin(), text.end());
    bytes.push_back(0);
    return {std::move(bytes), false};
}

Database::Param number(std::int64_t value) {
    return text(std::to_string(value));
}

const char* const select_conceptions =
    "select c.id, c.metadata, c.bytes, c.status, "
    "(select string_agg(encode(s.source, 'hex'), ',' order by s.id) "
    " from sources s where s.conception = c.id), c.decided_by, c.reading "
    "from conceptions c";

}  // namespace

namespace {

// The server's notices ("relation already exists, skipping") are not for the user.
void quiet(void* /*arg*/, const char* /*message*/) {}

}  // namespace

Database::Database(const std::string& connection) : connection_(PQconnectdb(connection.c_str())) {
    PQsetNoticeProcessor(connection_, quiet, nullptr);
    if (PQstatus(connection_) != CONNECTION_OK) {
        const std::string why = PQerrorMessage(connection_);
        PQfinish(connection_);
        connection_ = nullptr;
        throw std::runtime_error(std::format("Database: {}", why));
    }
}

Database::~Database() {
    if (connection_ != nullptr) {
        PQfinish(connection_);
    }
}

std::string Database::connection_from_environment() {
    const char* const from_environment = std::getenv("LARRY_DB");
    if (from_environment != nullptr && *from_environment != '\0') {
        return libpq_connection(from_environment);
    }
    return "";
}

namespace {

// The value of a key in a connection string ("dbname=larry"), or empty.
std::string connection_value(const std::string& connection, std::string_view key) {
    const std::string pattern = std::string{key} + '=';
    std::size_t at = 0;
    while ((at = connection.find(pattern, at)) != std::string::npos) {
        if (at == 0 || connection[at - 1] == ' ') {
            std::size_t end = connection.find(' ', at);
            std::string value = connection.substr(at + pattern.size(),
                                                  end == std::string::npos ? std::string::npos
                                                                           : end - at - pattern.size());
            if (value.size() >= 2 && value.front() == '\'' && value.back() == '\'') {
                value = value.substr(1, value.size() - 2);
            }
            return value;
        }
        at += pattern.size();
    }
    return "";
}

// The connection string with another database name.
std::string with_database(const std::string& connection, std::string_view name) {
    std::string out;
    std::string_view rest{connection};
    bool replaced = false;
    while (!rest.empty()) {
        const std::size_t space = rest.find(' ');
        const std::string_view item = rest.substr(0, space);
        if (item.starts_with("dbname=")) {
            out += "dbname=";
            out += name;
            replaced = true;
        } else {
            out += item;
        }
        if (space == std::string_view::npos) {
            break;
        }
        out += ' ';
        rest.remove_prefix(space + 1);
    }
    if (!replaced) {
        out += out.empty() ? "" : " ";
        out += "dbname=";
        out += name;
    }
    return out;
}

bool is_safe_name(std::string_view name) {
    if (name.empty() || name.size() > 63) {
        return false;
    }
    for (const char c : name) {
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) {
            return false;
        }
    }
    return true;
}

}  // namespace

std::unique_ptr<Database> Database::open(const std::string& connection) {
    try {
        auto cloud = std::make_unique<Database>(connection);
        cloud->apply_schema();
        return cloud;
    } catch (const std::runtime_error& e) {
        const std::string why = e.what();
        const std::string name = connection_value(connection, "dbname");
        if (why.find("does not exist") == std::string::npos || !is_safe_name(name) ||
            name == "postgres") {
            throw;
        }
        // The server is there and the database is not: make it.
        Database postgres{with_database(connection, "postgres")};
        postgres.run("create database " + name);
    }
    auto cloud = std::make_unique<Database>(connection);
    cloud->apply_schema();
    return cloud;
}

Database::Result Database::exec(const char* sql, const std::vector<Param>& params) {
    std::vector<const char*> values;
    std::vector<int> lengths;
    std::vector<int> formats;
    static const char empty = '\0';
    for (const Param& param : params) {
        // A null pointer would mean SQL null: an empty value keeps a pointer.
        values.push_back(param.bytes.empty() ? &empty
                                             : reinterpret_cast<const char*>(param.bytes.data()));
        lengths.push_back(static_cast<int>(param.binary ? param.bytes.size() : param.bytes.size() - 1));
        formats.push_back(param.binary ? 1 : 0);
    }
    Result result{PQexecParams(connection_, sql, static_cast<int>(params.size()), nullptr,
                               values.data(), lengths.data(), formats.data(), 1)};
    const ExecStatusType status = PQresultStatus(result.get());
    if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK) {
        throw std::runtime_error(
            std::format("Database: {}", PQresultErrorMessage(result.get())));
    }
    return result;
}

void Database::run(const std::string& sql) {
    Result result{PQexec(connection_, sql.c_str())};
    const ExecStatusType status = PQresultStatus(result.get());
    if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK) {
        throw std::runtime_error(
            std::format("Database: {}", PQresultErrorMessage(result.get())));
    }
}

void Database::apply_schema() {
    const std::filesystem::path file = std::filesystem::path{LARRY_SQL_DIR} / "schema.sql";
    std::ifstream in{file};
    if (!in) {
        throw std::runtime_error(std::format("Database: cannot read {}", file.string()));
    }
    run(std::string{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}});
    // Q28: conceptions from before the identity existed get theirs; two rows
    // with one identity are one conception: the first keeps its description,
    // takes the sources of the other, and the other goes.
    const AtomOperations ops;
    const Result without = exec("select id, metadata from conceptions where identity is null order by id");
    for (int row = 0; row < without.rows(); ++row) {
        const std::int64_t id = without.integer(row, 0);
        const Bytes identity = ops.identity(MetadataElectron{without.bytes(row, 1)});
        const Result held = exec("select id from conceptions where identity = $1", {binary(identity)});
        if (held.rows() == 0) {
            (void)exec("update conceptions set identity = $2 where id = $1", {number(id), binary(identity)});
            continue;
        }
        const std::int64_t keeper = held.integer(0, 0);
        (void)exec("insert into sources (conception, source) select $1, source from sources where conception = $2 "
                   "on conflict (conception, source) do nothing",
                   {number(keeper), number(id)});
        (void)exec("delete from conceptions where id = $1", {number(id)});
    }
}

void Database::clear() {
    // The validators stay: a rebuild forgets atoms, not who may validate.
    run("truncate conceptions, sources, words restart identity cascade");
}

void Database::index(std::int64_t id, const MetadataElectron& metadata) {
    const AtomOperations ops;
    const std::vector<Entity> entities = ops.electrons(metadata).entities.entities;
    // A guessed category is no evidence: the index keeps it empty.
    static const Bytes guessed{'g', 'u', 'e', 's', 's', 'e', 'd'};
    const auto category_of = [&](std::size_t i) {
        return std::ranges::contains(entities[i].types, guessed) ? Bytes{} : entities[i].category;
    };
    for (std::size_t i = 0; i < entities.size(); ++i) {
        const Bytes before = i > 0 ? ops.fold(entities[i - 1].word) : Bytes{};
        const Bytes after = i + 1 < entities.size() ? ops.fold(entities[i + 1].word) : Bytes{};
        const Bytes before_category = i > 0 ? category_of(i - 1) : Bytes{};
        const Bytes after_category = i + 1 < entities.size() ? category_of(i + 1) : Bytes{};
        (void)exec("insert into words (word, conception, position, category, word_before, "
                   "word_after, category_before, category_after) "
                   "values ($1, $2, $3, $4, $5, $6, $7, $8)",
                   {binary(ops.fold(entities[i].word)), number(id),
                    number(static_cast<std::int64_t>(i)), binary(category_of(i)),
                    binary(before), binary(after), binary(before_category),
                    binary(after_category)});
    }
}

Stored Database::store(const Sentence& atom, const MetadataElectron& metadata, Status status,
                       std::string_view source) {
    const AtomOperations ops;
    (void)ops.electrons(metadata);  // validate before anything is written
    const Bytes identity = ops.identity(metadata);
    run("begin");
    try {
        const Result inserted =
            exec("insert into conceptions (identity, metadata, bytes, status) values ($1, $2, $3, $4) "
                 "on conflict (identity) do nothing returning id",
                 {binary(identity), binary(metadata.bytes), binary(ops.bytes(atom)), text(name(status))});
        std::int64_t id = 0;
        Stored outcome = Stored::New;
        if (inserted.rows() == 1) {
            id = inserted.integer(0, 0);
            index(id, metadata);
        } else {
            const Result existing = exec("select id, bytes from conceptions where identity = $1",
                                         {binary(identity)});
            id = existing.integer(0, 0);
            const Bytes stored = existing.bytes(0, 1);
            outcome = std::ranges::equal(stored, ops.bytes(atom)) ? Stored::Same : Stored::SameForm;
        }
        if (!source.empty()) {
            (void)exec("insert into sources (conception, source) values ($1, $2) "
                       "on conflict (conception, source) do nothing",
                       {number(id), text(source)});
        }
        run("commit");
        return outcome;
    } catch (...) {
        run("rollback");
        throw;
    }
}

std::vector<StoredAtom> Database::read_atoms(const Result& result) {
    const AtomOperations ops;
    std::vector<StoredAtom> out;
    out.reserve(static_cast<std::size_t>(result.rows()));
    for (int row = 0; row < result.rows(); ++row) {
        StoredAtom atom;
        atom.id = result.integer(row, 0);
        Description& d = atom.description;
        d.metadata.bytes = result.bytes(row, 1);
        const Bytes bytes = result.bytes(row, 2);
        d.atom = ops.from_text(
            std::string_view{reinterpret_cast<const char*>(bytes.data()), bytes.size()});
        Electrons electrons = ops.electrons(d.metadata);
        d.category = std::move(electrons.category);
        d.type = std::move(electrons.type);
        d.entities = std::move(electrons.entities);
        d.image.bytes = bytes;
        const Bytes status = result.bytes(row, 3);
        atom.status = status_from(std::string_view{reinterpret_cast<const char*>(status.data()),
                                                   status.size()})
                          .value_or(Status::Proposed);
        const Bytes by = result.bytes(row, 5);
        atom.decided_by.assign(by.begin(), by.end());
        const Bytes reading = result.bytes(row, 6);
        atom.reading.assign(reading.begin(), reading.end());
        if (!result.null(row, 4)) {
            const Bytes list = result.bytes(row, 4);
            std::string_view rest{reinterpret_cast<const char*>(list.data()), list.size()};
            while (!rest.empty()) {
                const std::size_t comma = rest.find(',');
                const std::optional<Bytes> source = hex::decode(rest.substr(0, comma));
                if (source) {
                    atom.sources.emplace_back(source->begin(), source->end());
                }
                if (comma == std::string_view::npos) {
                    break;
                }
                rest.remove_prefix(comma + 1);
            }
        }
        out.push_back(std::move(atom));
    }
    return out;
}

std::optional<StoredAtom> Database::find(const MetadataElectron& metadata) {
    const AtomOperations ops;
    std::vector<StoredAtom> found = read_atoms(
        exec((std::string{select_conceptions} + " where c.identity = $1").c_str(),
             {binary(ops.identity(metadata))}));
    if (found.empty()) {
        return std::nullopt;
    }
    return std::move(found.front());
}

bool Database::redescribe(std::int64_t id, const MetadataElectron& metadata) {
    const AtomOperations ops;
    (void)ops.electrons(metadata);  // validate before anything is written
    const Result held = exec("select metadata from conceptions where id = $1", {number(id)});
    if (held.rows() == 0 || held.bytes(0, 0) == metadata.bytes) {
        return false;
    }
    run("begin");
    try {
        (void)exec("update conceptions set metadata = $2 where id = $1", {number(id), binary(metadata.bytes)});
        (void)exec("delete from words where conception = $1", {number(id)});
        index(id, metadata);
        run("commit");
        return true;
    } catch (...) {
        run("rollback");
        throw;
    }
}

std::optional<StoredAtom> Database::find_id(std::int64_t id) {
    std::vector<StoredAtom> found = read_atoms(
        exec((std::string{select_conceptions} + " where c.id = $1").c_str(), {number(id)}));
    if (found.empty()) {
        return std::nullopt;
    }
    return std::move(found.front());
}

std::vector<StoredAtom> Database::find_prefix(const Bytes& prefix) {
    // Every key with the prefix lies in [prefix, upper), where upper is the
    // prefix with its last byte raised. A prefix of only 0xFF has no upper.
    Bytes upper = prefix;
    while (!upper.empty() && upper.back() == 0xFF) {
        upper.pop_back();
    }
    if (upper.empty()) {
        return read_atoms(
            exec((std::string{select_conceptions} + " where c.metadata >= $1 order by c.metadata").c_str(),
                 {binary(prefix)}));
    }
    ++upper.back();
    return read_atoms(exec((std::string{select_conceptions} +
                            " where c.metadata >= $1 and c.metadata < $2 order by c.metadata")
                               .c_str(),
                           {binary(prefix), binary(upper)}));
}

std::vector<StoredAtom> Database::all() {
    return read_atoms(exec((std::string{select_conceptions} + " order by c.id").c_str()));
}

std::vector<StoredAtom> Database::recent(std::int64_t count) {
    return read_atoms(
        exec((std::string{select_conceptions} + " order by c.id desc limit $1").c_str(),
             {number(count)}));
}

std::vector<StoredAtom> Database::with_status(Status status) {
    return read_atoms(
        exec((std::string{select_conceptions} + " where c.status = $1 order by c.id").c_str(),
             {text(name(status))}));
}

void Database::set_status(std::int64_t id, Status status, std::string_view by) {
    (void)exec("update conceptions set status = $2, decided_by = $3 where id = $1",
               {number(id), text(name(status)), text(by)});
}

void Database::set_reading(std::int64_t id, std::string_view reading) {
    (void)exec("update conceptions set reading = $2 where id = $1", {number(id), text(reading)});
}

std::vector<std::string> Database::validators() {
    const Result rows = exec("select name from validators order by id");
    std::vector<std::string> out;
    for (int row = 0; row < rows.rows(); ++row) {
        const Bytes name = rows.bytes(row, 0);
        out.emplace_back(name.begin(), name.end());
    }
    return out;
}

void Database::add_validator(std::string_view name) {
    if (name.empty()) {
        return;
    }
    (void)exec("insert into validators (name) values ($1) on conflict (name) do nothing",
               {text(name)});
}

std::vector<WordUse> Database::uses(const Bytes& word) {
    const Result rows =
        exec("select conception, position, category, word_before, word_after, category_before, "
             "category_after from words where word = $1 order by conception, position",
             {binary(word)});
    std::vector<WordUse> out;
    out.reserve(static_cast<std::size_t>(rows.rows()));
    for (int row = 0; row < rows.rows(); ++row) {
        out.push_back({.atom = rows.integer(row, 0),
                       .position = static_cast<std::size_t>(rows.integer(row, 1)),
                       .category = rows.bytes(row, 2),
                       .before = rows.bytes(row, 3),
                       .after = rows.bytes(row, 4),
                       .before_category = rows.bytes(row, 5),
                       .after_category = rows.bytes(row, 6)});
    }
    return out;
}

std::vector<CategoryCount> Database::categories_of(const Bytes& word) {
    const Result rows = exec("select category, count(*) from words "
                             "where word = $1 and octet_length(category) > 0 "
                             "group by category order by category",
                             {binary(word)});
    std::vector<CategoryCount> out;
    for (int row = 0; row < rows.rows(); ++row) {
        out.push_back({.category = rows.bytes(row, 0), .count = rows.integer(row, 1)});
    }
    return out;
}

std::vector<StoredAtom> Database::containing(const Bytes& word) {
    return read_atoms(exec((std::string{select_conceptions} +
                            " where c.id in (select conception from words where word = $1) "
                            "order by c.id")
                               .c_str(),
                           {binary(word)}));
}

std::int64_t Database::count() {
    return exec("select count(*) from conceptions").integer(0, 0);
}

std::int64_t Database::count_words() {
    return exec("select count(*) from words").integer(0, 0);
}

}  // namespace larry
