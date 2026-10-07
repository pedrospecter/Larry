#include "larry/atom_operations.hpp"

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

}  // namespace

Sentence AtomOperations::from_text(std::string_view text) const {
    Sentence s;
    s.bytes_.reserve(text.size());
    for (const char c : text) {
        s.bytes_.push_back(static_cast<std::uint8_t>(c));
    }
    return s;
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
    put(m.bytes, category.bytes);
    put(m.bytes, type.bytes);
    for (const Entity& entity : entities.entities) {
        put(m.bytes, entity.word);
        put(m.bytes, entity.category);
        put(m.bytes, entity.types);
    }
    m.bytes.push_back(0x00);
    m.bytes.push_back(0x00);
    return m;
}

}  // namespace larry
