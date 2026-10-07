// Tests for Brain: is a concept true, false or unknown?

#include "larry/brain.hpp"

#include "larry/assimilation.hpp"
#include "larry/atom_operations.hpp"
#include "larry/base_rules.hpp"
#include "larry/memory.hpp"

#include "check.hpp"

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

int main() {
    return larry::test::run();
}
