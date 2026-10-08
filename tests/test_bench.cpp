// Tests for the benchmark (F8): one line per measure, on a small run.

#include "larry/bench.hpp"

#include "larry/base_rules.hpp"

#include "check.hpp"

#include <algorithm>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

using larry::Benchmark;
using larry::Measure;

namespace {

const larry::BaseRules& rules() {
    static const larry::BaseRules instance{larry::Language::English};
    return instance;
}

}  // namespace

TEST(a_small_run_measures_everything) {
    const std::filesystem::path file = std::filesystem::temp_directory_path() / "larry_test_bench.atoms";
    std::vector<std::string> phases;
    const Benchmark result = larry::bench(rules(), 300, file, [&](std::string_view what) {
        phases.emplace_back(what);
    });
    CHECK(result.atoms == 300);
    const std::vector<std::string> names = {"stored", "memory per atom", "described", "lookups",
                                            "prefix lookups", "answers", "spread", "start-up", "file per atom"};
    CHECK(result.measures.size() == names.size());
    for (std::size_t i = 0; i < names.size() && i < result.measures.size(); ++i) {
        CHECK(result.measures[i].name == names[i]);
        CHECK(result.measures[i].value >= 0);
        CHECK(!result.measures[i].unit.empty());
    }
    const auto find = [&](std::string_view name) -> const Measure* {
        const auto it = std::ranges::find(result.measures, name, &Measure::name);
        return it == result.measures.end() ? nullptr : &*it;
    };
    CHECK(find("stored") != nullptr && find("stored")->value > 0);
    CHECK(find("lookups") != nullptr && find("lookups")->value > 0);
    CHECK(find("answers") != nullptr && find("answers")->note.find("100 answered yes") != std::string::npos);
    CHECK(find("start-up") != nullptr && find("start-up")->note == "300 atoms read from the file");
    CHECK(find("file per atom") != nullptr && find("file per atom")->value > 0);
    CHECK(!std::filesystem::exists(file));
    CHECK(phases.size() == 7);
    CHECK(find("spread") != nullptr && find("spread")->note.starts_with("100 lookups of three steps"));
    CHECK(!phases.empty() && phases.front() == "storing 300 sentences");
    CHECK(phases.size() > 1 && phases[1] == "describing up to 300 sentences from the word index");
}

TEST(the_text_is_one_line_per_measure) {
    Benchmark result;
    result.measures.push_back({"stored", 24390.2, "sentences per second", "10000 taught and stored in 0.41 s"});
    result.measures.push_back({"start-up", 0.0312, "s", "10000 atoms read from the file"});
    result.measures.push_back({"answers", 12.345, "ms each", ""});
    CHECK(result.text() == "stored: 24390 sentences per second (10000 taught and stored in 0.41 s)\n"
                           "start-up: 0.03 s (10000 atoms read from the file)\n"
                           "answers: 12.3 ms each\n");
}

int main() {
    return larry::test::run();
}
