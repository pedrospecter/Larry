#include "larry/memory.hpp"

#include "larry/atom_operations.hpp"
#include "larry/hex.hpp"

#include <cstdlib>
#include <format>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace larry {

namespace {

std::string line_of(const MetadataElectron& metadata, const Bytes& bytes) {
    return hex::encode(metadata.bytes) + '\t' + hex::encode(bytes) + '\n';
}

}  // namespace

Memory::Memory(std::filesystem::path file) : file_(std::move(file)) {
    std::ifstream in{file_, std::ios::binary};
    if (!in) {
        return;
    }
    std::size_t number = 0;
    for (std::string line; std::getline(in, line);) {
        ++number;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            continue;
        }
        const std::size_t tab = line.find('\t');
        const std::optional<Bytes> metadata =
            tab == std::string::npos ? std::nullopt : hex::decode(std::string_view{line}.substr(0, tab));
        const std::optional<Bytes> bytes =
            tab == std::string::npos ? std::nullopt : hex::decode(std::string_view{line}.substr(tab + 1));
        if (!metadata || !bytes) {
            throw std::runtime_error(
                std::format("Memory: {} line {} is not an atom", file_.string(), number));
        }
        if (by_metadata_.contains(*metadata)) {
            throw std::runtime_error(std::format("Memory: {} line {} repeats an atom's metadata",
                                                 file_.string(), number));
        }
        atoms_.push_back({MetadataElectron{*metadata}, *bytes});
        const auto id = static_cast<std::int64_t>(atoms_.size());
        by_metadata_.emplace(*metadata, id);
        index(id, atoms_.back().metadata);
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
    for (std::size_t i = 0; i < entities.size(); ++i) {
        WordUse use{.atom = id,
                    .position = i,
                    .category = entities[i].category,
                    .before = i > 0 ? ops.fold(entities[i - 1].word) : Bytes{},
                    .after = i + 1 < entities.size() ? ops.fold(entities[i + 1].word) : Bytes{}};
        words_[ops.fold(entities[i].word)].push_back(std::move(use));
        ++word_uses_;
    }
}

Stored Memory::store(const Sentence& atom, const MetadataElectron& metadata) {
    const AtomOperations ops;
    const std::span<const std::uint8_t> bytes = ops.bytes(atom);
    const auto found = by_metadata_.find(metadata.bytes);
    if (found != by_metadata_.end()) {
        const Bytes& stored = atoms_[static_cast<std::size_t>(found->second - 1)].bytes;
        return std::ranges::equal(stored, bytes) ? Stored::Same : Stored::SameForm;
    }
    // Validate the metadata before anything is written.
    (void)ops.electrons(metadata);
    std::filesystem::create_directories(file_.parent_path().empty() ? "." : file_.parent_path());
    std::ofstream out{file_, std::ios::binary | std::ios::app};
    if (!out) {
        throw std::runtime_error(std::format("Memory: cannot write {}", file_.string()));
    }
    Bytes copy(bytes.begin(), bytes.end());
    out << line_of(metadata, copy);
    out.flush();
    if (!out) {
        throw std::runtime_error(std::format("Memory: cannot write {}", file_.string()));
    }
    atoms_.push_back({metadata, std::move(copy)});
    const auto id = static_cast<std::int64_t>(atoms_.size());
    by_metadata_.emplace(metadata.bytes, id);
    index(id, metadata);
    return Stored::New;
}

StoredAtom Memory::read(std::int64_t id) const {
    const AtomOperations ops;
    const Record& record = atoms_[static_cast<std::size_t>(id - 1)];
    StoredAtom out{.id = id, .description = {}};
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
