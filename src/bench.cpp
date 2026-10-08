#include "larry/bench.hpp"

#include "larry/assimilation.hpp"
#include "larry/atom_operations.hpp"
#include "larry/brain.hpp"
#include "larry/memory.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <sys/resource.h>

namespace larry {

namespace {

using Clock = std::chrono::steady_clock;

double seconds_since(Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

/// The resident memory of this process at its highest, in bytes.
double resident_bytes() {
    rusage usage{};
    getrusage(RUSAGE_SELF, &usage);
#ifdef __APPLE__
    return static_cast<double>(usage.ru_maxrss);
#else
    return static_cast<double>(usage.ru_maxrss) * 1024.0;
#endif
}

Bytes b(std::string_view text) {
    return Bytes(text.begin(), text.end());
}

/// Made-up words of two syllables: "bado", "kemu"; the adjectives end in "y".
std::vector<std::string> made_up(std::size_t count, std::string_view ending) {
    static constexpr std::string_view consonants = "bdfgklmnprstvz";
    static constexpr std::string_view vowels = "aeiou";
    std::vector<std::string> syllables;
    for (const char c : consonants) {
        for (const char v : vowels) {
            syllables.push_back(std::string{c} + v);
        }
    }
    std::vector<std::string> out;
    for (const std::string& first : syllables) {
        for (const std::string& second : syllables) {
            if (out.size() >= count) {
                return out;
            }
            out.push_back(first + second + std::string{ending});
        }
    }
    // Three syllables when two are not enough.
    for (const std::string& first : syllables) {
        for (const std::string& second : syllables) {
            for (const std::string& third : syllables) {
                if (out.size() >= count) {
                    return out;
                }
                out.push_back(first + second + third + std::string{ending});
            }
        }
    }
    return out;
}

std::string plain(double value) {
    if (value >= 100) {
        return std::format("{:.0f}", value);
    }
    if (value >= 10) {
        return std::format("{:.1f}", value);
    }
    return std::format("{:.2f}", value);
}

}  // namespace

std::string Benchmark::text() const {
    std::string out;
    for (const Measure& m : measures) {
        out += std::format("{}: {} {}", m.name, plain(m.value), m.unit);
        if (!m.note.empty()) {
            out += " (" + m.note + ")";
        }
        out += '\n';
    }
    return out;
}

Benchmark bench(const BaseRules& rules, std::int64_t atoms, const std::filesystem::path& file,
                const std::function<void(std::string_view)>& progress) {
    const auto say = [&](std::string_view what) {
        if (progress) {
            progress(what);
        }
    };
    Benchmark out;
    out.atoms = std::max<std::int64_t>(atoms, 1);
    const auto count = static_cast<std::size_t>(out.atoms);
    const auto side = static_cast<std::size_t>(std::ceil(std::sqrt(static_cast<double>(count))));
    const std::vector<std::string> nouns = made_up(side, "");
    const std::vector<std::string> adjectives = made_up(side, "y");
    const auto sentence = [&](std::size_t i) {
        return std::format("The {} is {}.", nouns[i / adjectives.size()], adjectives[i % adjectives.size()]);
    };
    const auto question = [&](std::size_t i) {
        return std::format("Is the {} {}?", nouns[i / adjectives.size()], adjectives[i % adjectives.size()]);
    };
    const std::vector<Bytes> taught = {b("determiner"), b("noun"), b("auxiliary verb"), b("adjective")};
    const AtomOperations ops;
    const Assimilation assimilation{rules};
    std::filesystem::remove(file);
    std::mt19937 random{42};

    // Stored: taught and stored, as larry teach does.
    say(std::format("storing {} sentences", count));
    const double resident_before = resident_bytes();
    std::vector<MetadataElectron> stored;
    stored.reserve(count);
    double stored_seconds = 0;
    {
        Memory memory{file};
        const Clock::time_point start = Clock::now();
        for (std::size_t i = 0; i < count; ++i) {
            const Description d = assimilation.describe(ops.from_text(sentence(i)), &memory, taught);
            memory.store(d.atom, d.metadata, Status::Proposed, "bench");
            stored.push_back(d.metadata);
        }
        stored_seconds = seconds_since(start);
        out.measures.push_back({"stored", static_cast<double>(count) / stored_seconds, "sentences per second",
                                std::format("{} taught and stored in {} s", count, plain(stored_seconds))});
        const double per_atom = (resident_bytes() - resident_before) / static_cast<double>(count);
        out.measures.push_back({"memory per atom", std::max(per_atom, 0.0), "bytes",
                                "resident memory grown while storing, over the atoms"});

        // Described: the categories from the word index, as larry show does.
        // Each phase from here runs its count or its seconds, whichever ends first,
        // so that a run with a million atoms ends in minutes.
        constexpr double box = 10.0;
        const std::size_t sample = std::min<std::size_t>(count, 1000);
        say(std::format("describing up to {} sentences from the word index", sample));
        std::uniform_int_distribution<std::size_t> pick{0, count - 1};
        const Clock::time_point described_start = Clock::now();
        std::size_t described = 0;
        for (; described < sample && seconds_since(described_start) < box; ++described) {
            const Description d = assimilation.describe(ops.from_text(sentence(pick(random))), &memory);
            if (d.entities.entities.size() != 4) {
                throw std::runtime_error("bench: a generated sentence was not read as four words");
            }
        }
        const double described_seconds = seconds_since(described_start);
        out.measures.push_back({"described", static_cast<double>(described) / described_seconds, "sentences per second",
                                std::format("{} described from the word index in {} s", described, plain(described_seconds))});

        // Lookups: by metadata and by prefix.
        const std::size_t lookups = std::min<std::size_t>(count * 10, 100000);
        say(std::format("up to {} lookups by metadata", lookups));
        const Clock::time_point lookup_start = Clock::now();
        std::size_t looked_up = 0;
        for (; looked_up < lookups && seconds_since(lookup_start) < box; ++looked_up) {
            if (!memory.find(stored[pick(random)]).has_value()) {
                throw std::runtime_error("bench: a stored atom was not found");
            }
        }
        const double lookup_seconds = seconds_since(lookup_start);
        out.measures.push_back({"lookups", static_cast<double>(looked_up) / lookup_seconds, "per second",
                                std::format("{} by metadata, each found, in {} s", looked_up, plain(lookup_seconds))});
        const std::size_t prefixes = std::min<std::size_t>(count, 1000);
        say(std::format("up to {} lookups by prefix", prefixes));
        const Clock::time_point prefix_start = Clock::now();
        std::int64_t with_prefix = 0;
        std::size_t prefixed = 0;
        for (; prefixed < prefixes && seconds_since(prefix_start) < box; ++prefixed) {
            const MetadataElectron& m = stored[pick(random)];
            const Bytes prefix(m.bytes.begin(), m.bytes.begin() + static_cast<std::ptrdiff_t>(m.bytes.size() / 2));
            with_prefix += static_cast<std::int64_t>(memory.find_prefix(prefix).size());
        }
        const double prefix_seconds = seconds_since(prefix_start);
        out.measures.push_back({"prefix lookups", static_cast<double>(prefixed) / prefix_seconds, "per second",
                                std::format("{} by half a metadata, {} atoms found, in {} s", prefixed, with_prefix,
                                            plain(prefix_seconds))});

        // Answers: "Is the <noun> <adjective>?" through the brain.
        const std::size_t answers = std::min<std::size_t>(count, 100);
        say(std::format("answering up to {} questions", answers));
        Brain brain{rules, memory};
        std::int64_t yes = 0;
        std::size_t answered = 0;
        const Clock::time_point answer_start = Clock::now();
        for (; answered < answers && seconds_since(answer_start) < 2 * box; ++answered) {
            const Reply reply = brain.answer(ops.from_text(question(pick(random))));
            yes += reply.text == "Yes." || reply.text == "true" ? 1 : 0;
        }
        const double answer_seconds = seconds_since(answer_start);
        out.measures.push_back({"answers", answer_seconds * 1000.0 / static_cast<double>(std::max<std::size_t>(answered, 1)), "ms each",
                                std::format("{} yes/no questions, {} answered yes, in {} s", answered, yes,
                                            plain(answer_seconds))});
    }

    // Start-up: the file read whole, as Larry starts.
    say("starting with the file");
    const Clock::time_point open_start = Clock::now();
    std::int64_t reread = 0;
    {
        const Memory again{file};
        reread = again.count();
    }
    const double open_seconds = seconds_since(open_start);
    out.measures.push_back({"start-up", open_seconds, "s",
                            std::format("{} atoms read from the file", reread)});
    const auto file_bytes = static_cast<double>(std::filesystem::file_size(file));
    out.measures.push_back({"file per atom", file_bytes / static_cast<double>(count), "bytes",
                            std::format("{} bytes on disk", plain(file_bytes))});
    std::filesystem::remove(file);
    return out;
}

}  // namespace larry
