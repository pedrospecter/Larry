// Tests for Brain: is a concept true, false or unknown?

#include "larry/brain.hpp"

#include "larry/assimilation.hpp"
#include "larry/atom_operations.hpp"
#include "larry/base_rules.hpp"
#include "larry/database.hpp"
#include "larry/dictionary.hpp"
#include "larry/memory.hpp"

#include "check.hpp"

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <memory>
#include <optional>
#include <print>
#include <utility>

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

using larry::Bytes;
using larry::Truth;
using larry::Verdict;

namespace {

Bytes b(std::string_view text) {
    return Bytes(text.begin(), text.end());
}

const larry::BaseRules& rules() {
    static const larry::BaseRules instance{larry::Language::English};
    return instance;
}

// A memory taught a few conceptions, in a scratch file.
larry::Memory& memory() {
    static larry::Memory* instance = [] {
        const std::filesystem::path file =
            std::filesystem::temp_directory_path() / "larry_test_brain.atoms";
        std::filesystem::remove(file);
        auto* m = new larry::Memory{file};
        const larry::Assimilation assimilation{rules()};
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
        teach("The door is not closed.", {"determiner", "noun", "auxiliary verb", "adverb", "adjective"});
        teach("Penguins do not fly.", {"noun", "auxiliary verb", "adverb", "verb"});
        teach("Birds fly.", {"noun", "verb"});
        teach("Tom has three apples.", {"proper noun", "verb", "numeral", "noun"});
        teach("Birds can fly.", {"noun", "auxiliary verb", "verb"});
        teach("Is the sky blue?", {"auxiliary verb", "determiner", "noun", "adjective"});
        teach("Suppose the sky is green.", {"verb", "determiner", "noun", "auxiliary verb", "adjective"});
        teach("Mary went to the kitchen.", {"proper noun", "verb", "preposition", "determiner", "noun"});
        return m;
    }();
    return *instance;
}

const larry::Brain& brain() {
    static const larry::Brain instance{rules(), memory()};
    return instance;
}

Verdict ask(std::string_view text) {
    const larry::AtomOperations ops;
    return brain().truth(ops.from_text(text));
}

std::string first_because(const Verdict& v) {
    const larry::AtomOperations ops;
    return v.because.empty() ? std::string{} : std::string{ops.text(v.because.front().description.atom)};
}

std::vector<std::string> words(const larry::Core& core) {
    std::vector<std::string> out;
    for (const Bytes& w : core.words) {
        out.emplace_back(w.begin(), w.end());
    }
    return out;
}

}  // namespace

TEST(core_expands_contractions_and_removes_negation) {
    const larry::AtomOperations ops;
    const larry::Assimilation assimilation{rules()};
    const auto core = [&](std::string_view text) {
        return brain().core(assimilation.describe(ops.from_text(text), &memory()));
    };
    CHECK(words(core("The sky is blue.")) == (std::vector<std::string>{"the", "sky", "is", "blue"}));
    CHECK(!core("The sky is blue.").negated);
    CHECK(words(core("The sky isn't blue.")) == (std::vector<std::string>{"the", "sky", "is", "blue"}));
    CHECK(core("The sky isn't blue.").negated);
    CHECK(core("The sky is not blue.").negated);
    CHECK(words(core("Penguins don't fly.")) == (std::vector<std::string>{"penguins", "fly"}));
    CHECK(core("Penguins don't fly.").negated);
    CHECK(words(core("Penguins cannot fly.")) == (std::vector<std::string>{"penguins", "can", "fly"}));
    CHECK(!core("It is not not true.").negated);  // two negations cancel
    CHECK(words(core("I'm here.")) == (std::vector<std::string>{"i", "am", "here"}));
}

TEST(statements_of_a_yes_no_question) {
    const larry::AtomOperations ops;
    const larry::Assimilation assimilation{rules()};
    const auto statements = [&](std::string_view text) {
        return brain().statements(assimilation.describe(ops.from_text(text), &memory()));
    };
    const std::vector<larry::Core> is_blue = statements("Is the sky blue?");
    CHECK(is_blue.size() == 2);
    CHECK(is_blue.size() == 2 && words(is_blue[0]) == (std::vector<std::string>{"the", "is", "sky", "blue"}));
    CHECK(is_blue.size() == 2 && words(is_blue[1]) == (std::vector<std::string>{"the", "sky", "is", "blue"}));
    const std::vector<larry::Core> do_fly = statements("Do birds fly?");
    CHECK(do_fly.size() == 1);
    CHECK(do_fly.size() == 1 && words(do_fly[0]) == (std::vector<std::string>{"birds", "fly"}));
    CHECK(statements("The sky is blue.").empty());
    CHECK(statements("What is the sky?").empty());
    CHECK(statements("Blue?").empty());
    const std::vector<larry::Core> negative = statements("Isn't the sky blue?");
    CHECK(negative.size() == 2);
    for (const larry::Core& c : negative) {
        CHECK(!c.negated);
    }
    const std::vector<larry::Core> inside = statements("Is the door not closed?");
    CHECK(inside.size() == 3);
    CHECK(inside.size() == 3 && inside[1].negated);
    CHECK(inside.size() == 3 && words(inside[1]) == (std::vector<std::string>{"the", "door", "is", "closed"}));
}

TEST(a_conception_makes_a_concept_true) {
    CHECK(ask("the sky is blue").truth == Truth::True);
    CHECK(first_because(ask("the sky is blue")) == "The sky is blue.");
    CHECK(ask("The sky is blue!").truth == Truth::True);
    CHECK(ask("THE SKY IS BLUE").truth == Truth::True);
    CHECK(ask("the door is not closed").truth == Truth::True);
    CHECK(ask("the door isn't closed").truth == Truth::True);
    CHECK(ask("penguins do not fly").truth == Truth::True);
    CHECK(ask("penguins don't fly").truth == Truth::True);
    CHECK(ask("birds fly").truth == Truth::True);
    CHECK(ask("Tom has three apples").truth == Truth::True);
    CHECK(ask("Mary went to the kitchen").truth == Truth::True);
}

TEST(the_opposite_polarity_makes_a_concept_false) {
    CHECK(ask("the sky is not blue").truth == Truth::False);
    CHECK(first_because(ask("the sky is not blue")) == "The sky is blue.");
    CHECK(ask("the sky isn't blue").truth == Truth::False);
    CHECK(ask("the door is closed").truth == Truth::False);
    CHECK(first_because(ask("the door is closed")) == "The door is not closed.");
    CHECK(ask("penguins fly").truth == Truth::False);
    CHECK(ask("birds do not fly").truth == Truth::False);
    CHECK(ask("birds don't fly").truth == Truth::False);
}

TEST(nothing_in_memory_is_unknown) {
    const Verdict high = ask("the sky is high");
    CHECK(high.truth == Truth::Unknown);
    CHECK(high.because.empty());
    CHECK(!high.nearest.empty());
    Verdict nearest;
    nearest.truth = Truth::True;
    nearest.because = high.nearest;
    CHECK(first_because(nearest) == "The sky is blue.");
    CHECK(ask("Tom has many apples").truth == Truth::Unknown);
    CHECK(ask("penguins can fly").truth == Truth::Unknown);
    CHECK(ask("Mary went to the garden").truth == Truth::Unknown);
    CHECK(ask("the moon is made of cheese").truth == Truth::Unknown);
    CHECK(ask("the moon is made of cheese").nearest.empty());
    CHECK(ask("").truth == Truth::Unknown);
    // Questions and assumptions in memory are no evidence: only affirmations.
    CHECK(ask("suppose the sky is green").truth == Truth::Unknown);
    // K1: "the sky is green" is false, since the sky is blue (exclusive attributes).
    CHECK(ask("the sky is green").truth == Truth::False);
    CHECK(ask("Tom has five apples").truth == Truth::False);
}

TEST(yes_no_questions_are_answered_from_their_statements) {
    CHECK(ask("Is the sky blue?").truth == Truth::True);
    CHECK(first_because(ask("Is the sky blue?")) == "The sky is blue.");
    CHECK(ask("Is the sky green?").truth == Truth::False);  // K1: the sky is blue
    CHECK(ask("Isn't the sky blue?").truth == Truth::True);
    CHECK(ask("Is the door closed?").truth == Truth::False);
    CHECK(ask("Is the door not closed?").truth == Truth::True);
    CHECK(ask("Do birds fly?").truth == Truth::True);
    CHECK(ask("Do penguins fly?").truth == Truth::False);
    CHECK(ask("Can birds fly?").truth == Truth::True);
    CHECK(ask("Can penguins fly?").truth == Truth::Unknown);
    CHECK(ask("Does Tom have three apples?").truth == Truth::Unknown);  // "have" is not "has" (A4)
    CHECK(ask("Is the sky blue").truth == Truth::True);                   // no mark: "is" is known
}

namespace {

// A brain of its own for hearing, so that what it stores does not change
// the memory above.
larry::Brain& hearing_brain() {
    static larry::Brain* instance = [] {
        const std::filesystem::path file =
            std::filesystem::temp_directory_path() / "larry_test_brain_hear.atoms";
        std::filesystem::remove(file);
        auto* m = new larry::Memory{file};
        const larry::Assimilation assimilation{rules()};
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
        teach("The door is not closed.", {"determiner", "noun", "auxiliary verb", "adverb", "adjective"});
        teach("Tom is a teacher.", {"proper noun", "auxiliary verb", "determiner", "noun"});
        teach("Mary went to the kitchen.", {"proper noun", "verb", "preposition", "determiner", "noun"});
        teach("The capital of France is Paris.", {"determiner", "noun", "preposition", "proper noun", "auxiliary verb", "proper noun"});
        teach("London is the capital of England.", {"proper noun", "auxiliary verb", "determiner", "noun", "preposition", "proper noun"});
        teach("Close the door.", {"verb", "determiner", "noun"});
        teach("What colour is the sky?", {"pronoun", "noun", "auxiliary verb", "determiner", "noun"});
        return new larry::Brain{rules(), *m};
    }();
    return *instance;
}

larry::Reply say(std::string_view text) {
    const larry::AtomOperations ops;
    return hearing_brain().hear(ops.from_text(text));
}

}  // namespace

TEST(hear_answers_questions) {
    CHECK(say("Is the sky blue?").text == "Yes.");
    CHECK(say("Is the sky blue?").because == (std::vector<std::string>{"The sky is blue."}));
    CHECK(say("Is the door closed?").text == "No.");
    CHECK(say("Is the sky green?").text == "No.");  // K1: the sky is blue
    CHECK(say("Is the sky high?").text.starts_with("I don't know."));
    CHECK(say("Is the moon made of cheese?").text == "I don't know.");
    CHECK(say("What is the sky?").text == "The sky is blue.");
    CHECK(say("What colour is the sky?").text == "The sky is blue.");
    CHECK(say("Who is Tom?").text == "Tom is a teacher.");
    CHECK(say("What is the capital of France?").text == "The capital of France is Paris.");
    CHECK(say("What is the capital of England?").text == "London is the capital of England.");
    CHECK(say("What is London?").text == "London is the capital of England.");
    CHECK(say("Who went to the kitchen?").text == "Mary went to the kitchen.");
    CHECK(say("Where did Mary go?").text == "I don't know.");
    CHECK(say("What is the door?").text.starts_with("I don't know."));  // "not closed" is no answer
    CHECK(say("What is the sky?").because == (std::vector<std::string>{"The sky is blue."}));
}

TEST(answer_replies_to_questions_and_judges_claims_without_storing) {
    const larry::Memory& memory = hearing_brain().memory();
    const larry::AtomOperations ops;
    larry::Brain& brain = hearing_brain();
    const std::int64_t before = memory.count();
    CHECK(brain.answer(ops.from_text("Is the sky blue?")).text == "Yes.");
    CHECK(brain.answer(ops.from_text("Is the sky blue?")).because == (std::vector<std::string>{"The sky is blue."}));
    CHECK(brain.answer(ops.from_text("Is the door closed?")).text == "No.");
    CHECK(brain.answer(ops.from_text("What is the sky?")).text == "The sky is blue.");
    CHECK(brain.answer(ops.from_text("Who is Tom?")).text == "Tom is a teacher.");
    CHECK(brain.answer(ops.from_text("Is the moon made of cheese?")).text == "I don't know.");
    // A claim is judged, with the conception and its standing.
    const larry::Reply claim = brain.answer(ops.from_text("The sky is blue."));
    CHECK(claim.text == "true");
    CHECK(claim.because == (std::vector<std::string>{"The sky is blue. (proposed)"}));
    CHECK(!claim.stored);
    const larry::Reply wrong = brain.answer(ops.from_text("The door is closed."));
    CHECK(wrong.text == "false");
    CHECK(std::ranges::contains(wrong.because, std::string{"The door is not closed. (proposed)"}));
    const larry::Reply unknown = brain.answer(ops.from_text("Mary went to the garden."));
    CHECK(unknown.text == "I don't know. I know: Mary went to the kitchen.");
    CHECK(std::ranges::contains(unknown.because, std::string{"nearest: Mary went to the kitchen."}));
    // The rest is answered in kind, and nothing is stored.
    CHECK(brain.answer(ops.from_text("Suppose the sky is red.")).text == "That is an assumption: I do not judge it.");
    CHECK(brain.answer(ops.from_text("Close the door.")).text == "I cannot do that yet.");
    CHECK(brain.answer(ops.from_text("Hello!")).text == "Hello!");
    CHECK(memory.count() == before);
}

TEST(hear_stores_affirmations_and_checks_novelty) {
    const larry::Memory& memory = hearing_brain().memory();
    const std::int64_t before = memory.count();
    const larry::Reply same = say("The sky is blue.");
    CHECK(same.text == "I already know that.");
    CHECK(!same.stored);
    CHECK(memory.count() == before);
    const larry::Reply paraphrase = say("the sky is blue");
    CHECK(paraphrase.text == "I know. The sky is blue.");
    CHECK(paraphrase.stored);
    const larry::Reply fresh = say("The door is blue.");
    CHECK(fresh.text == "Noted.");
    CHECK(fresh.stored);
    CHECK(say("Is the door blue?").text == "Yes.");
    CHECK(say("The sea is blue.").text == "Noted. I take \"sea\" as noun.");
    CHECK(say("Zorp.").text == "Noted. What is \"Zorp\"?");
    const larry::Reply conflict = say("The sky is not blue.");
    CHECK(conflict.text.starts_with("That conflicts with what I know: The sky is blue."));
    CHECK(conflict.stored);
    const larry::Reply unknown = say("The sky is azure.");
    CHECK(unknown.text.starts_with("Noted. \"azure\" was never attribute of sky; of sky I know as attribute of: blue (proposed)"));
    CHECK(unknown.text.ends_with(" I take \"azure\" as adjective."));
    CHECK(unknown.stored);
}

TEST(only_a_validator_decides) {
    const std::filesystem::path file =
        std::filesystem::temp_directory_path() / "larry_test_brain_validate.atoms";
    std::filesystem::remove(file);
    larry::Memory cache{file};
    larry::Brain brain{rules(), cache};
    const larry::Assimilation assimilation{rules()};
    const larry::AtomOperations ops;
    const auto describe = [&](std::string_view text, std::vector<std::string_view> categories) {
        std::vector<Bytes> taught;
        for (const std::string_view c : categories) {
            taught.emplace_back(c.begin(), c.end());
        }
        return assimilation.describe(ops.from_text(text), &cache, taught);
    };
    const larry::Description sky = describe("The sky is blue.", {"determiner", "noun", "auxiliary verb", "adjective"});
    CHECK(brain.remember(sky, larry::Status::Proposed, "lesson:1") == larry::Stored::New);
    // A second source never validates by itself: the user does.
    const larry::Reply heard = brain.hear(ops.from_text("The sky is blue."), "user:pedro");
    CHECK(heard.text == "I already know that.");
    CHECK(cache.find(sky.metadata)->status == larry::Status::Proposed);
    CHECK(cache.find(sky.metadata)->sources == (std::vector<std::string>{"lesson:1", "user:pedro"}));
    CHECK(brain.proposed().size() == 1);
    // Nobody may validate until a validator is added; anyone adds the first.
    CHECK(brain.validators().empty());
    CHECK(!brain.is_validator("pedro"));
    CHECK_THROWS(brain.decide(sky.metadata, larry::Status::Validated, "pedro"), std::runtime_error);
    CHECK(cache.find(sky.metadata)->status == larry::Status::Proposed);
    CHECK(brain.add_validator("pedro", "pedro"));
    CHECK(brain.validators() == std::vector<std::string>{"pedro"});
    CHECK(brain.is_validator("pedro"));
    // Then only a validator adds another, and only a validator decides.
    CHECK(!brain.add_validator("claude", "claude"));
    CHECK(!brain.add_validator("claude", ""));
    CHECK(brain.validators().size() == 1);
    CHECK_THROWS(brain.decide(sky.metadata, larry::Status::Validated, "claude"), std::runtime_error);
    CHECK_THROWS(brain.decide(sky.metadata, larry::Status::Validated, ""), std::runtime_error);
    CHECK(cache.find(sky.metadata)->status == larry::Status::Proposed);
    CHECK(brain.decide(sky.metadata, larry::Status::Validated, "pedro"));
    CHECK(cache.find(sky.metadata)->status == larry::Status::Validated);
    CHECK(cache.find(sky.metadata)->decided_by == "pedro");
    CHECK(brain.proposed().empty());
    CHECK(brain.add_validator("ana", "pedro"));
    CHECK(brain.validators() == (std::vector<std::string>{"pedro", "ana"}));
    // Validated evidence stays evidence; withdrawn evidence does not.
    CHECK(brain.truth(ops.from_text("the sky is blue")).truth == Truth::True);
    CHECK(brain.decide(sky.metadata, larry::Status::Withdrawn, "ana"));
    CHECK(brain.truth(ops.from_text("the sky is blue")).truth == Truth::Unknown);
    CHECK(cache.find(sky.metadata)->decided_by == "ana");
    CHECK(!brain.decide(describe("No.", {"interjection"}).metadata, larry::Status::Validated, "pedro"));
    CHECK(brain.conception(1).has_value());
    CHECK(brain.conception(1)->status == larry::Status::Withdrawn);
    CHECK(!brain.conception(2).has_value());
    // The validators and the decision survive a restart of the cache.
    larry::Memory again{file};
    CHECK(again.validators() == (std::vector<std::string>{"pedro", "ana"}));
    CHECK(again.find(sky.metadata)->decided_by == "ana");
    CHECK(again.find(sky.metadata)->status == larry::Status::Withdrawn);
}

TEST(hear_with_the_dictionary_suggests_and_takes_categories) {
    const larry::Dictionary dictionary{larry::Dictionary::file_for(larry::Language::English)};
    const std::filesystem::path file =
        std::filesystem::temp_directory_path() / "larry_test_brain_dictionary.atoms";
    std::filesystem::remove(file);
    larry::Memory cache{file};
    larry::Brain brain{rules(), cache, nullptr, &dictionary};
    const larry::AtomOperations ops;
    const larry::Assimilation assimilation{rules(), &dictionary};
    const auto teach = [&](std::string_view text, std::vector<std::string_view> categories) {
        std::vector<Bytes> taught;
        for (const std::string_view c : categories) {
            taught.emplace_back(c.begin(), c.end());
        }
        const larry::Description d = assimilation.describe(ops.from_text(text), &cache, taught);
        cache.store(d.atom, d.metadata);
    };
    teach("The sky is blue.", {"determiner", "noun", "auxiliary verb", "adjective"});
    CHECK(brain.hear(ops.from_text("The skyy is blue.")).text == "Noted. What is \"skyy\"? Did you mean \"sky\"?");
    CHECK(brain.hear(ops.from_text("Oh, the sky is blue.")).text == "I know. The sky is blue.");
    CHECK(brain.hear(ops.from_text("The sky is azure.")).text.ends_with(" I take \"azure\" as adjective."));
    CHECK(brain.hear(ops.from_text("Zqxjkv.")).text == "Noted. What is \"Zqxjkv\"?");
}

namespace {

std::string_view trim_view(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) {
        s.remove_suffix(1);
    }
    return s;
}

}  // namespace

TEST(truth_suite_with_exclusive_attributes) {
    const std::filesystem::path file =
        std::filesystem::temp_directory_path() / "larry_test_brain_truth.atoms";
    std::filesystem::remove(file);
    larry::Memory cache{file};
    larry::Brain brain{rules(), cache};
    const larry::Assimilation assimilation{rules()};
    const larry::AtomOperations ops;
    const auto teach = [&](std::string_view text, std::vector<std::string_view> categories) {
        std::vector<Bytes> taught;
        for (const std::string_view c : categories) {
            taught.emplace_back(c.begin(), c.end());
        }
        const larry::Description d = assimilation.describe(ops.from_text(text), &cache, taught);
        cache.store(d.atom, d.metadata);
    };
    teach("The sky is blue.", {"determiner", "noun", "auxiliary verb", "adjective"});
    teach("The door is closed.", {"determiner", "noun", "auxiliary verb", "adjective"});
    teach("The water is hot.", {"determiner", "noun", "auxiliary verb", "adjective"});
    teach("Tom has three apples.", {"proper noun", "verb", "numeral", "noun"});
    teach("It is Monday.", {"pronoun", "auxiliary verb", "proper noun"});
    teach("The car is red.", {"determiner", "noun", "auxiliary verb", "adjective"});
    teach("Tom is tall.", {"proper noun", "auxiliary verb", "adjective"});
    teach("The cat is asleep.", {"determiner", "noun", "auxiliary verb", "adjective"});
    teach("The cup is full.", {"determiner", "noun", "auxiliary verb", "adjective"});
    teach("The sea is deep.", {"determiner", "noun", "auxiliary verb", "adjective"});
    teach("Penguins do not fly.", {"noun", "auxiliary verb", "adverb", "verb"});
    teach("Birds fly.", {"noun", "verb"});

    const std::filesystem::path suite = std::filesystem::path{LARRY_TEST_DATA_DIR} / "en" / "truth.txt";
    std::ifstream in{suite, std::ios::binary};
    CHECK(static_cast<bool>(in));
    std::size_t cases = 0;
    std::size_t failed = 0;
    std::size_t number = 0;
    for (std::string line; std::getline(in, line);) {
        ++number;
        const std::string_view text = trim_view(line);
        if (text.empty() || text.front() == '#') {
            continue;
        }
        const std::size_t separator = text.find(" | ");
        CHECK(separator != std::string_view::npos);
        if (separator == std::string_view::npos) {
            continue;
        }
        const std::string_view expected = trim_view(text.substr(0, separator));
        const std::string claim{trim_view(text.substr(separator + 3))};
        ++cases;
        const Verdict verdict = brain.truth(ops.from_text(claim));
        const std::string_view got = verdict.truth == Truth::True    ? "true"
                                     : verdict.truth == Truth::False ? "false"
                                                                     : "unknown";
        if (got != expected) {
            ++failed;
            std::println(stderr, "truth.txt line {}: \"{}\" expected {}, got {}", number, claim,
                         expected, got);
        }
        // A decided answer names a conception; a false by exclusivity names the rule.
        if (verdict.truth != Truth::Unknown) {
            CHECK(!verdict.because.empty());
        }
    }
    CHECK(cases >= 45);
    CHECK(failed == 0);

    const Verdict green = brain.truth(ops.from_text("the sky is green"));
    CHECK(green.truth == Truth::False);
    CHECK(green.rules.size() == 1);
    CHECK(green.rules.size() == 1 &&
          green.rules.front() == "green and blue are both of the kind colour, and a thing has one at a time");
    CHECK(!green.because.empty() && ops.text(green.because.front().description.atom) == "The sky is blue.");
    const Verdict five = brain.truth(ops.from_text("Tom has five apples"));
    CHECK(five.truth == Truth::False);
    CHECK(five.rules.size() == 1 && five.rules.front().starts_with("5 and 3 are both of the kind number"));
    CHECK(brain.truth(ops.from_text("the sky is blue")).rules.empty());
    // A withdrawn conception decides nothing.
    cache.add_validator("pedro");
    CHECK(brain.decide(brain.proposed().front().description.metadata, larry::Status::Withdrawn, "pedro"));
    CHECK(brain.truth(ops.from_text("the sky is green")).truth == Truth::Unknown);
    // A question answered by the rule names the rule; so does a false claim,
    // which hear stores as a proposal, so the question goes first.
    const larry::Reply question = brain.hear(ops.from_text("Is the door open?"), "user:pedro");
    CHECK(question.text == "No.");
    CHECK(std::ranges::any_of(question.because, [](const std::string& b) { return b.starts_with("rule: open and closed"); }));
    const larry::Reply conflict = brain.hear(ops.from_text("The door is open."), "user:pedro");
    CHECK(conflict.text.starts_with("That conflicts with what I know: The door is closed."));
    CHECK(std::ranges::any_of(conflict.because, [](const std::string& b) { return b.starts_with("rule: open and closed"); }));
    CHECK(brain.hear(ops.from_text("Is the door open?"), "user:pedro").text == "Yes.");  // now proposed
}

TEST(qualification_by_the_rules_and_by_example) {
    const std::filesystem::path file =
        std::filesystem::temp_directory_path() / "larry_test_brain_qualify.atoms";
    std::filesystem::remove(file);
    larry::Memory cache{file};
    larry::Grammar own{rules()};
    larry::Brain brain{rules(), cache, nullptr, nullptr, &own};
    const larry::Assimilation assimilation{rules(), nullptr, &own};
    const larry::AtomOperations ops;
    const auto teach = [&](std::string_view text, std::vector<std::string_view> categories) {
        std::vector<Bytes> taught;
        for (const std::string_view c : categories) {
            taught.emplace_back(c.begin(), c.end());
        }
        const larry::Description d = assimilation.describe(ops.from_text(text), &cache, taught);
        cache.store(d.atom, d.metadata);
    };
    teach("The sky is blue.", {"determiner", "noun", "auxiliary verb", "adjective"});
    teach("The sea is deep.", {"determiner", "noun", "auxiliary verb", "adjective"});
    teach("The grass is green?", {"determiner", "noun", "auxiliary verb", "adjective"});
    teach("The snow is white?", {"determiner", "noun", "auxiliary verb", "adjective"});
    teach("The door is open?", {"determiner", "noun", "auxiliary verb", "adjective"});
    teach("The moon is round?", {"determiner", "noun", "auxiliary verb", "adjective"});
    teach("Is the sky blue?", {"auxiliary verb", "determiner", "noun", "adjective"});
    teach("Can birds fly?", {"auxiliary verb", "noun", "verb"});
    teach("Close the door.", {"verb", "determiner", "noun"});
    teach("Shut the window.", {"verb", "determiner", "noun"});
    teach("Open the window.", {"verb", "determiner", "noun"});
    teach("Remember the sky is blue.", {"verb", "determiner", "noun", "auxiliary verb", "adjective"});
    teach("Suppose the sky is green.", {"verb", "determiner", "noun", "auxiliary verb", "adjective"});
    teach("Hello.", {"interjection"});
    teach("Thanks.", {"interjection"});
    teach("Birds fly.", {"noun", "verb"});
    teach("Dogs bark.", {"noun", "verb"});
    teach("What colour is the sky?", {"pronoun", "noun", "auxiliary verb", "determiner", "noun"});
    teach("Oh, the dogs bark.", {"interjection", "determiner", "noun", "verb"});
    teach("Imagine that.", {"verb", "pronoun"});  // "imagine" known as a verb

    // The suite: by the rules, by the examples, the sentence.
    const std::filesystem::path suite = std::filesystem::path{LARRY_TEST_DATA_DIR} / "en" / "qualify.txt";
    std::ifstream in{suite, std::ios::binary};
    CHECK(static_cast<bool>(in));
    std::size_t cases = 0;
    std::size_t failed = 0;
    std::size_t number = 0;
    for (std::string line; std::getline(in, line);) {
        ++number;
        const std::string_view text = trim_view(line);
        if (text.empty() || text.front() == '#') {
            continue;
        }
        const std::size_t first = text.find(" | ");
        const std::size_t second = first == std::string_view::npos ? first : text.find(" | ", first + 3);
        CHECK(second != std::string_view::npos);
        if (second == std::string_view::npos) {
            continue;
        }
        const std::string by_rules{trim_view(text.substr(0, first))};
        const std::string by_examples{trim_view(text.substr(first + 3, second - first - 3))};
        const std::string sentence{trim_view(text.substr(second + 3))};
        ++cases;
        const larry::Qualifying q = brain.qualify(assimilation.describe(ops.from_text(sentence), &cache));
        const std::string got_rules{larry::name(q.by_rules)};
        const std::string got_examples = q.examples.empty() ? "none"
                                         : !q.by_examples   ? "tie"
                                                            : std::string{larry::name(*q.by_examples)};
        if (got_rules != by_rules || got_examples != by_examples) {
            ++failed;
            std::println(stderr, "qualify.txt line {}: \"{}\" expected {} | {}, got {} | {}: {}", number, sentence,
                         by_rules, by_examples, got_rules, got_examples, q.text());
        }
        CHECK(!q.rule.empty());
        CHECK(!q.text().empty());
    }
    CHECK(cases >= 10);
    CHECK(failed == 0);

    // The reasons, as text; both answers when they disagree.
    const larry::Qualifying moon = brain.qualify(assimilation.describe(ops.from_text("The moon is white."), &cache));
    CHECK(!moon.agree());
    CHECK(moon.examples.size() == 6);
    CHECK(!moon.validated);
    CHECK(moon.text() == "affirmation (declaration), by the rule 7: anything else is an affirmation; but question (question) by the 6 proposed conceptions of the same structure: The sky is blue., The sea is deep., The grass is green? and 3 more");
    const larry::Qualifying sea = brain.qualify(assimilation.describe(ops.from_text("Is the sea deep?"), &cache));
    CHECK(sea.agree());
    CHECK(sea.text() == "question (question), by the rule 1: a sentence that ends with a question mark is a question, and by the 1 proposed conception of the same structure: Is the sky blue?");
    const larry::Qualifying what = brain.qualify(assimilation.describe(ops.from_text("What is the sky?"), &cache));
    CHECK(what.agree());
    CHECK(what.text().ends_with("; no conception has the same structure"));
    const larry::Qualifying imagine = brain.qualify(assimilation.describe(ops.from_text("Imagine the sea is deep."), &cache));
    CHECK(imagine.agree());  // a tie decides nothing
    CHECK(imagine.text().ends_with("disagree among themselves"));
    // Validated conceptions are the standard: once one example is validated, it alone counts.
    cache.add_validator("pedro");
    CHECK(brain.decide(assimilation.describe(ops.from_text("The sky is blue."), &cache).metadata, larry::Status::Validated, "pedro"));
    const larry::Qualifying again = brain.qualify(assimilation.describe(ops.from_text("The moon is white."), &cache));
    CHECK(again.agree());
    CHECK(again.validated);
    CHECK(again.examples.size() == 1);
    CHECK(again.by_examples == larry::Qualification::Affirmation);
    // A withdrawn conception is no example.
    CHECK(brain.decide(assimilation.describe(ops.from_text("The sky is blue."), &cache).metadata, larry::Status::Withdrawn, "pedro"));
    CHECK(brain.qualify(assimilation.describe(ops.from_text("The moon is white."), &cache)).examples.size() == 5);
}

TEST(hear_handles_orders_assumptions_and_expressions) {
    const larry::Memory& memory = hearing_brain().memory();
    const larry::Reply order = say("Close the window.");
    CHECK(order.text == "I cannot do that yet.");
    CHECK(!order.stored);
    const std::int64_t before = memory.count();
    const larry::Reply assumption = say("Suppose the sky is green.");
    CHECK(assumption.text == "Noted as an assumption, not as a truth.");
    CHECK(assumption.stored);
    CHECK(memory.count() == before + 1);
    CHECK(say("Is the sky green?").text == "No.");  // the assumption is no evidence; the sky is blue
    CHECK(say("Hello!").text == "Hello!");
    CHECK(say("Thank you.").text == "Thank you.");
    CHECK(!say("Hello!").because.empty());
}

namespace {

// The cloud: a scratch schema of the local server, when one answers.
std::unique_ptr<larry::Database> cloud_database() {
    std::string connection;
    for (const char* variable : {"LARRY_TEST_DB", "LARRY_DB"}) {
        const char* value = std::getenv(variable);
        if (value != nullptr && *value != '\0') {
            connection = value;
            break;
        }
    }
    if (connection.empty()) {
        connection = "dbname=larry";
    }
    connection += " options='-c search_path=larry_test_brain'";
    try {
        auto db = std::make_unique<larry::Database>(connection);
        db->run("drop schema if exists larry_test_brain cascade; create schema larry_test_brain");
        db->apply_schema();
        return db;
    } catch (const std::exception& e) {
        std::println("cloud checks skipped: {}", e.what());
        return nullptr;
    }
}

}  // namespace

TEST(the_cache_answers_first_and_the_cloud_second) {
    std::unique_ptr<larry::Database> cloud = cloud_database();
    if (!cloud) {
        return;
    }
    const std::filesystem::path file =
        std::filesystem::temp_directory_path() / "larry_test_brain_cloud.atoms";
    std::filesystem::remove(file);
    larry::Memory cache{file};
    larry::Brain brain{rules(), cache, cloud.get()};
    const larry::Assimilation assimilation{rules()};
    const larry::AtomOperations ops;
    const auto describe = [&](std::string_view text, std::vector<std::string_view> categories) {
        std::vector<Bytes> taught;
        for (const std::string_view c : categories) {
            taught.emplace_back(c.begin(), c.end());
        }
        return assimilation.describe(ops.from_text(text), &cache, taught);
    };
    // Taught through the brain: in the cache and in the cloud.
    const larry::Description sky = describe("The sky is blue.", {"determiner", "noun", "auxiliary verb", "adjective"});
    CHECK(brain.remember(sky, larry::Status::Proposed, "lesson:test") == larry::Stored::New);
    CHECK(cache.count() == 1);
    CHECK(cloud->count() == 1);
    CHECK(cloud->find(sky.metadata)->sources == std::vector<std::string>{"lesson:test"});
    // Only in the cloud: the cache does not know it, the cloud does, and then the cache does.
    const larry::Description sea = describe("The sea is wide.", {"determiner", "noun", "auxiliary verb", "adjective"});
    cloud->store(sea.atom, sea.metadata, larry::Status::Validated, "pi");
    CHECK(cache.find(sea.metadata) == std::nullopt);
    const larry::Verdict first = brain.truth(ops.from_text("the sea is wide"));
    CHECK(first.truth == Truth::True);
    CHECK(first.from_cloud);
    CHECK(cache.find(sea.metadata).has_value());
    CHECK(cache.find(sea.metadata)->status == larry::Status::Validated);
    CHECK(cache.find(sea.metadata)->sources == std::vector<std::string>{"pi"});
    const larry::Verdict second = brain.truth(ops.from_text("the sea is wide"));
    CHECK(second.truth == Truth::True);
    CHECK(!second.from_cloud);
    // The same for an open question, and for hearing.
    const larry::Description tom = describe("Tom is a teacher.", {"proper noun", "auxiliary verb", "determiner", "noun"});
    cloud->store(tom.atom, tom.metadata, larry::Status::Proposed, "pi");
    larry::Reply who = brain.hear(ops.from_text("Who is Tom?"));
    CHECK(who.text == "Tom is a teacher.");
    CHECK(cache.find(tom.metadata).has_value());
    const larry::Reply heard = brain.hear(ops.from_text("The sky is wide."));
    CHECK(heard.text == "Noted.");
    CHECK(cloud->count() == 4);
    CHECK(cloud->all().back().sources == std::vector<std::string>{"user"});
    // A second source does not validate: only a validator, in both stores.
    CHECK(cloud->find(tom.metadata)->status == larry::Status::Proposed);
    (void)brain.hear(ops.from_text("Tom is a teacher."), "user:pedro");
    CHECK(cloud->find(tom.metadata)->status == larry::Status::Proposed);
    CHECK(cloud->find(tom.metadata)->sources == (std::vector<std::string>{"pi", "user:pedro"}));
    CHECK(brain.validators().empty());
    CHECK(brain.add_validator("pedro", "pedro"));
    CHECK(cloud->validators() == std::vector<std::string>{"pedro"});
    CHECK(cache.validators() == std::vector<std::string>{"pedro"});
    CHECK_THROWS(brain.decide(tom.metadata, larry::Status::Validated, "claude"), std::runtime_error);
    CHECK(brain.decide(tom.metadata, larry::Status::Validated, "pedro"));
    CHECK(cloud->find(tom.metadata)->status == larry::Status::Validated);
    CHECK(cloud->find(tom.metadata)->decided_by == "pedro");
    CHECK(cache.find(tom.metadata)->status == larry::Status::Validated);
    CHECK(cache.find(tom.metadata)->decided_by == "pedro");
    CHECK(brain.proposed().size() == 2);  // the sky, and the sky is wide
    CHECK(brain.conception(cloud->find(sky.metadata)->id)->sources == std::vector<std::string>{"lesson:test"});
    // A withdrawn conception in the cloud is no evidence.
    const larry::Description moon = describe("The moon is blue.", {"determiner", "noun", "auxiliary verb", "adjective"});
    cloud->store(moon.atom, moon.metadata, larry::Status::Withdrawn, "pi");
    CHECK(brain.truth(ops.from_text("the moon is blue")).truth == Truth::Unknown);
    // Sync: what the cache has and the cloud not, and the other way round.
    const larry::Description grass = describe("The grass is tall.", {"determiner", "noun", "auxiliary verb", "adjective"});
    cache.store(grass.atom, grass.metadata, larry::Status::Proposed, "lesson:2");
    const larry::Description cat = describe("The cat is small.", {"determiner", "noun", "auxiliary verb", "adjective"});
    cloud->store(cat.atom, cat.metadata, larry::Status::Validated, "pi");
    const auto [pushed, pulled, redescribed] = brain.sync(100);
    CHECK(pushed == 1);
    CHECK(pulled == 2);  // the cat and the withdrawn moon
    CHECK(cloud->find(grass.metadata)->sources == std::vector<std::string>{"lesson:2"});
    CHECK(cache.find(cat.metadata)->status == larry::Status::Validated);
    CHECK(brain.sync(100) == larry::Brain::Synced{});
    // Q28: the cloud holds an older description of a conception: when the
    // machine describes it anew, the cloud follows, at remember and at sync.
    const larry::Description sun = describe("The sun is hot.", {"determiner", "noun", "auxiliary verb", "adjective"});
    larry::Description older = sun;
    older.entities.entities[3].types = {b("positive"), b("object")};
    older.type.bytes = b("subject subject predicate object / neutral");
    older.metadata = ops.metadata(older.category, older.type, older.entities);
    CHECK(older.metadata.bytes != sun.metadata.bytes);
    CHECK(cloud->store(older.atom, older.metadata, larry::Status::Proposed, "pi") == larry::Stored::New);
    const std::int64_t in_cloud = cloud->count();
    CHECK(brain.remember(sun, larry::Status::Proposed, "lesson:3") == larry::Stored::New);
    CHECK(cloud->count() == in_cloud);
    CHECK(cloud->find(sun.metadata)->description.metadata.bytes == sun.metadata.bytes);
    CHECK(cloud->find(sun.metadata)->sources == (std::vector<std::string>{"pi", "lesson:3"}));
    CHECK(cloud->redescribe(cloud->find(sun.metadata)->id, older.metadata));
    const larry::Brain::Synced synced = brain.sync(100);
    CHECK(synced.redescribed == 1);
    CHECK(synced.pushed == 0);
    CHECK(cloud->find(sun.metadata)->description.metadata.bytes == sun.metadata.bytes);
    CHECK(brain.sync(100) == larry::Brain::Synced{});
    // Q29: a reading goes with the conception to the cloud, at remember and at sync.
    const larry::Description plain = describe("Sky is grey.", {"noun", "auxiliary verb", "adjective"});
    CHECK(brain.remember(plain, larry::Status::Proposed, "user:pedro", "The sky is grey.") == larry::Stored::New);
    CHECK(cache.find(plain.metadata)->reading == "The sky is grey.");
    CHECK(cloud->find(plain.metadata)->reading == "The sky is grey.");
    const larry::Description other = describe("Sea is wet.", {"noun", "auxiliary verb", "adjective"});
    cache.store(other.atom, other.metadata, larry::Status::Proposed, "user:pedro");
    cache.set_reading(other.metadata, "The sea is wet.");
    CHECK(brain.sync(100).pushed == 1);
    CHECK(cloud->find(other.metadata)->reading == "The sea is wet.");
    // And back: a reading in the cloud comes into a cache that lacks the conception.
    const larry::Description third = describe("Moon is white.", {"noun", "auxiliary verb", "adjective"});
    cloud->store(third.atom, third.metadata, larry::Status::Proposed, "pi");
    cloud->set_reading(cloud->find(third.metadata)->id, "The moon is white.");
    CHECK(brain.sync(100).pulled == 1);
    CHECK(cache.find(third.metadata)->reading == "The moon is white.");
    // A description that is not complete never replaces one that is.
    larry::Description guessed = sun;
    guessed.entities.entities[3].types.push_back(b("guessed"));
    guessed.metadata = ops.metadata(guessed.category, guessed.type, guessed.entities);
    CHECK(brain.remember(guessed, larry::Status::Proposed, "user:ana") == larry::Stored::Same);
    CHECK(cache.find(sun.metadata)->description.metadata.bytes == sun.metadata.bytes);
    CHECK(cloud->find(sun.metadata)->description.metadata.bytes == sun.metadata.bytes);
    cloud->run("drop schema if exists larry_test_brain cascade");
}

int main() {
    return larry::test::run();
}
