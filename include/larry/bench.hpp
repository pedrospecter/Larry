#pragma once

#include "larry/base_rules.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace larry {

/// One thing the benchmark measured (F8).
struct Measure {
    std::string name;   ///< "stored", "described", "lookups", "prefix lookups", "answers", "start-up", "memory per atom", "file per atom".
    double value = 0;
    std::string unit;   ///< "sentences per second", "ms", "bytes", ...
    std::string note;   ///< How it was measured: "10000 sentences in 0.41 s".
};

/// What `larry bench` found, with the atoms it generated.
struct Benchmark {
    std::int64_t atoms = 0;
    std::vector<Measure> measures;
    /// One line per measure: "stored: 24390 sentences per second (10000 in 0.41 s)".
    [[nodiscard]] std::string text() const;
};

/// F8: measures Larry with generated atoms in a scratch memory file. The
/// atoms are sentences "The <noun> is <adjective>." with made-up words, so
/// that `atoms` of them are distinct. It measures how many sentences are
/// taught and stored per second, how many are described from the word
/// index per second, how many lookups by metadata and by prefix the cache
/// does per second, the time to answer "Is the <noun> <adjective>?", the
/// time to start with the file, and the memory and file bytes per atom.
/// Without a cloud, a dictionary or a grammar: the cache alone. The file
/// is removed at the end. After the storing, each phase runs its count or
/// ten seconds (twenty for the answers), whichever ends first, so that a
/// run with a million atoms ends in minutes. `progress` is told each phase
/// as it starts.
[[nodiscard]] Benchmark bench(const BaseRules& rules, std::int64_t atoms, const std::filesystem::path& file,
                              const std::function<void(std::string_view)>& progress = {});

}  // namespace larry
