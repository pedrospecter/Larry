#include "larry/memory.hpp"

#include "larry/atom_operations.hpp"
#include "larry/hex.hpp"

#include <algorithm>
#include <cstdlib>
#include <format>
#include <fstream>
#include <map>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace larry {

namespace {

// The memory file is a log. Each line is tagged:
//   atom    <hex metadata> <hex bytes> <status> <hex source>,<hex source>
//   source  <hex metadata> <hex source>
//   status  <hex metadata> <status>
// with tabs between the fields. The first form of the file had untagged
// lines of two fields, metadata and bytes, which still read.
constexpr char tab = '\t';

std::vector<std::string_view> fields(std::string_view line) {
    std::vector<std::string_view> out;
    while (true) {
        const std::size_t next = line.find(tab);
        out.push_back(line.substr(0, next));
        if (next == std::string_view::npos) {
            break;
        }
        line.remove_prefix(next + 1);
    }
    return out;
}

std::string hex_of(std::string_view text) {
    return hex::encode(std::span{reinterpret_cast<const std::uint8_t*>(text.data()), text.size()});
}

std::vector<std::string> sources_of(std::string_view list) {
    std::vector<std::string> out;
    while (!list.empty()) {
        const std::size_t comma = list.find(',');
        const std::optional<Bytes> source = hex::decode(list.substr(0, comma));
        if (!source) {
            throw std::runtime_error("Memory: a source is not hex bytes");
        }
        out.emplace_back(source->begin(), source->end());
        if (comma == std::string_view::npos) {
            break;
        }
        list.remove_prefix(comma + 1);
    }
    return out;
}

}  // namespace

std::string_view name(Status status) noexcept {
    switch (status) {
    case Status::Proposed:
        return "proposed";
    case Status::Validated:
        return "validated";
    case Status::Withdrawn:
        return "withdrawn";
    }
    return "";
}

std::optional<Status> status_from(std::string_view text) noexcept {
    for (const Status status : {Status::Proposed, Status::Validated, Status::Withdrawn}) {
        if (name(status) == text) {
            return status;
        }
    }
    return std::nullopt;
}

Memory::Memory(std::filesystem::path file) : file_(std::move(file)) {
    std::ifstream in{file_, std::ios::binary};
    if (!in) {
        return;
    }
    std::size_t number = 0;
    const auto bad = [&](const char* why) {
        throw std::runtime_error(
            std::format("Memory: {} line {} {}", file_.string(), number, why));
    };
    for (std::string line; std::getline(in, line);) {
        ++number;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            continue;
        }
        const std::vector<std::string_view> f = fields(line);
        if (f.size() < 2) {
            bad("is not an atom");
        }
        std::string_view tag = f[0];
        std::size_t first = 1;
        if (f.size() == 2 && hex::decode(f[0])) {
            tag = "atom";  // the first form of the file
            first = 0;
        }
        const std::optional<Bytes> metadata = hex::decode(f[first]);
        if (!metadata) {
            bad("is not an atom");
        }
        if (tag == "atom") {
            if (f.size() < first + 2) {
                bad("is not an atom");
            }
            const std::optional<Bytes> bytes = hex::decode(f[first + 1]);
            if (!bytes) {
                bad("is not an atom");
            }
            if (by_metadata_.contains(*metadata)) {
                bad("repeats an atom's metadata");
            }
            Record record{MetadataElectron{*metadata}, *bytes, Status::Proposed, {}};
            if (f.size() > first + 2) {
                const std::optional<Status> status = status_from(f[first + 2]);
                if (!status) {
                    bad("has an unknown status");
                }
                record.status = *status;
            }
            if (f.size() > first + 3) {
                record.sources = sources_of(f[first + 3]);
            }
            atoms_.push_back(std::move(record));
            const auto id = static_cast<std::int64_t>(atoms_.size());
            by_metadata_.emplace(*metadata, id);
            index(id, atoms_.back().metadata);
            continue;
        }
        const auto found = by_metadata_.find(*metadata);
        if (found == by_metadata_.end() || f.size() != 3) {
            bad("changes an atom that is not there");
        }
        Record& record = atoms_[static_cast<std::size_t>(found->second - 1)];
        if (tag == "source") {
            const std::optional<Bytes> source = hex::decode(f[2]);
            if (!source) {
                bad("has a source that is not hex bytes");
            }
            std::string text(source->begin(), source->end());
            if (!std::ranges::contains(record.sources, text)) {
                record.sources.push_back(std::move(text));
            }
        } else if (tag == "status") {
            const std::optional<Status> status = status_from(f[2]);
            if (!status) {
                bad("has an unknown status");
            }
            record.status = *status;
        } else {
            bad("has an unknown tag");
        }
    }
}

std::filesystem::path Memory::file_from_environment(Language language) {
    const char* const from_environment = std::getenv("LARRY_MEMORY");
    if (from_environment != nullptr && *from_environment != '\0') {
        return from_environment;
    }
    return std::filesystem::path{LARRY_MEMORY_DIR} /
           (std::string{locale(language)} + ".atoms");
}

void Memory::append(const std::string& line) {
    std::filesystem::create_directories(file_.parent_path().empty() ? "." : file_.parent_path());
    std::ofstream out{file_, std::ios::binary | std::ios::app};
    if (!out) {
        throw std::runtime_error(std::format("Memory: cannot write {}", file_.string()));
    }
    out << line << '\n';
    out.flush();
    if (!out) {
        throw std::runtime_error(std::format("Memory: cannot write {}", file_.string()));
    }
}

void Memory::clear() {
    atoms_.clear();
    by_metadata_.clear();
    words_.clear();
    word_uses_ = 0;
    std::filesystem::create_directories(file_.parent_path().empty() ? "." : file_.parent_path());
    std::ofstream out{file_, std::ios::binary | std::ios::trunc};
    if (!out) {
        throw std::runtime_error(std::format("Memory: cannot write {}", file_.string()));
    }
}

void Memory::index(std::int64_t id, const MetadataElectron& metadata) {
    const AtomOperations ops;
    const std::vector<Entity> entities = ops.electrons(metadata).entities.entities;
    // A guessed category is no evidence: the index keeps it empty.
    static const Bytes guessed{'g', 'u', 'e', 's', 's', 'e', 'd'};
    const auto category_of = [&](std::size_t i) {
        return std::ranges::contains(entities[i].types, guessed) ? Bytes{} : entities[i].category;
    };
    for (std::size_t i = 0; i < entities.size(); ++i) {
        WordUse use{.atom = id,
                    .position = i,
                    .category = category_of(i),
                    .before = i > 0 ? ops.fold(entities[i - 1].word) : Bytes{},
                    .after = i + 1 < entities.size() ? ops.fold(entities[i + 1].word) : Bytes{},
                    .before_category = i > 0 ? category_of(i - 1) : Bytes{},
                    .after_category = i + 1 < entities.size() ? category_of(i + 1) : Bytes{}};
        words_[ops.fold(entities[i].word)].push_back(std::move(use));
        ++word_uses_;
    }
}

Stored Memory::store(const Sentence& atom, const MetadataElectron& metadata, Status status,
                     std::string_view source) {
    const AtomOperations ops;
    const std::span<const std::uint8_t> bytes = ops.bytes(atom);
    const auto found = by_metadata_.find(metadata.bytes);
    if (found != by_metadata_.end()) {
        Record& record = atoms_[static_cast<std::size_t>(found->second - 1)];
        if (!source.empty() && !std::ranges::contains(record.sources, std::string{source})) {
            append(std::format("source{}{}{}{}", tab, hex::encode(metadata.bytes), tab,
                               hex_of(source)));
            record.sources.emplace_back(source);
        }
        return std::ranges::equal(record.bytes, bytes) ? Stored::Same : Stored::SameForm;
    }
    (void)ops.electrons(metadata);  // validate before anything is written
    Bytes copy(bytes.begin(), bytes.end());
    append(std::format("atom{}{}{}{}{}{}{}{}", tab, hex::encode(metadata.bytes), tab,
                       hex::encode(copy), tab, name(status), tab,
                       source.empty() ? std::string{} : hex_of(source)));
    Record record{metadata, std::move(copy), status, {}};
    if (!source.empty()) {
        record.sources.emplace_back(source);
    }
    atoms_.push_back(std::move(record));
    const auto id = static_cast<std::int64_t>(atoms_.size());
    by_metadata_.emplace(metadata.bytes, id);
    index(id, metadata);
    return Stored::New;
}

bool Memory::set_status(const MetadataElectron& metadata, Status status) {
    const auto found = by_metadata_.find(metadata.bytes);
    if (found == by_metadata_.end()) {
        return false;
    }
    Record& record = atoms_[static_cast<std::size_t>(found->second - 1)];
    if (record.status != status) {
        append(std::format("status{}{}{}{}", tab, hex::encode(metadata.bytes), tab, name(status)));
        record.status = status;
    }
    return true;
}

StoredAtom Memory::read(std::int64_t id) const {
    const AtomOperations ops;
    const Record& record = atoms_[static_cast<std::size_t>(id - 1)];
    StoredAtom out;
    out.id = id;
    out.status = record.status;
    out.sources = record.sources;
    Description& d = out.description;
    d.atom = ops.from_text(
        std::string_view{reinterpret_cast<const char*>(record.bytes.data()), record.bytes.size()});
    d.metadata = record.metadata;
    Electrons electrons = ops.electrons(record.metadata);
    d.category = std::move(electrons.category);
    d.type = std::move(electrons.type);
    d.entities = std::move(electrons.entities);
    d.image.bytes = record.bytes;
    return out;
}

std::optional<StoredAtom> Memory::find(const MetadataElectron& metadata) const {
    const auto found = by_metadata_.find(metadata.bytes);
    if (found == by_metadata_.end()) {
        return std::nullopt;
    }
    return read(found->second);
}

std::optional<StoredAtom> Memory::find_id(std::int64_t id) const {
    if (id < 1 || id > static_cast<std::int64_t>(atoms_.size())) {
        return std::nullopt;
    }
    return read(id);
}

std::vector<StoredAtom> Memory::find_prefix(const Bytes& prefix) const {
    std::vector<StoredAtom> out;
    for (auto it = by_metadata_.lower_bound(prefix); it != by_metadata_.end(); ++it) {
        const Bytes& key = it->first;
        if (key.size() < prefix.size() || !std::equal(prefix.begin(), prefix.end(), key.begin())) {
            break;
        }
        out.push_back(read(it->second));
    }
    return out;
}

std::vector<StoredAtom> Memory::all() const {
    std::vector<StoredAtom> out;
    out.reserve(atoms_.size());
    for (std::size_t i = 0; i < atoms_.size(); ++i) {
        out.push_back(read(static_cast<std::int64_t>(i + 1)));
    }
    return out;
}

std::vector<StoredAtom> Memory::recent(std::int64_t count) const {
    std::vector<StoredAtom> out;
    for (std::int64_t id = static_cast<std::int64_t>(atoms_.size()); id >= 1 && count > 0;
         --id, --count) {
        out.push_back(read(id));
    }
    return out;
}

std::vector<StoredAtom> Memory::with_status(Status status) const {
    std::vector<StoredAtom> out;
    for (std::size_t i = 0; i < atoms_.size(); ++i) {
        if (atoms_[i].status == status) {
            out.push_back(read(static_cast<std::int64_t>(i + 1)));
        }
    }
    return out;
}

std::vector<WordUse> Memory::uses(const Bytes& word) const {
    const auto found = words_.find(word);
    if (found == words_.end()) {
        return {};
    }
    return found->second;
}

std::vector<CategoryCount> Memory::categories_of(const Bytes& word) const {
    std::map<Bytes, std::int64_t> counts;
    for (const WordUse& use : uses(word)) {
        if (!use.category.empty()) {
            ++counts[use.category];
        }
    }
    std::vector<CategoryCount> out;
    out.reserve(counts.size());
    for (const auto& [category, count] : counts) {
        out.push_back({category, count});
    }
    return out;
}

std::vector<Bytes> Memory::words() const {
    std::vector<Bytes> out;
    out.reserve(words_.size());
    for (const auto& [word, uses] : words_) {
        out.push_back(word);
    }
    return out;
}

std::vector<StoredAtom> Memory::containing(const Bytes& word) const {
    std::vector<StoredAtom> out;
    std::int64_t last = 0;
    for (const WordUse& use : uses(word)) {
        if (use.atom != last) {
            out.push_back(read(use.atom));
            last = use.atom;
        }
    }
    return out;
}

}  // namespace larry
