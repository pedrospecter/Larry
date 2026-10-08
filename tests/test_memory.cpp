// Tests for Memory: storing conceptions, finding them back, the word index.

#include "larry/memory.hpp"

#include "larry/atom_operations.hpp"
#include "larry/hex.hpp"

#include "check.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using larry::AtomOperations;
using larry::Bytes;
using larry::Description;
using larry::Memory;
using larry::Stored;

namespace {

Bytes b(std::string_view text) {
    return Bytes(text.begin(), text.end());
}

std::filesystem::path fresh(std::string_view name) {
    const std::filesystem::path file = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove(file);
    return file;
}

std::string contents(const std::filesystem::path& file) {
    std::ifstream in{file, std::ios::binary};
    return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

// A described atom: the words with their categories, qualified as an affirmation.
Description describe(std::string_view text, std::vector<std::string_view> words,
                     std::vector<std::string_view> categories) {
    const AtomOperations ops;
    Description d;
    d.atom = ops.from_text(text);
    d.category.bytes = b("affirmation");
    for (std::size_t i = 0; i < words.size(); ++i) {
        d.entities.entities.push_back(
            {.word = b(words[i]), .category = b(categories[i]), .types = {}});
    }
    d.image.bytes = b(text);
    d.metadata = ops.metadata(d.category, d.type, d.entities);
    return d;
}

Description sky_blue() {
    return describe("The sky is blue.", {"The", "sky", "is", "blue"},
                    {"determiner", "noun", "auxiliary verb", "adjective"});
}

Description sea_blue() {
    return describe("The sea is blue.", {"The", "sea", "is", "blue"},
                    {"determiner", "noun", "auxiliary verb", "adjective"});
}

Description sky_clouds() {
    return describe("Clouds cross the sky.", {"Clouds", "cross", "the", "sky"},
                    {"noun", "verb", "determiner", "noun"});
}

}  // namespace

TEST(store_and_find_round_trip) {
    Memory memory{fresh("larry_test_round_trip.atoms")};
    CHECK(memory.count() == 0);
    const Description d = sky_blue();
    CHECK(memory.store(d.atom, d.metadata) == Stored::New);
    CHECK(memory.count() == 1);
    CHECK(memory.count_words() == 4);
    const auto found = memory.find(d.metadata);
    CHECK(found.has_value());
    const AtomOperations ops;
    CHECK(found->id == 1);
    CHECK(ops.text(found->description.atom) == "The sky is blue.");
    CHECK(found->description.metadata.bytes == d.metadata.bytes);
    CHECK(found->description.category.bytes == b("affirmation"));
    CHECK(found->description.entities.entities.size() == 4);
    CHECK(found->description.entities.entities[1].word == b("sky"));
    CHECK(found->description.entities.entities[1].category == b("noun"));
    CHECK(found->description.image.bytes == b("The sky is blue."));
    CHECK(!memory.find(sea_blue().metadata).has_value());
}

TEST(the_same_atom_twice_changes_nothing) {
    Memory memory{fresh("larry_test_same.atoms")};
    const Description d = sky_blue();
    CHECK(memory.store(d.atom, d.metadata) == Stored::New);
    CHECK(memory.store(d.atom, d.metadata) == Stored::Same);
    CHECK(memory.count() == 1);
    CHECK(memory.count_words() == 4);
    CHECK(memory.all().size() == 1);
}

TEST(the_same_form_keeps_the_first_atom) {
    // Punctuation is not an entity (Q6), so these two share their metadata.
    Memory memory{fresh("larry_test_same_form.atoms")};
    const Description first = sky_blue();
    const Description second = describe("The sky is blue", {"The", "sky", "is", "blue"},
                                        {"determiner", "noun", "auxiliary verb", "adjective"});
    CHECK(first.metadata.bytes == second.metadata.bytes);
    CHECK(memory.store(first.atom, first.metadata) == Stored::New);
    CHECK(memory.store(second.atom, second.metadata) == Stored::SameForm);
    CHECK(memory.count() == 1);
    const AtomOperations ops;
    CHECK(ops.text(memory.find(second.metadata)->description.atom) == "The sky is blue.");
}

namespace {

// The same sentence described with types and roles: another metadata, the
// same identity (Q28).
Description sky_blue_with_types() {
    const AtomOperations ops;
    Description d = sky_blue();
    d.entities.entities[0].types = {b("subject")};
    d.entities.entities[1].types = {b("singular"), b("subject")};
    d.entities.entities[2].types = {b("third person"), b("singular"), b("present"), b("predicate")};
    d.entities.entities[3].types = {b("positive"), b("attribute")};
    d.type.bytes = b("subject subject predicate attribute / neutral");
    d.metadata = ops.metadata(d.category, d.type, d.entities);
    return d;
}

}  // namespace

TEST(the_same_words_with_other_types_are_one_conception) {
    const std::filesystem::path file = fresh("larry_test_identity.atoms");
    Memory memory{file};
    const AtomOperations ops;
    const Description first = sky_blue();
    const Description second = sky_blue_with_types();
    CHECK(first.metadata.bytes != second.metadata.bytes);
    CHECK(ops.identity(first.metadata) == ops.identity(second.metadata));
    CHECK(ops.identity(first.metadata) != ops.identity(sea_blue().metadata));
    CHECK(ops.complete(first.metadata));
    CHECK(ops.complete(second.metadata));
    {
        Description guessed = sky_blue();
        guessed.entities.entities[3].types = {b("guessed")};
        guessed.metadata = ops.metadata(guessed.category, guessed.type, guessed.entities);
        CHECK(!ops.complete(guessed.metadata));
        Description unknown = sky_blue();
        unknown.entities.entities[3].category.clear();
        unknown.metadata = ops.metadata(unknown.category, unknown.type, unknown.entities);
        CHECK(!ops.complete(unknown.metadata));
    }
    CHECK(memory.store(first.atom, first.metadata, larry::Status::Proposed, "lesson:1") == Stored::New);
    CHECK(memory.store(second.atom, second.metadata, larry::Status::Proposed, "user:pedro") == Stored::Same);
    CHECK(memory.count() == 1);
    // Found by either description; the stored one is the first, until it is redescribed.
    CHECK(memory.find(second.metadata).has_value());
    CHECK(memory.find(second.metadata)->description.metadata.bytes == first.metadata.bytes);
    CHECK(memory.find(second.metadata)->sources == (std::vector<std::string>{"lesson:1", "user:pedro"}));
    CHECK(memory.uses(b("sky")).size() == 1);
    CHECK(memory.redescribe(second.metadata));
    CHECK(!memory.redescribe(second.metadata));
    CHECK(!memory.redescribe(sea_blue().metadata));
    CHECK(memory.count() == 1);
    CHECK(memory.find(first.metadata)->description.metadata.bytes == second.metadata.bytes);
    CHECK(memory.find(first.metadata)->description.entities.entities[1].types ==
          (std::vector<Bytes>{b("singular"), b("subject")}));
    CHECK(memory.find_prefix(second.metadata.bytes).size() == 1);
    CHECK(memory.find_prefix(first.metadata.bytes).empty());
    CHECK(memory.count_words() == 4);
    CHECK(memory.uses(b("sky")).size() == 1);
    // The status and a decision follow the conception, not the description.
    CHECK(memory.set_status(first.metadata, larry::Status::Validated, "pedro"));
    CHECK(memory.find(second.metadata)->status == larry::Status::Validated);
    // A restart reads the describe line back.
    Memory again{file};
    CHECK(again.count() == 1);
    CHECK(again.find(first.metadata)->description.metadata.bytes == second.metadata.bytes);
    CHECK(again.find(first.metadata)->status == larry::Status::Validated);
    CHECK(again.find(first.metadata)->decided_by == "pedro");
    CHECK(again.count_words() == 4);
    CHECK(!again.redescribe(second.metadata));
}

TEST(the_reading_lives_in_the_log) {
    const std::filesystem::path file = fresh("larry_test_reading.atoms");
    Memory memory{file};
    const Description d = describe("Sky is blue.", {"Sky", "is", "blue"}, {"noun", "auxiliary verb", "adjective"});
    CHECK(!memory.set_reading(d.metadata, "The sky is blue."));
    CHECK(memory.store(d.atom, d.metadata) == Stored::New);
    CHECK(memory.find(d.metadata)->reading.empty());
    CHECK(memory.set_reading(d.metadata, "The sky is blue."));
    CHECK(memory.find(d.metadata)->reading == "The sky is blue.");
    CHECK(memory.find_id(1)->reading == "The sky is blue.");
    CHECK(memory.store(d.atom, d.metadata) == Stored::Same);
    CHECK(memory.find(d.metadata)->reading == "The sky is blue.");
    Memory again{file};
    CHECK(again.find(d.metadata)->reading == "The sky is blue.");
    CHECK(again.all().front().reading == "The sky is blue.");
    // A reading that is not hex is a bad file.
    {
        std::ofstream out{file, std::ios::binary | std::ios::app};
        out << "reading\t" << larry::hex::encode(d.metadata.bytes) << "\tzz\n";
    }
    CHECK_THROWS(Memory{file}, std::runtime_error);
}

TEST(two_records_of_one_conception_from_an_old_file_merge) {
    // Before Q28 the same words with other types were two atoms. They read
    // as one: the later complete description, every source, the status from
    // the lines that follow.
    const std::filesystem::path file = fresh("larry_test_identity_old.atoms");
    const Description first = sky_blue();
    const Description second = sky_blue_with_types();
    {
        std::ofstream out{file, std::ios::binary};
        out << "atom\t" << larry::hex::encode(first.metadata.bytes) << "\t"
            << larry::hex::encode(b("The sky is blue.")) << "\tproposed\t" << larry::hex::encode(b("lesson:1")) << "\n";
        out << "atom\t" << larry::hex::encode(second.metadata.bytes) << "\t"
            << larry::hex::encode(b("The sky is blue.")) << "\tproposed\t" << larry::hex::encode(b("user:pedro")) << "\n";
        out << "status\t" << larry::hex::encode(second.metadata.bytes) << "\tvalidated\t"
            << larry::hex::encode(b("pedro")) << "\n";
    }
    Memory memory{file};
    CHECK(memory.count() == 1);
    CHECK(memory.find(first.metadata)->description.metadata.bytes == second.metadata.bytes);
    CHECK(memory.find(first.metadata)->sources == (std::vector<std::string>{"lesson:1", "user:pedro"}));
    CHECK(memory.find(first.metadata)->status == larry::Status::Validated);
    CHECK(memory.find(first.metadata)->decided_by == "pedro");
    CHECK(memory.count_words() == 4);
    CHECK(memory.uses(b("sky")).size() == 1);
}

TEST(find_prefix_gives_the_atoms_that_share_leading_parts) {
    Memory memory{fresh("larry_test_prefix.atoms")};
    const AtomOperations ops;
    const Description a = sky_clouds();
    const Description c = sky_blue();
    const Description d = sea_blue();
    memory.store(a.atom, a.metadata);
    memory.store(c.atom, c.metadata);
    memory.store(d.atom, d.metadata);
    // Atoms that open with "The sky", in metadata order.
    const Bytes the_sky = ops.metadata_prefix(c.category, c.type,
                                              std::span{c.entities.entities}.first(2));
    const auto found = memory.find_prefix(the_sky);
    CHECK(found.size() == 1);
    CHECK(found.size() == 1 && ops.text(found[0].description.atom) == "The sky is blue.");
    // Atoms that open with "The".
    const Bytes the = ops.metadata_prefix(c.category, c.type, std::span{c.entities.entities}.first(1));
    const auto with_the = memory.find_prefix(the);
    CHECK(with_the.size() == 2);
    CHECK(with_the.size() == 2 && with_the[0].description.metadata.bytes < with_the[1].description.metadata.bytes);
    // Every affirmation.
    const Bytes affirmations = ops.metadata_prefix(c.category, c.type, {});
    CHECK(memory.find_prefix(affirmations).size() == 3);
    CHECK(memory.find_prefix(b("no such prefix")).empty());
    CHECK(memory.find_prefix({}).size() == 3);
}

TEST(word_index_answers_both_questions) {
    Memory memory{fresh("larry_test_words.atoms")};
    const Description a = sky_clouds();
    const Description c = sky_blue();
    const Description d = sea_blue();
    memory.store(c.atom, c.metadata);
    memory.store(d.atom, d.metadata);
    memory.store(a.atom, a.metadata);
    // Which atoms mention sky? Capitals do not matter.
    const auto sky = memory.uses(b("sky"));
    CHECK(sky.size() == 2);
    CHECK(sky.size() == 2 && sky[0].atom == 1 && sky[0].position == 1 && sky[0].category == b("noun"));
    CHECK(sky.size() == 2 && sky[0].before == b("the") && sky[0].after == b("is"));
    CHECK(sky.size() == 2 && sky[0].before_category == b("determiner") &&
          sky[0].after_category == b("auxiliary verb"));
    CHECK(sky.size() == 2 && sky[1].after_category.empty());
    CHECK(sky.size() == 2 && sky[1].atom == 3 && sky[1].position == 3 && sky[1].after.empty());
    CHECK(sky.size() == 2 && sky[1].before == b("the"));
    const auto clouds = memory.uses(b("clouds"));
    CHECK(clouds.size() == 1 && clouds[0].before.empty() && clouds[0].after == b("cross"));
    CHECK(memory.containing(b("sky")).size() == 2);
    CHECK(memory.containing(b("the")).size() == 3);
    CHECK(memory.containing(b("moon")).empty());
    // Which categories has "the" been seen with?
    const auto the = memory.categories_of(b("the"));
    CHECK(the.size() == 1);
    CHECK(the.size() == 1 && the[0].category == b("determiner") && the[0].count == 3);
    CHECK(memory.categories_of(b("The")).empty());
    CHECK(memory.categories_of(b("moon")).empty());
    // A word with two categories.
    const Description run_noun = describe("A run.", {"A", "run"}, {"determiner", "noun"});
    const Description run_verb = describe("Birds run.", {"Birds", "run"}, {"noun", "verb"});
    memory.store(run_noun.atom, run_noun.metadata);
    memory.store(run_verb.atom, run_verb.metadata);
    const auto run = memory.categories_of(b("run"));
    CHECK(run.size() == 2);
    CHECK(run.size() == 2 && run[0].category == b("noun") && run[1].category == b("verb"));
    // An entity with no category is no evidence of a category.
    const Description unknown = describe("Azure sky.", {"Azure", "sky"}, {"", "noun"});
    memory.store(unknown.atom, unknown.metadata);
    CHECK(memory.categories_of(b("azure")).empty());
    CHECK(memory.uses(b("azure")).size() == 1);
}

TEST(memory_survives_a_restart) {
    const std::filesystem::path file = fresh("larry_test_restart.atoms");
    const Description c = sky_blue();
    const Description d = sea_blue();
    {
        Memory memory{file};
        memory.store(c.atom, c.metadata);
        memory.store(d.atom, d.metadata);
    }
    Memory again{file};
    CHECK(again.count() == 2);
    CHECK(again.count_words() == 8);
    CHECK(again.find(c.metadata).has_value());
    CHECK(again.find(d.metadata)->id == 2);
    CHECK(again.uses(b("sea")).size() == 1);
    CHECK(again.store(c.atom, c.metadata) == Stored::Same);
    const AtomOperations ops;
    CHECK(ops.text(again.all()[1].description.atom) == "The sea is blue.");
}

TEST(two_builds_from_the_same_atoms_give_the_same_file) {
    const std::filesystem::path one = fresh("larry_test_build_one.atoms");
    const std::filesystem::path two = fresh("larry_test_build_two.atoms");
    for (const std::filesystem::path& file : {one, two}) {
        Memory memory{file};
        memory.store(sky_blue().atom, sky_blue().metadata);
        memory.store(sea_blue().atom, sea_blue().metadata);
        memory.store(sky_clouds().atom, sky_clouds().metadata);
    }
    CHECK(!contents(one).empty());
    CHECK(contents(one) == contents(two));
}

TEST(clear_forgets_everything) {
    const std::filesystem::path file = fresh("larry_test_clear.atoms");
    Memory memory{file};
    memory.store(sky_blue().atom, sky_blue().metadata);
    memory.clear();
    CHECK(memory.count() == 0);
    CHECK(memory.count_words() == 0);
    CHECK(!memory.find(sky_blue().metadata).has_value());
    CHECK(memory.uses(b("sky")).empty());
    CHECK(contents(file).empty());
    CHECK(memory.store(sky_blue().atom, sky_blue().metadata) == Stored::New);
    CHECK(memory.find(sky_blue().metadata)->id == 1);
}

TEST(a_file_that_is_not_memory_is_rejected) {
    const std::filesystem::path file = fresh("larry_test_bad.atoms");
    {
        std::ofstream out{file, std::ios::binary};
        out << "this is not hex\n";
    }
    CHECK_THROWS(Memory{file}, std::runtime_error);
    {
        std::ofstream out{file, std::ios::binary | std::ios::trunc};
        out << "00010001\n";  // no tab, no bytes
    }
    CHECK_THROWS(Memory{file}, std::runtime_error);
    {
        std::ofstream out{file, std::ios::binary | std::ios::trunc};
        out << "zz\t00\n";
    }
    CHECK_THROWS(Memory{file}, std::runtime_error);
    {
        // Valid hex that is not metadata.
        std::ofstream out{file, std::ios::binary | std::ios::trunc};
        out << "ff\t41\n";
    }
    CHECK_THROWS(Memory{file}, std::invalid_argument);
}

TEST(sources_and_status_live_in_the_log) {
    const std::filesystem::path file = fresh("larry_test_log.atoms");
    const Description c = sky_blue();
    const Description d = sea_blue();
    {
        Memory memory{file};
        CHECK(memory.store(c.atom, c.metadata, larry::Status::Proposed, "lesson:1") == Stored::New);
        CHECK(memory.store(c.atom, c.metadata, larry::Status::Proposed, "lesson:1") == Stored::Same);
        CHECK(memory.store(c.atom, c.metadata, larry::Status::Proposed, "user") == Stored::Same);
        CHECK(memory.store(d.atom, d.metadata, larry::Status::Validated, "") == Stored::New);
        CHECK(memory.find(c.metadata)->sources == (std::vector<std::string>{"lesson:1", "user"}));
        CHECK(memory.find(c.metadata)->status == larry::Status::Proposed);
        CHECK(memory.find(d.metadata)->sources.empty());
        CHECK(memory.find(d.metadata)->status == larry::Status::Validated);
        CHECK(memory.set_status(c.metadata, larry::Status::Validated, "pedro"));
        CHECK(memory.find(c.metadata)->decided_by == "pedro");
        CHECK(!memory.set_status(sky_clouds().metadata, larry::Status::Validated));
        CHECK(memory.validators().empty());
        memory.add_validator("pedro");
        memory.add_validator("pedro");
        memory.add_validator("");
        CHECK(memory.validators() == std::vector<std::string>{"pedro"});
        CHECK(memory.with_status(larry::Status::Validated).size() == 2);
        CHECK(memory.with_status(larry::Status::Proposed).empty());
        const auto recent = memory.recent(5);
        CHECK(recent.size() == 2);
        CHECK(recent.size() == 2 && recent[0].id == 2 && recent[1].id == 1);
        CHECK(memory.recent(1).size() == 1);
        CHECK(memory.find_id(2)->status == larry::Status::Validated);
        CHECK(!memory.find_id(3).has_value());
        CHECK(!memory.find_id(0).has_value());
    }
    // The log reads back to the same state.
    Memory again{file};
    CHECK(again.count() == 2);
    CHECK(again.find(c.metadata)->sources == (std::vector<std::string>{"lesson:1", "user"}));
    CHECK(again.find(c.metadata)->status == larry::Status::Validated);
    CHECK(again.find(c.metadata)->decided_by == "pedro");
    CHECK(again.find(d.metadata)->status == larry::Status::Validated);
    CHECK(again.find(d.metadata)->decided_by.empty());
    CHECK(again.validators() == std::vector<std::string>{"pedro"});
    CHECK(again.store(c.atom, c.metadata, larry::Status::Proposed, "user") == Stored::Same);
    CHECK(again.find(c.metadata)->sources.size() == 2);
    // A source with a comma or a tab is no problem: sources are hex.
    CHECK(again.store(d.atom, d.metadata, larry::Status::Proposed, "read:a,b\tc") == Stored::Same);
    Memory third{file};
    CHECK(third.find(d.metadata)->sources == std::vector<std::string>{"read:a,b\tc"});
}

TEST(clear_keeps_the_validators) {
    const std::filesystem::path file = fresh("larry_test_clear_validators.atoms");
    Memory memory{file};
    memory.add_validator("pedro");
    memory.store(sky_blue().atom, sky_blue().metadata);
    memory.clear();
    CHECK(memory.count() == 0);
    CHECK(memory.validators() == std::vector<std::string>{"pedro"});
    Memory again{file};
    CHECK(again.count() == 0);
    CHECK(again.validators() == std::vector<std::string>{"pedro"});
}

TEST(the_first_form_of_the_file_still_reads) {
    const std::filesystem::path file = fresh("larry_test_first_form.atoms");
    const Description c = sky_blue();
    {
        std::ofstream out{file, std::ios::binary};
        out << larry::hex::encode(c.metadata.bytes) << '\t' << larry::hex::encode(c.image.bytes) << '\n';
    }
    Memory memory{file};
    CHECK(memory.count() == 1);
    CHECK(memory.find(c.metadata)->status == larry::Status::Proposed);
    CHECK(memory.find(c.metadata)->sources.empty());
    const AtomOperations ops;
    CHECK(ops.text(memory.find(c.metadata)->description.atom) == "The sky is blue.");
}

TEST(a_log_line_that_changes_a_missing_atom_is_rejected) {
    const std::filesystem::path file = fresh("larry_test_bad_log.atoms");
    {
        std::ofstream out{file, std::ios::binary};
        out << "status\t00\tvalidated\n";
    }
    CHECK_THROWS(Memory{file}, std::runtime_error);
    {
        std::ofstream out{file, std::ios::binary | std::ios::trunc};
        out << "atom\t" << larry::hex::encode(sky_blue().metadata.bytes) << "\t41\tmaybe\n";
    }
    CHECK_THROWS(Memory{file}, std::runtime_error);
}

TEST(status_names) {
    CHECK(larry::name(larry::Status::Proposed) == "proposed");
    CHECK(larry::name(larry::Status::Validated) == "validated");
    CHECK(larry::name(larry::Status::Withdrawn) == "withdrawn");
    CHECK(larry::status_from("validated") == larry::Status::Validated);
    CHECK(!larry::status_from("maybe").has_value());
}

TEST(the_file_comes_from_the_environment) {
    const std::filesystem::path from_repository = Memory::file_from_environment(larry::Language::English);
    CHECK(from_repository.filename() == "en.atoms");
    CHECK(from_repository.parent_path().filename() == "memory");
    setenv("LARRY_MEMORY", "/tmp/elsewhere.atoms", 1);
    CHECK(Memory::file_from_environment(larry::Language::English) == "/tmp/elsewhere.atoms");
    unsetenv("LARRY_MEMORY");
    CHECK(Memory::file_from_environment(larry::Language::English) == from_repository);
}

int main() {
    return larry::test::run();
}
