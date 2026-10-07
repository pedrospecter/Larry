// Tests for Brain: is a concept true, false or unknown?

#include "larry/brain.hpp"

#include "larry/assimilation.hpp"
#include "larry/atom_operations.hpp"
#include "larry/base_rules.hpp"
#include "larry/database.hpp"
#include "larry/memory.hpp"

#include "check.hpp"

#include <cstdlib>
#include <exception>
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
    const Verdict green = ask("the sky is green");
    CHECK(green.truth == Truth::Unknown);
    CHECK(green.because.empty());
    CHECK(!green.nearest.empty());
    CHECK(!green.nearest.empty() && first_because(Verdict{.truth = Truth::True, .because = {green.nearest.front()}, .nearest = {}}) == "The sky is blue.");
    CHECK(ask("Tom has five apples").truth == Truth::Unknown);
    CHECK(ask("penguins can fly").truth == Truth::Unknown);
    CHECK(ask("Mary went to the garden").truth == Truth::Unknown);
    CHECK(ask("the moon is made of cheese").truth == Truth::Unknown);
    CHECK(ask("the moon is made of cheese").nearest.empty());
    CHECK(ask("").truth == Truth::Unknown);
    // Questions and assumptions in memory are no evidence: only affirmations.
    CHECK(ask("the sky is green").truth == Truth::Unknown);
    CHECK(ask("suppose the sky is green").truth == Truth::Unknown);
}

TEST(yes_no_questions_are_answered_from_their_statements) {
    CHECK(ask("Is the sky blue?").truth == Truth::True);
    CHECK(first_because(ask("Is the sky blue?")) == "The sky is blue.");
    CHECK(ask("Is the sky green?").truth == Truth::Unknown);
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
    CHECK(say("Is the sky green?").text.starts_with("I don't know."));
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
    CHECK(unknown.text == "Noted. I take \"azure\" as adjective.");
    CHECK(unknown.stored);
}

TEST(two_sources_validate_a_conception) {
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
    CHECK(cache.find(sky.metadata)->status == larry::Status::Proposed);
    // The same source again changes nothing.
    CHECK(brain.remember(sky, larry::Status::Proposed, "lesson:1") == larry::Stored::Same);
    CHECK(cache.find(sky.metadata)->status == larry::Status::Proposed);
    CHECK(brain.proposed().size() == 1);
    // A second source validates.
    const larry::Reply heard = brain.hear(ops.from_text("The sky is blue."), "user");
    CHECK(heard.text == "I already know that. Now validated: a second source says so.");
    CHECK(cache.find(sky.metadata)->status == larry::Status::Validated);
    CHECK(cache.find(sky.metadata)->sources == (std::vector<std::string>{"lesson:1", "user"}));
    CHECK(brain.proposed().empty());
    // Validated evidence stays evidence; withdrawn evidence does not.
    CHECK(brain.truth(ops.from_text("the sky is blue")).truth == Truth::True);
    CHECK(brain.set_status(sky.metadata, larry::Status::Withdrawn));
    CHECK(brain.truth(ops.from_text("the sky is blue")).truth == Truth::Unknown);
    CHECK(!brain.set_status(describe("No.", {"interjection"}).metadata, larry::Status::Validated));
    CHECK(brain.conception(1).has_value());
    CHECK(brain.conception(1)->status == larry::Status::Withdrawn);
    CHECK(!brain.conception(2).has_value());
    // By hand: accept a proposed conception.
    const larry::Description sea = describe("The sea is wide.", {"determiner", "noun", "auxiliary verb", "adjective"});
    brain.remember(sea, larry::Status::Proposed, "user");
    CHECK(brain.proposed().size() == 1);
    CHECK(brain.set_status(brain.proposed().front().description.metadata, larry::Status::Validated));
    CHECK(brain.proposed().empty());
    CHECK(cache.find(sea.metadata)->status == larry::Status::Validated);
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
    CHECK(say("Is the sky green?").text.starts_with("I don't know."));
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
    // A second source validates in both stores: Tom came from the Pi, now from the user.
    CHECK(cloud->find(tom.metadata)->status == larry::Status::Proposed);
    (void)brain.hear(ops.from_text("Tom is a teacher."), "user");
    CHECK(cloud->find(tom.metadata)->status == larry::Status::Validated);
    CHECK(cache.find(tom.metadata)->status == larry::Status::Validated);
    CHECK(brain.proposed().size() == 2);  // the sky and the sky is wide
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
    const auto [pushed, pulled] = brain.sync(100);
    CHECK(pushed == 1);
    CHECK(pulled == 2);  // the cat and the withdrawn moon
    CHECK(cloud->find(grass.metadata)->sources == std::vector<std::string>{"lesson:2"});
    CHECK(cache.find(cat.metadata)->status == larry::Status::Validated);
    CHECK(brain.sync(100) == std::make_pair(std::int64_t{0}, std::int64_t{0}));
    cloud->run("drop schema if exists larry_test_brain cascade");
}

int main() {
    return larry::test::run();
}
