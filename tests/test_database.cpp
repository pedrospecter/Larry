// Tests for Database: the cloud, in a scratch schema of the local server.
// Skipped (exit 77) when no server answers: set LARRY_TEST_DB or LARRY_DB.

#include "larry/database.hpp"

#include "larry/atom_operations.hpp"
#include "larry/hex.hpp"

#include "check.hpp"

#include <cstdlib>
#include <exception>
#include <format>
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

TEST(the_same_words_with_other_types_are_one_conception) {
    db().clear();
    const AtomOperations ops;
    const Description first = sky_blue();
    Description second = sky_blue();
    second.entities.entities[1].types = {b("singular"), b("subject")};
    second.entities.entities[3].types = {b("positive"), b("attribute")};
    second.type.bytes = b("subject subject predicate attribute / neutral");
    second.metadata = ops.metadata(second.category, second.type, second.entities);
    CHECK(db().store(first.atom, first.metadata, Status::Proposed, "lesson:test") == Stored::New);
    CHECK(db().store(second.atom, second.metadata, Status::Proposed, "user:pedro") == Stored::Same);
    CHECK(db().count() == 1);
    const auto held = db().find(second.metadata);
    CHECK(held.has_value());
    if (!held) {
        return;
    }
    CHECK(held->description.metadata.bytes == first.metadata.bytes);
    CHECK(held->sources == (std::vector<std::string>{"lesson:test", "user:pedro"}));
    CHECK(db().redescribe(held->id, second.metadata));
    CHECK(!db().redescribe(held->id, second.metadata));
    CHECK(!db().redescribe(held->id + 1000, second.metadata));
    CHECK(db().find(first.metadata)->description.metadata.bytes == second.metadata.bytes);
    CHECK(db().count_words() == 4);
    CHECK(db().uses(b("sky")).size() == 1);
    CHECK(db().find_prefix(second.metadata.bytes).size() == 1);
    CHECK(db().find_prefix(first.metadata.bytes).empty());
    // Rows from before the identity existed get theirs on apply_schema, and
    // two rows of one conception merge into the first, with every source.
    db().run("update conceptions set identity = null");
    db().run(std::format("insert into conceptions (metadata, bytes, status) values (decode('{}', 'hex'), "
                         "decode('{}', 'hex'), 'proposed')",
                         larry::hex::encode(first.metadata.bytes), larry::hex::encode(b("The sky is blue."))));
    db().run("insert into sources (conception, source) select max(id), 'read:old' from conceptions");
    CHECK(db().count() == 2);
    db().apply_schema();
    CHECK(db().count() == 1);
    const auto merged = db().find(first.metadata);
    CHECK(merged.has_value());
    if (merged) {
        CHECK(merged->id == held->id);
        CHECK(merged->description.metadata.bytes == second.metadata.bytes);
        CHECK(merged->sources == (std::vector<std::string>{"lesson:test", "user:pedro", "read:old"}));
    }
    CHECK(db().count_words() == 4);
    // Applying the schema again changes nothing.
    db().apply_schema();
    CHECK(db().count() == 1);
}

TEST(the_reading_is_a_column) {
    db().clear();
    const Description d = describe("Sky is blue.", {"Sky", "is", "blue"}, {"noun", "auxiliary verb", "adjective"});
    CHECK(db().store(d.atom, d.metadata, Status::Proposed, "user:pedro") == Stored::New);
    const auto held = db().find(d.metadata);
    CHECK(held.has_value());
    if (!held) {
        return;
    }
    CHECK(held->reading.empty());
    db().set_reading(held->id, "The sky is blue.");
    CHECK(db().find(d.metadata)->reading == "The sky is blue.");
    CHECK(db().find_id(held->id)->reading == "The sky is blue.");
    CHECK(db().all().front().reading == "The sky is blue.");
    db().set_reading(held->id, "");
    CHECK(db().find(d.metadata)->reading.empty());
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
    db().set_status(1, Status::Withdrawn, "pedro");
    CHECK(db().find(c.metadata)->status == Status::Withdrawn);
    CHECK(db().find(c.metadata)->decided_by == "pedro");
    CHECK(db().find_id(2)->decided_by.empty());
    CHECK(db().find_id(2)->status == Status::Validated);
    CHECK(db().find_id(2)->sources == std::vector<std::string>{"lesson:1"});
    CHECK(!db().find_id(3).has_value());
    // The validators stay through a clear; nothing else does.
    CHECK(db().validators().empty());
    db().add_validator("pedro");
    db().add_validator("pedro");
    db().add_validator("");
    db().add_validator("ana");
    CHECK(db().validators() == (std::vector<std::string>{"pedro", "ana"}));
    db().clear();
    CHECK(db().validators() == (std::vector<std::string>{"pedro", "ana"}));
    CHECK(!db().find_id(2).has_value());
    db().run("truncate validators restart identity");
}

TEST(bonds_both_ways_and_through_a_clear) {
    using larry::Bond;
    using larry::BondEnd;
    db().clear();
    db().run("truncate bonds, bond_origins restart identity cascade");
    const Description c = sky_blue();
    const Description d = sea_blue();
    db().store(c.atom, c.metadata, Status::Proposed, "user");
    db().store(d.atom, d.metadata, Status::Proposed, "user");
    const Bond forms{b("form of"), BondEnd::entity("Skies"), BondEnd::entity("sky"), {"user:pedro"}};
    const Bond conflict{b("conflicts with"), BondEnd::atom(c.metadata), BondEnd::atom(d.metadata), {"rule: one colour"}};
    CHECK(db().count_bonds() == 0);
    CHECK(db().bond(forms));
    CHECK(!db().bond(forms));
    CHECK(db().bond(conflict));
    CHECK(!db().bond(Bond{b("form of"), BondEnd::entity("skies"), BondEnd::entity("Sky"), {"lesson:forms"}}));
    CHECK(db().count_bonds() == 2);
    CHECK(db().bonds_from(BondEnd::entity("skies")).size() == 1);
    CHECK(db().bonds_from(BondEnd::entity("skies")).front().origins ==
          (std::vector<std::string>{"user:pedro", "lesson:forms"}));
    CHECK(db().bonds_to(BondEnd::entity("sky")).size() == 1);
    CHECK(db().bonds_to(BondEnd::entity("skies")).empty());
    CHECK(db().bonds_of(BondEnd::entity("sky")).size() == 1);
    CHECK(db().bonds_of(BondEnd::atom(d.metadata)).size() == 1);
    CHECK(db().bonds_to(BondEnd::atom(d.metadata)).front().from == BondEnd::atom(c.metadata));
    CHECK(db().bonds_to(BondEnd::atom(d.metadata)).front().kind == b("conflicts with"));
    CHECK(db().bonds().size() == 2);
    CHECK(db().find_identity(AtomOperations{}.identity(c.metadata))->id == 1);
    CHECK(!db().find_identity(b("nobody")).has_value());
    CHECK_THROWS(db().bond(Bond{{}, BondEnd::entity("a"), BondEnd::entity("b"), {}}), std::invalid_argument);
    db().clear();
    CHECK(db().count() == 0);
    CHECK(db().count_bonds() == 2);
    db().run("truncate bonds, bond_origins restart identity cascade");
}

TEST(molecules_in_order_through_a_clear) {
    using larry::Member;
    using larry::Molecule;
    db().clear();
    db().run("truncate molecules, molecule_members restart identity cascade");
    const AtomOperations ops;
    const Description c = sky_blue();
    const Description d = sea_blue();
    const Bytes sky = ops.identity(c.metadata);
    const Bytes sea = ops.identity(d.metadata);
    const Bytes text = b("read:sky.txt:2026-10-08T07:58:00Z");
    const Bytes chat = b("chat:pedro:2026-10-08T08:00:00Z");
    CHECK(db().count_molecules() == 0);
    CHECK(!db().molecule(text).has_value());
    CHECK(db().join(text, sky, "read:sky.txt", "2026-10-08T07:58:01Z") == 0);
    CHECK(db().join(text, sea, "read:sky.txt", "2026-10-08T07:58:02Z") == 1);
    CHECK(db().join(chat, sea, "user:pedro", "2026-10-08T08:00:05Z") == 0);
    CHECK(db().join(chat, sea, "user:pedro", "2026-10-08T08:00:09Z") == 1);
    CHECK(db().count_molecules() == 2);
    CHECK(db().molecules() == (std::vector<Bytes>{text, chat}));
    const std::optional<Molecule> m = db().molecule(text);
    CHECK(m.has_value());
    if (m) {
        CHECK(m->members == (std::vector<Member>{{sky, "read:sky.txt", "2026-10-08T07:58:01Z"},
                                                 {sea, "read:sky.txt", "2026-10-08T07:58:02Z"}}));
    }
    CHECK(db().molecule(chat)->members[1].when == "2026-10-08T08:00:09Z");
    CHECK_THROWS(db().join({}, sky, "x", "y"), std::invalid_argument);
    db().clear();
    CHECK(db().count_molecules() == 2);
    CHECK(db().molecule(text)->members.size() == 2);
    db().run("truncate molecules, molecule_members restart identity cascade");
}

TEST(open_makes_the_database_when_the_server_lacks_it) {
    // The scratch database must not exist; open() creates it through "postgres".
    db().run("drop database if exists larry_test_open");
    const std::string base = std::getenv("LARRY_TEST_DB") != nullptr && *std::getenv("LARRY_TEST_DB") != '\0'
                                 ? std::getenv("LARRY_TEST_DB")
                             : std::getenv("LARRY_DB") != nullptr && *std::getenv("LARRY_DB") != '\0'
                                 ? std::getenv("LARRY_DB")
                                 : "dbname=larry";
    std::string connection;
    bool replaced = false;
    std::string_view rest{base};
    while (!rest.empty()) {
        const std::size_t space = rest.find(' ');
        const std::string_view item = rest.substr(0, space);
        connection += item.starts_with("dbname=") ? (replaced = true, "dbname=larry_test_open") : std::string{item};
        if (space == std::string_view::npos) {
            break;
        }
        connection += ' ';
        rest.remove_prefix(space + 1);
    }
    if (!replaced) {
        connection += " dbname=larry_test_open";
    }
    CHECK_THROWS(Database{connection}, std::runtime_error);
    {
        const std::unique_ptr<Database> made = Database::open(connection);
        CHECK(made->count() == 0);
        made->add_validator("pedro");
        CHECK(made->validators() == std::vector<std::string>{"pedro"});
    }
    {
        // The second time it is simply opened.
        const std::unique_ptr<Database> again = Database::open(connection);
        CHECK(again->validators() == std::vector<std::string>{"pedro"});
    }
    db().run("drop database larry_test_open");
    // A server that is not there is still an error.
    CHECK_THROWS(Database::open("host=192.0.2.1 connect_timeout=1 dbname=larry"), std::runtime_error);
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
