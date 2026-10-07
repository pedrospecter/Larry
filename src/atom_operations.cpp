#include "larry/atom_operations.hpp"

#include <cstddef>
#include <format>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace larry {

namespace {

// A part is written with each 0x00 byte escaped as 0x00 0xFF, then ends with
// 0x00 0x01. A list of parts ends with 0x00 0x00. No end marker can be read as
// content, so different electrons can never produce the same metadata.
void put(Bytes& out, const Bytes& part) {
    for (const std::uint8_t b : part) {
        out.push_back(b);
        if (b == 0x00) {
            out.push_back(0xFF);
        }
    }
    out.push_back(0x00);
    out.push_back(0x01);
}

void put(Bytes& out, const std::vector<Bytes>& parts) {
    for (const Bytes& part : parts) {
        put(out, part);
    }
    out.push_back(0x00);
    out.push_back(0x00);
}

void put(Bytes& out, const Entity& entity) {
    put(out, entity.word);
    put(out, entity.category);
    put(out, entity.types);
}

// Reads parts and lists back, in the order metadata() wrote them.
class Reader {
public:
    explicit Reader(std::span<const std::uint8_t> in) : in_(in) {}

    [[nodiscard]] bool done() const noexcept { return pos_ == in_.size(); }

    /// Consumes the end of a list when it is next.
    [[nodiscard]] bool list_end() noexcept {
        if (pos_ + 1 < in_.size() && in_[pos_] == 0x00 && in_[pos_ + 1] == 0x00) {
            pos_ += 2;
            return true;
        }
        return false;
    }

    [[nodiscard]] Bytes part() {
        Bytes out;
        while (true) {
            if (pos_ >= in_.size()) {
                bad("the metadata ends inside a part");
            }
            const std::uint8_t b = in_[pos_++];
            if (b != 0x00) {
                out.push_back(b);
                continue;
            }
            if (pos_ >= in_.size()) {
                bad("the metadata ends after a marker");
            }
            const std::uint8_t marker = in_[pos_++];
            if (marker == 0xFF) {
                out.push_back(0x00);
            } else if (marker == 0x01) {
                return out;
            } else {
                bad("a list ends where a part was expected");
            }
        }
    }

    [[nodiscard]] std::vector<Bytes> list() {
        std::vector<Bytes> out;
        while (!list_end()) {
            out.push_back(part());
        }
        return out;
    }

    [[noreturn]] void bad(const char* why) const {
        throw std::invalid_argument(
            std::format("AtomOperations::electrons: {} at byte {}", why, pos_));
    }

private:
    std::span<const std::uint8_t> in_;
    std::size_t pos_ = 0;
};

}  // namespace

Sentence AtomOperations::from_text(std::string_view text) const {
    Sentence s;
    s.bytes_.reserve(text.size());
    for (const char c : text) {
        s.bytes_.push_back(static_cast<std::uint8_t>(c));
    }
    return s;
}

std::span<const std::uint8_t> AtomOperations::bytes(const Sentence& sentence) const noexcept {
    return sentence.bytes_;
}

std::string_view AtomOperations::text(const Sentence& sentence) const noexcept {
    return {reinterpret_cast<const char*>(sentence.bytes_.data()), sentence.bytes_.size()};
}

std::string AtomOperations::to_bits(const Sentence& sentence) const {
    std::string out;
    out.reserve(sentence.size());
    for (const std::uint8_t byte : sentence.bytes_) {
        for (int k = 7; k >= 0; --k) {
            out.push_back(((byte >> k) & 1) != 0 ? '1' : '0');
        }
    }
    return out;
}

MetadataElectron AtomOperations::metadata(const CategoryElectron& category,
                                          const TypeElectron& type,
                                          const EntitiesElectron& entities) const {
    MetadataElectron m;
    m.bytes = metadata_prefix(category, type, entities.entities);
    m.bytes.push_back(0x00);
    m.bytes.push_back(0x00);
    return m;
}

Bytes AtomOperations::metadata_prefix(const CategoryElectron& category, const TypeElectron& type,
                                      std::span<const Entity> leading) const {
    Bytes out;
    put(out, category.bytes);
    put(out, type.bytes);
    for (const Entity& entity : leading) {
        put(out, entity);
    }
    return out;
}

Electrons AtomOperations::electrons(const MetadataElectron& metadata) const {
    Reader in{metadata.bytes};
    Electrons e;
    e.category.bytes = in.part();
    e.type.bytes = in.part();
    while (!in.list_end()) {
        Entity entity;
        entity.word = in.part();
        entity.category = in.part();
        entity.types = in.list();
        e.entities.entities.push_back(std::move(entity));
    }
    if (!in.done()) {
        in.bad("there are bytes after the end");
    }
    return e;
}

Bytes AtomOperations::fold(std::span<const std::uint8_t> word) const {
    Bytes out(word.begin(), word.end());
    for (std::uint8_t& b : out) {
        if (b >= 'A' && b <= 'Z') {
            b = static_cast<std::uint8_t>(b - 'A' + 'a');
        }
    }
    return out;
}

}  // namespace larry
