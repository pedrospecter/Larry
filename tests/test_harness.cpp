// Tests for Harness: the context harness, known, plausible or unusual (K4).

#include "larry/harness.hpp"

#include "larry/assimilation.hpp"
#include "larry/atom_operations.hpp"
#include "larry/base_rules.hpp"
#include "larry/brain.hpp"
#include "larry/description.hpp"
#include "larry/grammar.hpp"
#include "larry/memory.hpp"

#include "check.hpp"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <optional>
#include <print>
#include <string>
#include <string_view>
#include <vector>

using larry::Bytes;
using larry::Harness;
using larry::Judgement;
using larry::Relation;
using larry::Report;
using larry::Standing;

namespace {

std::string t(const Bytes& bytes) {
    return std::string(bytes.begin(), bytes.end());
}

const larry::BaseRules& rules() {
    static const larry::BaseRules instance{larry::Language::English};
    return instance;
}

larry::Grammar& grammar() {
    static larry::Grammar instance{rules()};
    return instance;
}

const larry::Assimilation& assimilation() {
    static const larry::Assimilation instance{rules(), nullptr, &grammar()};
    return instance;
}

void teach(larry::Memory& m, std::string_view text, std::vector<std::string_view> categories,
           std::string_view source = "lesson:test") {
    const larry::AtomOperations ops;
    std::vector<Bytes> taught;
    for (const std::string_view c : categories) {
        taught.emplace_back(c.begin(), c.end());
    }
    const larry::Description d = assimilation().describe(ops.from_text(text), &m, taught);
    m.store(d.atom, d.metadata, larry::Status::Proposed, source);
}

// The fixed memory of the suite.
larry::Memory& memory() {
    static larry::Memory* instance = [] {
        const std::filesystem::path file =
            std::filesystem::temp_directory_path() / "larry_test_harness.atoms";
        std::filesystem::remove(file);
        auto* m = new larry::Memory{file};
        teach(*m, "The sky is blue.", {"determiner", "noun", "auxiliary verb", "adjective"});
        teach(*m, "The sea is deep.", {"determiner", "noun", "auxiliary verb", "adjective"});
        teach(*m, "The sea is blue.", {"determiner", "noun", "auxiliary verb", "adjective"});
        teach(*m, "The road is wide.", {"determiner", "noun", "auxiliary verb", "adjective"});
        teach(*m, "The river is wide.", {"determiner", "noun", "auxiliary verb", "adjective"});
        teach(*m, "Water is wet.", {"noun", "auxiliary verb", "adjective"});
        teach(*m, "Birds fly.", {"noun", "verb"});
        teach(*m, "Tom has three apples.", {"proper noun", "verb", "numeral", "noun"});
        teach(*m, "Dogs eat pears.", {"noun", "verb", "noun"});
        teach(*m, "The old dog is slow.", {"determiner", "adjective", "noun", "auxiliary verb", "adjective"});
        teach(*m, "Mary went to the kitchen.", {"proper noun", "verb", "preposition", "determiner", "noun"});
        teach(*m, "Mary is in the garden.", {"proper noun", "auxiliary verb", "preposition", "determiner", "noun"});
        teach(*m, "The sky is not green.", {"determiner", "noun", "auxiliary verb", "adverb", "adjective"});
        teach(*m, "Tom is a teacher.", {"proper noun", "auxiliary verb", "determiner", "noun"});
        teach(*m, "The sea is vast?", {"determiner", "noun", "auxiliary verb", "adjective"});  // a question is no evidence
        teach(*m, "The cat is wet.", {"determiner", "noun", "auxiliary verb", "adjective"});
        teach(*m, "Dogs sleep.", {"noun", "verb"});
        teach(*m, "Books are heavy.", {"noun", "auxiliary verb", "adjective"});
        teach(*m, "The bird flies.", {"determiner", "noun", "verb"});
        return m;
    }();
    return *instance;
}

const Harness& harness() {
    static const Harness instance{rules()};
    return instance;
}

larry::Description describe(std::string_view text) {
    const larry::AtomOperations ops;
    return assimilation().describe(ops.from_text(text), &memory());
}

std::string show(const Relation& r) {
    return t(r.dependent) + ", " + t(r.kind) + " " + t(r.head);
}

std::optional<Judgement> judgement(const Report& report, std::string_view relation) {
    for (const Judgement& j : report.judgements) {
        if (show(j.relation) == relation) {
            return j;
        }
    }
    return std::nullopt;
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) {
        s.remove_suffix(1);
    }
    return s;
}

std::vector<std::string> words(const std::vector<larry::Seen>& seen) {
    std::vector<std::string> out;
    for (const larry::Seen& s : seen) {
        out.push_back(t(s.word));
    }
    return out;
}

}  // namespace

TEST(relations_come_from_the_roles) {
    const auto relations = [&](std::string_view text) {
        std::vector<std::string> out;
        for (const Relation& r : harness().relations(describe(text))) {
            out.push_back(show(r));
        }
        return out;
    };
    CHECK(relations("The sky is blue.") == (std::vector<std::string>{"blue, attribute of sky"}));
    CHECK(relations("Tom has three apples.") ==
          (std::vector<std::string>{"tom, subject of has", "apples, object of has"}));
    CHECK(relations("The old dog is slow.") ==
          (std::vector<std::string>{"slow, attribute of dog", "old, modifier of dog"}));
    CHECK(relations("Mary went to the kitchen.") ==
          (std::vector<std::string>{"mary, subject of went", "kitchen, complement of to"}));
    CHECK(relations("Is the sky blue?") == (std::vector<std::string>{"blue, attribute of sky"}));
    CHECK(relations("Hello.").empty());
    CHECK(relations("What is the sky?").empty());  // the attribute is what is asked
    CHECK(relations("Tom is a teacher.") == (std::vector<std::string>{"teacher, attribute of tom"}));
    CHECK(relations("Birds fly.") == (std::vector<std::string>{"birds, subject of fly"}));
}

TEST(known_plausible_and_unusual) {
    const Report sea = harness().judge(describe("The sea is wide."), &memory());
    CHECK(sea.judgements.size() == 1);
    const std::optional<Judgement> wide = judgement(sea, "wide, attribute of sea");
    CHECK(wide.has_value());
    if (wide) {
        CHECK(wide->standing == Standing::Plausible);
        CHECK(wide->validated == 0 && wide->proposed == 0);
        CHECK(words(wide->of_head) == (std::vector<std::string>{"deep", "blue"}));
        CHECK(words(wide->of_dependent) == (std::vector<std::string>{"road", "river"}));
        CHECK(wide->text() == "\"wide\" was never attribute of sea; of sea I know as attribute of: deep (proposed), blue (proposed); \"wide\" is attribute of: road (proposed), river (proposed)");
        CHECK(!wide->from_cloud);
    }
    CHECK(sea.unusual().empty());
    const Report vast = harness().judge(describe("The sea is vast."), &memory());
    const std::optional<Judgement> v = judgement(vast, "vast, attribute of sea");
    CHECK(v.has_value());
    if (v) {
        CHECK(v->standing == Standing::Unusual);
        CHECK(v->of_dependent.empty());
        CHECK(words(v->of_head) == (std::vector<std::string>{"deep", "blue"}));
        CHECK(v->text().ends_with("; \"vast\" is attribute of nothing I know"));
    }
    CHECK(vast.unusual().size() == 1);
    const Report sky = harness().judge(describe("The sky is blue."), &memory());
    const std::optional<Judgement> blue = judgement(sky, "blue, attribute of sky");
    CHECK(blue.has_value());
    if (blue) {
        CHECK(blue->standing == Standing::Known);
        CHECK(blue->proposed == 1);
        CHECK(blue->validated == 0);
        CHECK(blue->text() == "\"blue\" is known as attribute of sky (1 proposed)");
    }
    // Without memory there is no judgement.
    CHECK(harness().judge(describe("The sky is blue."), nullptr).judgements.empty());
}

TEST(validated_conceptions_are_the_standard) {
    const std::filesystem::path file =
        std::filesystem::temp_directory_path() / "larry_test_harness_validated.atoms";
    std::filesystem::remove(file);
    larry::Memory m{file};
    teach(m, "The sea is wide.", {"determiner", "noun", "auxiliary verb", "adjective"}, "user:ana");
    teach(m, "The sea is vast.", {"determiner", "noun", "auxiliary verb", "adjective"}, "user:pedro");
    teach(m, "The sea is deep.", {"determiner", "noun", "auxiliary verb", "adjective"}, "user:pedro");
    const larry::AtomOperations ops;
    const auto metadata = [&](std::string_view text) {
        return assimilation().describe(ops.from_text(text), &m).metadata;
    };
    m.add_validator("pedro");
    CHECK(m.set_status(metadata("The sea is vast."), larry::Status::Validated, "pedro"));
    CHECK(m.set_status(metadata("The sea is wide."), larry::Status::Withdrawn, "pedro"));
    const Report wide = harness().judge(assimilation().describe(ops.from_text("The sea is wide."), &m), &m);
    const std::optional<Judgement> w = judgement(wide, "wide, attribute of sea");
    CHECK(w.has_value());
    if (w) {
        // The withdrawn conception counts for nothing: "wide" is unusual now.
        CHECK(w->standing == Standing::Unusual);
        CHECK(w->validated == 0 && w->proposed == 0);
        // The validated attribute comes first, marked as nothing; the proposed one is marked.
        CHECK(words(w->of_head) == (std::vector<std::string>{"vast", "deep"}));
        CHECK(w->of_head.front().status == larry::Status::Validated);
        CHECK(w->text() == "\"wide\" was never attribute of sea; of sea I know as attribute of: vast, deep (proposed); \"wide\" is attribute of nothing I know");
    }
    const Report vast = harness().judge(assimilation().describe(ops.from_text("The sea is vast."), &m), &m);
    const std::optional<Judgement> v = judgement(vast, "vast, attribute of sea");
    CHECK(v.has_value());
    if (v) {
        CHECK(v->standing == Standing::Known);
        CHECK(v->validated == 1);
        CHECK(v->text() == "\"vast\" is known as attribute of sea (1 validated)");
    }
}

TEST(the_suite_passes) {
    const std::filesystem::path suite = std::filesystem::path{LARRY_TEST_DATA_DIR} / "en" / "harness.txt";
    std::ifstream in{suite, std::ios::binary};
    CHECK(static_cast<bool>(in));
    std::size_t cases = 0;
    std::size_t failed = 0;
    std::size_t number = 0;
    for (std::string line; std::getline(in, line);) {
        ++number;
        const std::string_view text = trim(line);
        if (text.empty() || text.front() == '#') {
            continue;
        }
        const std::size_t first = text.find(" | ");
        const std::size_t second = first == std::string_view::npos ? first : text.find(" | ", first + 3);
        CHECK(second != std::string_view::npos);
        if (second == std::string_view::npos) {
            continue;
        }
        const std::string expected{trim(text.substr(0, first))};
        const std::string sentence{trim(text.substr(first + 3, second - first - 3))};
        const std::string relation{trim(text.substr(second + 3))};
        ++cases;
        const Report report = harness().judge(describe(sentence), &memory());
        const std::optional<Judgement> j = judgement(report, relation);
        std::string got = j ? std::string{larry::name(j->standing)} : "no such relation";
        if (got != expected) {
            ++failed;
            std::println(stderr, "harness.txt line {}: \"{}\" {}: expected {}, got {}{}", number, sentence,
                         relation, expected, got, j ? " (" + j->text() + ")" : "");
            if (!j) {
                for (const Judgement& other : report.judgements) {
                    std::println(stderr, "  has: {}", show(other.relation));
                }
            }
        }
    }
    CHECK(cases >= 20);
    CHECK(failed == 0);
}

TEST(the_brain_reports_what_is_unusual) {
    const std::filesystem::path file =
        std::filesystem::temp_directory_path() / "larry_test_harness_brain.atoms";
    std::filesystem::remove(file);
    larry::Memory cache{file};
    larry::Grammar own{rules()};
    larry::Brain brain{rules(), cache, nullptr, nullptr, &own};
    teach(cache, "The sky is blue.", {"determiner", "noun", "auxiliary verb", "adjective"});
    teach(cache, "The sea is deep.", {"determiner", "noun", "auxiliary verb", "adjective"});
    teach(cache, "The road is wide.", {"determiner", "noun", "auxiliary verb", "adjective"});
    teach(cache, "The old dog is slow.", {"determiner", "adjective", "noun", "auxiliary verb", "adjective"});
    const larry::AtomOperations ops;
    // ask: unusual relations are in the verdict.
    const larry::Verdict vast = brain.truth(ops.from_text("The sea is vast."));
    CHECK(vast.truth == larry::Truth::Unknown);
    CHECK(vast.unusual.size() == 1);
    CHECK(!vast.unusual.empty() && vast.unusual.front().starts_with("\"vast\" was never attribute of sea; of sea I know as attribute of: deep (proposed)"));
    CHECK(brain.truth(ops.from_text("The sea is wide.")).unusual.empty());  // plausible: wide is said of the road
    CHECK(brain.truth(ops.from_text("The sky is blue.")).unusual.empty());
    // say: a stored affirmation says what is unusual; the reasons carry it too.
    const larry::Reply heard = brain.hear(ops.from_text("The sea is vast."), "user:pedro");
    CHECK(heard.stored);
    // "vast" is new to this cache, so the reply also says what it takes it for.
    CHECK(heard.text == "Noted. \"vast\" was never attribute of sea; of sea I know as attribute of: deep (proposed); \"vast\" is attribute of nothing I know. I take \"vast\" as adjective.");
    CHECK(std::ranges::any_of(heard.because, [](const std::string& b) { return b.starts_with("unusual: \"vast\" was never attribute of sea"); }));
    // Now that it is a conception, it is known.
    CHECK(brain.truth(ops.from_text("The sea is vast.")).unusual.empty());
    CHECK(brain.truth(ops.from_text("The sea is vast.")).truth == larry::Truth::True);
    const larry::Reply plain = brain.hear(ops.from_text("The sea is wide."), "user:pedro");
    CHECK(plain.text == "Noted.");
    // A question with an unusual word still gets its answer, and the note in the reasons.
    const larry::Reply question = brain.hear(ops.from_text("Is the sky old?"), "user:pedro");
    CHECK(question.text == "I don't know. I know: The sky is blue.");
    CHECK(std::ranges::any_of(question.because, [](const std::string& b) { return b.starts_with("unusual: \"old\" was never attribute of sky"); }));
}

int main() {
    return larry::test::run();
}
