#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace larry {

class AtomOperations;

/// The atom: a sentence held as bits.
///
/// Stored as the sentence's UTF-8 bytes. Only AtomOperations reads or writes them.
class Sentence {
public:
    /// Number of bits.
    [[nodiscard]] std::size_t size() const noexcept { return bytes_.size() * 8; }

private:
    friend class AtomOperations;
    friend class Database;

    std::vector<std::uint8_t> bytes_;
};

}  // namespace larry
