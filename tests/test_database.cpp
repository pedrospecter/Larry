// Tests for Database: the cloud, in a scratch schema of the local server.
// Skipped (exit 77) when no server answers: set LARRY_TEST_DB or LARRY_DB.

#include "larry/database.hpp"

#include "larry/atom_operations.hpp"

#include "check.hpp"

#include <cstdlib>
#include <exception>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using larry::AtomOperations;
using larry::Bytes;
using larry::Database;
using larry::Description;
using larry::Status;
using larry::Stored;

namespace {

Bytes b(std::string_view text) {
    return Bytes(text.begin(), text.end());
}

std::unique_ptr<Database> database;

Database& db() {
    return *database;
}

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
    db().clear();
    const Description d = sky_blue();
    CHECK(db().store(d.atom, d.metadata, Status::Proposed, "lesson:test") == Stored::New);
    CHECK(db().count() == 1);
    CHECK(db().count_words() == 4);
    const auto found = db().find(d.metadata);
    CHECK(found.has_value());
    const AtomOperations ops;
    CHECK(found->id == 1);
    CHECK(ops.text(found->description.atom) == "The sky is blue.");
    CHECK(found->description.metadata.bytes == d.metadata.bytes);
    CHECK(found->description.category.bytes == b("affirmation"));
    CHECK(found->description.entities.entities.size() == 4);
    CHECK(found->description.entities.entities[1].word == b("sky"));
    CHECK(found->description.entities.entities[1].category == b("noun"));
    CHECK(found->status == Status::Proposed);
    CHECK(found->sources == std::vector<std::string>{"lesson:test"});
    CHECK(!db().find(sea_blue().metadata).has_value());
}

TEST(the_same_atom_again_only_adds_its_source) {
    db().clear();
    const Description d = sky_blue();
    CHECK(db().store(d.atom, d.metadata, Status::Proposed, "lesson:test") == Stored::New);
    CHECK(db().store(d.atom, d.metadata, Status::Proposed, "lesson:test") == Stored::Same);
    CHECK(db().store(d.atom, d.metadata, Status::Proposed, "user") == Stored::Same);
    CHECK(db().count() == 1);
    CHECK(db().count_words() == 4);
    const auto found = db().find(d.metadata);
    CHECK(found->sources == (std::vector<std::string>{"lesson:test", "user"}));
    // The same words in another form keep the first atom.
    const Description other = describe("The sky is blue", {"The", "sky", "is", "blue"},
                                       {"determiner", "noun", "auxiliary verb", "adjective"});
    CHECK(db().store(other.atom, other.metadata, Status::Proposed, "read:x") == Stored::SameForm);
    const AtomOperations ops;
    CHECK(ops.text(db().find(other.metadata)->description.atom) == "The sky is blue.");
    CHECK(db().find(other.metadata)->sources.size() == 3);
}

TEST(find_prefix_all_recent_and_containing) {
    db().clear();
    const AtomOperations ops;
    const Description a = sky_clouds();
    const Description c = sky_blue();
    const Description d = sea_blue();
    db().store(a.atom, a.metadata, Status::Proposed, "");
    db().store(c.atom, c.metadata, Status::Proposed, "");
    db().store(d.atom, d.metadata, Status::Proposed, "");
    const Bytes the_sky = ops.metadata_prefix(c.category, c.type, std::span{c.entities.entities}.first(2));
    const auto found = db().find_prefix(the_sky);
    CHECK(found.size() == 1);
    CHECK(found.size() == 1 && ops.text(found[0].description.atom) == "The sky is blue.");
    const Bytes the = ops.metadata_prefix(c.category, c.type, std::span{c.entities.entities}.first(1));
    CHECK(db().find_prefix(the).size() == 2);
    CHECK(db().find_prefix({}).size() == 3);
    CHECK(db().find_prefix(b("no such prefix")).empty());
    const auto all = db().all();
    CHECK(all.size() == 3);
    CHECK(all.size() == 3 && all[0].id == 1 && all[2].id == 3);
    CHECK(all.size() == 3 && all[0].sources.empty());
    const auto recent = db().recent(2);
    CHECK(recent.size() == 2);
    CHECK(recent.size() == 2 && recent[0].id == 3 && recent[1].id == 2);
    CHECK(db().containing(b("sky")).size() == 2);
    CHECK(db().containing(b("the")).size() == 3);
    CHECK(db().containing(b("moon")).empty());
}

TEST(word_index_with_context) {
    db().clear();
    const Description c = sky_blue();
    const Description a = sky_clouds();
    db().store(c.atom, c.metadata, Status::Proposed, "");
    db().store(a.atom, a.metadata, Status::Proposed, "");
    const auto sky = db().uses(b("sky"));
    CHECK(sky.size() == 2);
    CHECK(sky.size() == 2 && sky[0].atom == 1 && sky[0].position == 1 && sky[0].category == b("noun"));
    CHECK(sky.size() == 2 && sky[0].before == b("the") && sky[0].after == b("is"));
    CHECK(sky.size() == 2 && sky[0].before_category == b("determiner") &&
          sky[0].after_category == b("auxiliary verb"));
    CHECK(sky.size() == 2 && sky[1].atom == 2 && sky[1].position == 3 && sky[1].after.empty() &&
          sky[1].after_category.empty());
    const auto the = db().categories_of(b("the"));
    CHECK(the.size() == 1);
    CHECK(the.size() == 1 && the[0].category == b("determiner") && the[0].count == 2);
    CHECK(db().categories_of(b("The")).empty());
    const Description unknown = describe("Azure sky.", {"Azure", "sky"}, {"", "noun"});
    db().store(unknown.atom, unknown.metadata, Status::Proposed, "");
    CHECK(db().categories_of(b("azure")).empty());
    CHECK(db().uses(b("azure")).size() == 1);
    // A guessed category is no evidence in the index either.
    Description guessed = describe("Azure sea.", {"Azure", "sea"}, {"adjective", "noun"});
    guessed.entities.entities[0].types.push_back(b("guessed"));
    const AtomOperations ops;
    guessed.metadata = ops.metadata(guessed.category, guessed.type, guessed.entities);
    db().store(guessed.atom, guessed.metadata, Status::Proposed, "");
    CHECK(db().categories_of(b("azure")).empty());
    CHECK(db().uses(b("sea")).front().before_category.empty());
}

TEST(status_changes) {
    db().clear();
    const Description c = sky_blue();
    const Description d = sea_blue();
    db().store(c.atom, c.metadata, Status::Proposed, "user");
    db().store(d.atom, d.metadata, Status::Validated, "lesson:1");
    CHECK(db().with_status(Status::Proposed).size() == 1);
    CHECK(db().with_status(Status::Validated).size() == 1);
    CHECK(db().with_status(Status::Withdrawn).empty());
    db().set_status(1, Status::Validated);
    CHECK(db().with_status(Status::Validated).size() == 2);
    CHECK(db().find(c.metadata)->status == Status::Validated);
    db().set_status(1, Status::Withdrawn);
    CHECK(db().find(c.metadata)->status == Status::Withdrawn);
    CHECK(db().find_id(2)->status == Status::Validated);
    CHECK(db().find_id(2)->sources == std::vector<std::string>{"lesson:1"});
    CHECK(!db().find_id(3).has_value());
}

TEST(bytes_that_are_not_metadata_are_rejected) {
    db().clear();
    const AtomOperations ops;
    CHECK_THROWS(db().store(ops.from_text("x"), larry::MetadataElectron{{0xFF}}, Status::Proposed, ""),
                 std::invalid_argument);
    CHECK(db().count() == 0);
}

int main() {
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
    connection += " options='-c search_path=larry_test'";
    try {
        database = std::make_unique<Database>(connection);
        database->run("drop schema if exists larry_test cascade; create schema larry_test");
        database->apply_schema();
    } catch (const std::exception& e) {
        return larry::test::skip(std::string{"no database: "} + e.what());
    }
    const int result = larry::test::run();
    database->run("drop schema if exists larry_test cascade");
    database.reset();
    return result;
}
