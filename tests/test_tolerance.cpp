// Tests for Tolerance: non-native English read as meant (K3).

#include "larry/tolerance.hpp"

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
#include <print>
#include <string>
#include <string_view>
#include <vector>

using larry::Bytes;
using larry::Reading;
using larry::Tolerance;

namespace {

Bytes b(std::string_view text) {
    return Bytes(text.begin(), text.end());
}

const larry::BaseRules& rules() {
    static const larry::BaseRules instance{larry::Language::English};
    return instance;
}

larry::Grammar& grammar() {
    static larry::Grammar instance{rules()};
    return instance;
}

// A memory that knows the words of the suite, with one category each.
larry::Memory& memory() {
    static larry::Memory* instance = [] {
        const std::filesystem::path file =
            std::filesystem::temp_directory_path() / "larry_test_tolerance.atoms";
        std::filesystem::remove(file);
        auto* m = new larry::Memory{file};
        const larry::Assimilation assimilation{rules(), nullptr, &grammar()};
        const larry::AtomOperations ops;
        const auto teach = [&](std::string_view text, std::vector<std::string_view> categories) {
            std::vector<Bytes> taught;
            for (const std::string_view c : categories) {
                taught.emplace_back(c.begin(), c.end());
            }
            const larry::Description d = assimilation.describe(ops.from_text(text), m, taught);
            m->store(d.atom, d.metadata);
        };
        teach("The sky is blue.", {"determiner", "noun", "auxiliary verb", "adjective"});
        teach("The door is closed.", {"determiner", "noun", "auxiliary verb", "adjective"});
        teach("The sky is not green.", {"determiner", "noun", "auxiliary verb", "adverb", "adjective"});
        teach("Birds fly.", {"noun", "verb"});
        teach("Birds are animals.", {"noun", "auxiliary verb", "noun"});
        teach("Tom has three apples.", {"proper noun", "verb", "numeral", "noun"});
        teach("Mary went to the kitchen.", {"proper noun", "verb", "preposition", "determiner", "noun"});
        teach("Tom and Mary have blueness.", {"proper noun", "conjunction", "proper noun", "verb", "noun"});
        teach("Snow is white.", {"noun", "auxiliary verb", "adjective"});
        teach("Very old snow is grey.", {"adverb", "adjective", "noun", "auxiliary verb", "adjective"});
        return m;
    }();
    return *instance;
}

const larry::Assimilation& assimilation() {
    static const larry::Assimilation instance{rules(), nullptr, &grammar()};
    return instance;
}

const Tolerance& tolerance() {
    static const Tolerance instance{rules(), &grammar()};
    return instance;
}

Reading read(std::string_view text) {
    const larry::AtomOperations ops;
    return tolerance().read(assimilation().describe(ops.from_text(text), &memory()), assimilation(),
                            &memory());
}

std::string meant(const Reading& r) {
    const larry::AtomOperations ops;
    return std::string{ops.text(r.meant.atom)};
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

}  // namespace

TEST(the_allowance_grows_with_the_words) {
    const Tolerance& t = tolerance();
    CHECK(t.words_per_deviation() == 4);
    CHECK(t.most_deviations() == 2);
    CHECK(t.allowed(0) == 0);
    CHECK(t.allowed(1) == 1);
    CHECK(t.allowed(4) == 1);
    CHECK(t.allowed(5) == 2);
    CHECK(t.allowed(8) == 2);
    CHECK(t.allowed(9) == 2);
    CHECK(t.allowed(40) == 2);
    CHECK(t.filler(b("determiner")) == b("the"));
    CHECK(t.filler(b("auxiliary verb")) == b("is"));
    CHECK(!t.filler(b("noun")).has_value());
    CHECK(t.agreeing(b("is"), true) == b("are"));
    CHECK(t.agreeing(b("are"), false) == b("is"));
    CHECK(!t.agreeing(b("is"), false).has_value());
    CHECK(!t.agreeing(b("can"), true).has_value());
    CHECK(t.agreeing(b("has"), true) == b("have"));
}

TEST(a_sentence_that_fits_is_read_as_said) {
    const Reading r = read("The sky is blue.");
    CHECK(r.accepted);
    CHECK(!r.changed);
    CHECK(r.deviations.empty());
    CHECK(r.allowed == 1);
    CHECK(r.pattern == "statement");
    CHECK(meant(r) == "The sky is blue.");
}

TEST(deviations_are_named_and_the_reading_changed) {
    const Reading missing = read("Sky is blue.");
    CHECK(missing.accepted);
    CHECK(missing.changed);
    CHECK(meant(missing) == "The sky is blue.");
    CHECK(missing.deviations == (std::vector<std::string>{"a missing determiner before \"Sky\" (known as \"the sky\" in 2 uses)"}));
    CHECK(missing.pattern == "statement");
    // A noun known without a determiner too is read as said.
    CHECK(!read("Snow is white.").changed);
    CHECK(read("Snow is white.").deviations.empty());
    const Reading extra = read("The the sky is blue.");
    CHECK(extra.accepted);
    CHECK(meant(extra) == "The sky is blue.");
    CHECK(extra.deviations.size() == 1);
    CHECK(!extra.deviations.empty() && extra.deviations.front().starts_with("an extra word \""));
    const Reading agreement = read("The sky are blue.");
    CHECK(agreement.accepted);
    CHECK(meant(agreement) == "The sky is blue.");
    CHECK(agreement.deviations == (std::vector<std::string>{"\"are\" where \"is\" fits (\"sky\" is singular)"}));
    const Reading plural = read("Birds is animals.");
    CHECK(meant(plural) == "Birds are animals.");
    CHECK(plural.deviations == (std::vector<std::string>{"\"is\" where \"are\" fits (\"Birds\" is plural)"}));
    // An extra word is the cheaper reading of a word that fits no place.
    const Reading extra_blue = read("The sky is blue blue.");
    CHECK(extra_blue.accepted);
    CHECK(extra_blue.deviations == (std::vector<std::string>{"an extra word \"blue\""}));
    CHECK(extra_blue.counted == 1);
    // A filler made to agree costs nothing more.
    const Reading animals = read("Birds animals.");
    CHECK(meant(animals) == "Birds are animals.");
    CHECK(animals.deviations == (std::vector<std::string>{"a missing auxiliary verb before \"animals\""}));
    CHECK(animals.counted == 1);
    // The determiner memory puts back is named, not counted.
    const Reading sky_are = read("Sky are blue.");
    CHECK(sky_are.accepted);
    CHECK(sky_are.deviations.size() == 2);
    CHECK(sky_are.counted == 1);
    CHECK(meant(sky_are) == "The sky is blue.");
    const Reading verb = read("Tom have three apples.");
    CHECK(meant(verb) == "Tom has three apples.");
    CHECK(verb.deviations == (std::vector<std::string>{"\"have\" where \"has\" fits (\"Tom\" is singular)"}));
    // The question mark stays in the reading.
    const Reading question = read("Is sky blue?");
    CHECK(question.accepted);
    CHECK(meant(question) == "Is the sky blue?");
    CHECK(question.meant.category.bytes == b("question"));
}

TEST(beyond_the_allowance_the_sentence_is_refused) {
    const Reading two = read("Blue the is sky.");
    CHECK(!two.accepted);
    CHECK(two.allowed == 1);
    CHECK(!two.changed);
    CHECK(meant(two) == "Blue the is sky.");
    CHECK(two.reason.starts_with("no pattern fits within 1 deviation for 4 words"));
    const Reading three = read("Sky is is.");
    CHECK(!three.accepted);
    CHECK(three.reason.starts_with("no pattern fits within 1 deviation for 3 words"));
    CHECK(three.allowed == 1);
    // Five words allow two deviations.
    const Reading five = read("The the door are closed.");
    CHECK(five.accepted);
    CHECK(five.deviations.size() == 2);
    CHECK(meant(five) == "The door is closed.");
}

TEST(a_word_without_a_category_is_read_as_said) {
    const Reading r = read("The zqxjkv is blue.");
    CHECK(r.accepted);
    CHECK(!r.changed);
    CHECK(r.deviations.empty());
    // An empty sentence too.
    const larry::AtomOperations ops;
    const Reading empty = tolerance().read(assimilation().describe(ops.from_text(""), &memory()),
                                           assimilation(), &memory());
    CHECK(empty.accepted);
    CHECK(empty.allowed == 0);
}

TEST(without_a_grammar_everything_is_read_as_said) {
    const Tolerance blind{rules(), nullptr};
    const larry::AtomOperations ops;
    const Reading r = blind.read(assimilation().describe(ops.from_text("Sky is blue."), &memory()),
                                 assimilation(), &memory());
    CHECK(r.accepted);
    CHECK(!r.changed);
    CHECK(r.deviations.empty());
}

TEST(the_suite_passes) {
    const std::filesystem::path suite = std::filesystem::path{LARRY_TEST_DATA_DIR} / "en" / "tolerance.txt";
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
        const std::size_t separator = text.find(" | ");
        CHECK(separator != std::string_view::npos);
        if (separator == std::string_view::npos) {
            continue;
        }
        const std::string expected{trim(text.substr(0, separator))};
        const std::string sentence{trim(text.substr(separator + 3))};
        ++cases;
        const Reading r = read(sentence);
        std::string got;
        if (!r.accepted) {
            got = "refused";
        } else if (r.deviations.empty()) {
            got = "as said";
        } else {
            got = std::to_string(r.deviations.size()) + " " + (r.changed ? meant(r) : "as said");
        }
        if (got != expected) {
            ++failed;
            std::string deviations;
            for (const std::string& d : r.deviations) {
                deviations += "; " + d;
            }
            std::println(stderr, "tolerance.txt line {}: \"{}\" expected \"{}\", got \"{}\"{}{}", number,
                         sentence, expected, got, deviations, r.reason.empty() ? "" : "; " + r.reason);
        }
    }
    CHECK(cases >= 24);
    CHECK(failed == 0);
}

TEST(the_brain_thinks_with_the_reading_and_stores_what_was_said) {
    const std::filesystem::path file =
        std::filesystem::temp_directory_path() / "larry_test_tolerance_brain.atoms";
    std::filesystem::remove(file);
    larry::Memory cache{file};
    larry::Grammar own{rules()};
    larry::Brain brain{rules(), cache, nullptr, nullptr, &own};
    const larry::Assimilation describe_with{rules(), nullptr, &own};
    const larry::AtomOperations ops;
    const auto teach = [&](std::string_view text, std::vector<std::string_view> categories) {
        std::vector<Bytes> taught;
        for (const std::string_view c : categories) {
            taught.emplace_back(c.begin(), c.end());
        }
        const larry::Description d = describe_with.describe(ops.from_text(text), &cache, taught);
        cache.store(d.atom, d.metadata);
    };
    teach("The sky is blue.", {"determiner", "noun", "auxiliary verb", "adjective"});
    teach("Birds are animals.", {"noun", "auxiliary verb", "noun"});
    teach("The door is closed.", {"determiner", "noun", "auxiliary verb", "adjective"});
    // ask: the claim is read as meant, and the reading is in the verdict.
    const larry::Verdict sky = brain.truth(ops.from_text("sky are blue"));
    CHECK(sky.truth == larry::Truth::True);
    CHECK(sky.reading == "the sky is blue");
    CHECK(sky.deviations.size() == 2);
    CHECK(sky.deviations.front().starts_with("a missing determiner before \"sky\""));
    CHECK(!sky.refused);
    const larry::Verdict green = brain.truth(ops.from_text("Sky is green."));
    CHECK(green.truth == larry::Truth::False);
    CHECK(green.reading == "The sky is green.");
    const larry::Verdict refused = brain.truth(ops.from_text("Blue the is sky."));
    CHECK(refused.truth == larry::Truth::Unknown);
    CHECK(refused.refused);
    CHECK(!refused.deviations.empty());
    CHECK(brain.truth(ops.from_text("The sky is blue.")).reading.empty());
    // say: the reply names the reading; what was said is stored.
    const std::int64_t before = cache.count();
    const larry::Reply heard = brain.hear(ops.from_text("Sky is blue."), "user:pedro");
    CHECK(heard.text == "I read it as \"The sky is blue.\". I know. The sky is blue.");
    CHECK(heard.stored);
    CHECK(cache.count() == before + 1);
    CHECK(std::ranges::contains(heard.because, std::string{"read as: The sky is blue."}));
    CHECK(std::ranges::any_of(heard.because, [](const std::string& b) { return b.starts_with("deviation: a missing determiner before \"Sky\""); }));
    CHECK(cache.containing(b("sky")).size() == 2);
    const larry::Reply question = brain.hear(ops.from_text("Is door closed?"), "user:pedro");
    CHECK(question.text == "I read it as \"Is the door closed?\". Yes.");
    const larry::Reply plural = brain.hear(ops.from_text("Birds is animals."), "user:pedro");
    CHECK(plural.text == "I read it as \"Birds are animals.\". I know. Birds are animals.");
    const larry::Reply refused_reply = brain.hear(ops.from_text("Blue the is sky."), "user:pedro");
    CHECK(refused_reply.text.starts_with("I cannot read that"));
    CHECK(!refused_reply.stored);
    CHECK(cache.count() == before + 2);  // the question and the refusal store nothing
    CHECK(!refused_reply.because.empty() && refused_reply.because.front().starts_with("rule: no pattern fits within 1 deviation for 4 words"));
    // As said, nothing is noted.
    const larry::Reply plain = brain.hear(ops.from_text("The door is closed."), "user:pedro");
    CHECK(plain.text == "I already know that.");
    CHECK(plain.because.size() == 1);
    // What was stored as said now counts as a use without a determiner: "Sky
    // is blue." is known, so "sky" is no longer known with "the" alone, until
    // a validator withdraws it.
    CHECK(brain.truth(ops.from_text("Sky is green.")).reading.empty());
    cache.add_validator("pedro");
    const std::optional<larry::StoredAtom> as_said = cache.find(describe_with.describe(ops.from_text("Sky is blue."), &cache).metadata);
    CHECK(as_said.has_value());
    if (as_said) {
        CHECK(brain.decide(as_said->description.metadata, larry::Status::Withdrawn, "pedro"));
    }
    CHECK(brain.truth(ops.from_text("Sky is green.")).reading == "The sky is green.");
}

int main() {
    return larry::test::run();
}
