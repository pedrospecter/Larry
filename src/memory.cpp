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
//   atom       <hex metadata> <hex bytes> <status> <hex source>,<hex source>
//   describe   <hex metadata>
//   source     <hex metadata> <hex source>
//   status     <hex metadata> <status> <hex name of who decided>
//   reading    <hex metadata> <hex sentence as read>
//   validator  <hex name>
//   bond       <hex kind> <atom|entity> <hex from> <atom|entity> <hex to> <hex origin>
//   molecule   <hex name> <hex identity> <hex who> <hex when>
// with tabs between the fields. A line finds its conception by the identity
// of its metadata (Q28): the qualification and the words, not the types, so
// a conception described anew (its roles corrected) stays one conception,
// and "describe" is that: from there on the metadata is this one. A second
// "atom" line with the same identity, from before describe existed, reads
// as a describe when its description is complete. The first form of the
// file had untagged lines of two fields, metadata and bytes, which still
// read.
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

// The key of a bond's end in the maps: its kind as one byte, then its bytes.
Bytes end_key(const BondEnd& end) {
    Bytes key{static_cast<std::uint8_t>(end.kind)};
    key.insert(key.end(), end.bytes.begin(), end.bytes.end());
    return key;
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

std::string_view name(BondEnd::Kind kind) noexcept {
    return kind == BondEnd::Kind::Atom ? "atom" : "entity";
}

std::optional<BondEnd::Kind> bond_end_from(std::string_view text) noexcept {
    if (text == "atom") {
        return BondEnd::Kind::Atom;
    }
    if (text == "entity") {
        return BondEnd::Kind::Entity;
    }
    return std::nullopt;
}

BondEnd BondEnd::atom(const MetadataElectron& metadata) {
    return {Kind::Atom, AtomOperations{}.identity(metadata)};
}

BondEnd BondEnd::entity(std::string_view word) {
    return {Kind::Entity, AtomOperations{}.fold(std::span{reinterpret_cast<const std::uint8_t*>(word.data()), word.size()})};
}

Memory::Memory(std::filesystem::path file) : file_(std::move(file)) {
    std::ifstream in{file_, std::ios::binary};
    if (!in) {
        return;
    }
    const AtomOperations ops;
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
        if (tag == "validator") {
            const std::optional<Bytes> name = hex::decode(f[1]);
            if (!name || f.size() != 2) {
                bad("has a validator that is not hex bytes");
            }
            std::string text(name->begin(), name->end());
            if (!std::ranges::contains(validators_, text)) {
                validators_.push_back(std::move(text));
            }
            continue;
        }
        if (tag == "bond") {
            if (f.size() != 7) {
                bad("has a bond without its seven fields");
            }
            const std::optional<Bytes> kind = hex::decode(f[1]);
            const std::optional<BondEnd::Kind> from_kind = bond_end_from(f[2]);
            const std::optional<Bytes> from_bytes = hex::decode(f[3]);
            const std::optional<BondEnd::Kind> to_kind = bond_end_from(f[4]);
            const std::optional<Bytes> to_bytes = hex::decode(f[5]);
            const std::optional<Bytes> origin = hex::decode(f[6]);
            if (!kind || kind->empty() || !from_kind || !from_bytes || from_bytes->empty() || !to_kind ||
                !to_bytes || to_bytes->empty() || !origin) {
                bad("has a bond that is not hex bytes with its ends");
            }
            Bond bond{*kind, {*from_kind, *from_bytes}, {*to_kind, *to_bytes}, {}};
            if (!origin->empty()) {
                bond.origins.emplace_back(origin->begin(), origin->end());
            }
            add_bond(bond, false);
            continue;
        }
        if (tag == "molecule") {
            if (f.size() != 5) {
                bad("has a molecule member without its four fields");
            }
            const std::optional<Bytes> name = hex::decode(f[1]);
            const std::optional<Bytes> identity = hex::decode(f[2]);
            const std::optional<Bytes> who = hex::decode(f[3]);
            const std::optional<Bytes> when = hex::decode(f[4]);
            if (!name || name->empty() || !identity || identity->empty() || !who || !when) {
                bad("has a molecule member that is not hex bytes");
            }
            add_member(*name, *identity, std::string_view{reinterpret_cast<const char*>(who->data()), who->size()},
                       std::string_view{reinterpret_cast<const char*>(when->data()), when->size()}, false);
            continue;
        }
        if (tag != "atom" && tag != "describe" && tag != "source" && tag != "status" && tag != "reading") {
            bad("has an unknown tag");
        }
        const std::optional<Bytes> metadata = hex::decode(f[first]);
        if (!metadata) {
            bad("is not an atom");
        }
        const MetadataElectron electron{*metadata};
        Electrons electrons;  // read once: the identity, the index and the completeness come from it (N2)
        try {
            electrons = ops.electrons(electron);
        } catch (const std::invalid_argument&) {
            if (tag != "atom") {
                bad("changes an atom that is not there");
            }
            throw;
        }
        const Bytes identity = ops.identity(electrons);
        if (tag == "atom") {
            if (f.size() < first + 2) {
                bad("is not an atom");
            }
            const std::optional<Bytes> bytes = hex::decode(f[first + 1]);
            if (!bytes) {
                bad("is not an atom");
            }
            Record record{electron, *bytes, Status::Proposed, {}, {}, identity, {}};
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
            if (const auto held = by_identity_.find(identity); held != by_identity_.end()) {
                // The same conception again, from before Q28: one record, the
                // later complete description, every source.
                Record& first_record = atoms_[static_cast<std::size_t>(held->second - 1)];
                for (std::string& source : record.sources) {
                    if (!std::ranges::contains(first_record.sources, source)) {
                        first_record.sources.push_back(std::move(source));
                    }
                }
                if (first_record.metadata.bytes != electron.bytes && ops.complete(electrons)) {
                    describe(held->second, electron);
                }
                continue;
            }
            atoms_.push_back(std::move(record));
            const auto id = static_cast<std::int64_t>(atoms_.size());
            by_identity_.emplace(identity, id);
            by_metadata_.emplace(*metadata, id);
            index(id, electrons.entities.entities);
            continue;
        }
        const auto found = by_identity_.find(identity);
        if (found == by_identity_.end() || f.size() < 2) {
            bad("changes an atom that is not there");
        }
        if (tag == "describe" && f.size() == 2) {
            describe(found->second, electron);
            continue;
        }
        if (f.size() < 3) {
            bad("changes an atom that is not there");
        }
        Record& record = atoms_[static_cast<std::size_t>(found->second - 1)];
        if (tag == "source" && f.size() == 3) {
            const std::optional<Bytes> source = hex::decode(f[2]);
            if (!source) {
                bad("has a source that is not hex bytes");
            }
            std::string text(source->begin(), source->end());
            if (!std::ranges::contains(record.sources, text)) {
                record.sources.push_back(std::move(text));
            }
        } else if (tag == "reading" && f.size() == 3) {
            const std::optional<Bytes> reading = hex::decode(f[2]);
            if (!reading) {
                bad("has a reading that is not hex bytes");
            }
            record.reading.assign(reading->begin(), reading->end());
        } else if (tag == "status" && f.size() <= 4) {
            const std::optional<Status> status = status_from(f[2]);
            if (!status) {
                bad("has an unknown status");
            }
            record.status = *status;
            record.decided_by.clear();
            if (f.size() == 4) {
                const std::optional<Bytes> by = hex::decode(f[3]);
                if (!by) {
                    bad("has a name that is not hex bytes");
                }
                record.decided_by.assign(by->begin(), by->end());
            }
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
    // The validators and the bonds stay: a rebuild forgets atoms, not who
    // may validate nor what was bonded; a bond's atom end is an identity
    // (Q28), which the lessons give back.
    const std::vector<std::string> validators = std::move(validators_);
    validators_.clear();
    const std::vector<Bond> bonds = std::move(bonds_);
    bonds_.clear();
    bonds_from_.clear();
    bonds_to_.clear();
    const std::vector<Molecule> molecules = std::move(molecules_);
    molecules_.clear();
    molecule_index_.clear();
    molecules_by_identity_.clear();
    atoms_.clear();
    by_identity_.clear();
    by_metadata_.clear();
    words_.clear();
    categories_.clear();
    word_uses_ = 0;
    std::filesystem::create_directories(file_.parent_path().empty() ? "." : file_.parent_path());
    std::ofstream out{file_, std::ios::binary | std::ios::trunc};
    if (!out) {
        throw std::runtime_error(std::format("Memory: cannot write {}", file_.string()));
    }
    out.close();
    for (const std::string& validator : validators) {
        add_validator(validator);
    }
    for (const Bond& bond : bonds) {
        add_bond(bond, true);
    }
    for (const Molecule& molecule : molecules) {
        for (const Member& member : molecule.members) {
            add_member(molecule.name, member.identity, member.who, member.when, true);
        }
    }
}

std::size_t Memory::add_member(const Bytes& molecule, const Bytes& identity, std::string_view who,
                               std::string_view when, bool write) {
    if (write) {
        append(std::format("molecule{}{}{}{}{}{}{}{}", tab, hex::encode(molecule), tab, hex::encode(identity), tab,
                           hex_of(who), tab, hex_of(when)));
    }
    const auto held = molecule_index_.find(molecule);
    std::size_t index = 0;
    if (held == molecule_index_.end()) {
        molecules_.push_back({molecule, {}});
        index = molecules_.size() - 1;
        molecule_index_.emplace(molecule, index);
    } else {
        index = held->second;
    }
    Molecule& m = molecules_[index];
    m.members.push_back({identity, std::string{who}, std::string{when}});
    std::vector<Bytes>& in = molecules_by_identity_[identity];
    if (!std::ranges::contains(in, molecule)) {
        in.push_back(molecule);
    }
    return m.members.size() - 1;
}

std::size_t Memory::join(const Bytes& molecule, const Bytes& identity, std::string_view who, std::string_view when) {
    if (molecule.empty() || identity.empty()) {
        throw std::invalid_argument("Memory::join: a member needs a molecule and an identity");
    }
    return add_member(molecule, identity, who, when, true);
}

std::optional<Molecule> Memory::molecule(const Bytes& name) const {
    const auto held = molecule_index_.find(name);
    if (held == molecule_index_.end()) {
        return std::nullopt;
    }
    return molecules_[held->second];
}

std::vector<Bytes> Memory::molecules() const {
    std::vector<Bytes> out;
    for (const Molecule& m : molecules_) {
        out.push_back(m.name);
    }
    return out;
}

std::vector<Bytes> Memory::molecules_of(const Bytes& identity) const {
    const auto held = molecules_by_identity_.find(identity);
    return held == molecules_by_identity_.end() ? std::vector<Bytes>{} : held->second;
}

std::optional<StoredAtom> Memory::find_identity(const Bytes& identity) const {
    const auto found = by_identity_.find(identity);
    if (found == by_identity_.end()) {
        return std::nullopt;
    }
    return read(found->second);
}

bool Memory::add_bond(const Bond& bond, bool write) {
    const auto line = [&](std::string_view origin) {
        return std::format("bond{}{}{}{}{}{}{}{}{}{}{}{}", tab, hex::encode(bond.kind), tab, name(bond.from.kind), tab,
                           hex::encode(bond.from.bytes), tab, name(bond.to.kind), tab, hex::encode(bond.to.bytes), tab,
                           hex_of(origin));
    };
    const Bytes from_key = end_key(bond.from);
    if (const auto held = bonds_from_.find(from_key); held != bonds_from_.end()) {
        for (const std::size_t i : held->second) {
            Bond& mine = bonds_[i];
            if (!mine.same(bond)) {
                continue;
            }
            for (const std::string& origin : bond.origins) {
                if (!std::ranges::contains(mine.origins, origin)) {
                    if (write) {
                        append(line(origin));
                    }
                    mine.origins.push_back(origin);
                }
            }
            return false;
        }
    }
    if (write) {
        if (bond.origins.empty()) {
            append(line(""));
        }
        for (const std::string& origin : bond.origins) {
            append(line(origin));
        }
    }
    bonds_.push_back(bond);
    bonds_from_[from_key].push_back(bonds_.size() - 1);
    bonds_to_[end_key(bond.to)].push_back(bonds_.size() - 1);
    return true;
}

bool Memory::bond(const Bond& bond) {
    if (bond.kind.empty() || bond.from.bytes.empty() || bond.to.bytes.empty()) {
        throw std::invalid_argument("Memory::bond: a bond needs a kind and two ends");
    }
    return add_bond(bond, true);
}

std::vector<Bond> Memory::bonds_from(const BondEnd& end) const {
    std::vector<Bond> out;
    if (const auto held = bonds_from_.find(end_key(end)); held != bonds_from_.end()) {
        for (const std::size_t i : held->second) {
            out.push_back(bonds_[i]);
        }
    }
    return out;
}

std::vector<Bond> Memory::bonds_to(const BondEnd& end) const {
    std::vector<Bond> out;
    if (const auto held = bonds_to_.find(end_key(end)); held != bonds_to_.end()) {
        for (const std::size_t i : held->second) {
            out.push_back(bonds_[i]);
        }
    }
    return out;
}

std::vector<Bond> Memory::bonds_of(const BondEnd& end) const {
    std::vector<std::size_t> indexes;
    const Bytes key = end_key(end);
    if (const auto held = bonds_from_.find(key); held != bonds_from_.end()) {
        indexes.insert(indexes.end(), held->second.begin(), held->second.end());
    }
    if (const auto held = bonds_to_.find(key); held != bonds_to_.end()) {
        for (const std::size_t i : held->second) {
            if (!std::ranges::contains(indexes, i)) {
                indexes.push_back(i);
            }
        }
    }
    std::ranges::sort(indexes);
    std::vector<Bond> out;
    for (const std::size_t i : indexes) {
        out.push_back(bonds_[i]);
    }
    return out;
}

void Memory::index(std::int64_t id, const MetadataElectron& metadata) {
    index(id, AtomOperations{}.electrons(metadata).entities.entities);
}

void Memory::index(std::int64_t id, const std::vector<Entity>& entities) {
    const AtomOperations ops;
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
        Bytes word = ops.fold(entities[i].word);
        if (!use.category.empty()) {
            ++categories_[word][use.category];
        }
        words_[std::move(word)].push_back(std::move(use));
        ++word_uses_;
    }
}

void Memory::unindex(std::int64_t id) {
    const AtomOperations ops;
    const Record& record = atoms_[static_cast<std::size_t>(id - 1)];
    for (const Entity& entity : ops.electrons(record.metadata).entities.entities) {
        const auto found = words_.find(ops.fold(entity.word));
        if (found == words_.end()) {
            continue;
        }
        for (const WordUse& use : found->second) {
            if (use.atom == id && !use.category.empty()) {
                const auto counted = categories_.find(found->first);
                if (counted != categories_.end()) {
                    if (--counted->second[use.category] <= 0) {
                        counted->second.erase(use.category);
                    }
                    if (counted->second.empty()) {
                        categories_.erase(counted);
                    }
                }
            }
        }
        const auto removed = std::erase_if(found->second, [&](const WordUse& use) { return use.atom == id; });
        word_uses_ -= static_cast<std::int64_t>(removed);
        if (found->second.empty()) {
            words_.erase(found);
        }
    }
}

void Memory::describe(std::int64_t id, const MetadataElectron& metadata) {
    Record& record = atoms_[static_cast<std::size_t>(id - 1)];
    unindex(id);
    by_metadata_.erase(record.metadata.bytes);
    record.metadata = metadata;
    by_metadata_[metadata.bytes] = id;
    index(id, metadata);
}

Stored Memory::store(const Sentence& atom, const MetadataElectron& metadata, Status status,
                     std::string_view source) {
    const AtomOperations ops;
    const std::span<const std::uint8_t> bytes = ops.bytes(atom);
    const Electrons electrons = ops.electrons(metadata);  // validates before anything is written
    const Bytes identity = ops.identity(electrons);
    const auto found = by_identity_.find(identity);
    if (found != by_identity_.end()) {
        Record& record = atoms_[static_cast<std::size_t>(found->second - 1)];
        if (!source.empty() && !std::ranges::contains(record.sources, std::string{source})) {
            append(std::format("source{}{}{}{}", tab, hex::encode(record.metadata.bytes), tab,
                               hex_of(source)));
            record.sources.emplace_back(source);
        }
        return std::ranges::equal(record.bytes, bytes) ? Stored::Same : Stored::SameForm;
    }
    Bytes copy(bytes.begin(), bytes.end());
    append(std::format("atom{}{}{}{}{}{}{}{}", tab, hex::encode(metadata.bytes), tab,
                       hex::encode(copy), tab, name(status), tab,
                       source.empty() ? std::string{} : hex_of(source)));
    Record record{metadata, std::move(copy), status, {}, {}, identity, {}};
    if (!source.empty()) {
        record.sources.emplace_back(source);
    }
    atoms_.push_back(std::move(record));
    const auto id = static_cast<std::int64_t>(atoms_.size());
    by_identity_.emplace(identity, id);
    by_metadata_.emplace(metadata.bytes, id);
    index(id, electrons.entities.entities);
    return Stored::New;
}

bool Memory::redescribe(const MetadataElectron& metadata) {
    const AtomOperations ops;
    const auto found = by_identity_.find(ops.identity(metadata));
    if (found == by_identity_.end()) {
        return false;
    }
    if (atoms_[static_cast<std::size_t>(found->second - 1)].metadata.bytes == metadata.bytes) {
        return false;
    }
    append(std::format("describe{}{}", tab, hex::encode(metadata.bytes)));
    describe(found->second, metadata);
    return true;
}

bool Memory::set_status(const MetadataElectron& metadata, Status status, std::string_view by) {
    const AtomOperations ops;
    const auto found = by_identity_.find(ops.identity(metadata));
    if (found == by_identity_.end()) {
        return false;
    }
    Record& record = atoms_[static_cast<std::size_t>(found->second - 1)];
    if (record.status != status || record.decided_by != by) {
        append(std::format("status{}{}{}{}{}{}", tab, hex::encode(record.metadata.bytes), tab,
                           name(status), tab, hex_of(by)));
        record.status = status;
        record.decided_by = std::string{by};
    }
    return true;
}

bool Memory::set_reading(const MetadataElectron& metadata, std::string_view reading) {
    const AtomOperations ops;
    const auto found = by_identity_.find(ops.identity(metadata));
    if (found == by_identity_.end()) {
        return false;
    }
    Record& record = atoms_[static_cast<std::size_t>(found->second - 1)];
    if (record.reading != reading) {
        append(std::format("reading{}{}{}{}", tab, hex::encode(record.metadata.bytes), tab, hex_of(reading)));
        record.reading = std::string{reading};
    }
    return true;
}

std::vector<std::string> Memory::validators() const {
    return validators_;
}

void Memory::add_validator(std::string_view name) {
    if (name.empty() || std::ranges::contains(validators_, std::string{name})) {
        return;
    }
    append(std::format("validator{}{}", tab, hex_of(name)));
    validators_.emplace_back(name);
}

StoredAtom Memory::read(std::int64_t id) const {
    const AtomOperations ops;
    const Record& record = atoms_[static_cast<std::size_t>(id - 1)];
    StoredAtom out;
    out.id = id;
    out.status = record.status;
    out.sources = record.sources;
    out.decided_by = record.decided_by;
    out.reading = record.reading;
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
    const AtomOperations ops;
    const auto found = by_identity_.find(ops.identity(metadata));
    if (found == by_identity_.end()) {
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

const std::vector<WordUse>& Memory::uses(const Bytes& word) const {
    static const std::vector<WordUse> none;
    const auto found = words_.find(word);
    return found == words_.end() ? none : found->second;
}

std::vector<CategoryCount> Memory::categories_of(const Bytes& word) const {
    std::vector<CategoryCount> out;
    const auto counted = categories_.find(word);
    if (counted == categories_.end()) {
        return out;
    }
    out.reserve(counted->second.size());
    for (const auto& [category, count] : counted->second) {
        out.push_back({category, count});
    }
    return out;
}

std::int64_t Memory::count_uses(const Bytes& word) const {
    const auto found = words_.find(word);
    return found == words_.end() ? 0 : static_cast<std::int64_t>(found->second.size());
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
