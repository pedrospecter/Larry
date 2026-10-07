// Tests for AtomOperations: the metadata and the atom's bits.

#include "larry/atom_operations.hpp"

#include "check.hpp"

#include <algorithm>
#include <string_view>
#include <vector>

using larry::AtomOperations;
using larry::Bytes;
using larry::CategoryElectron;
using larry::Electrons;
using larry::EntitiesElectron;
using larry::Entity;
using larry::MetadataElectron;
using larry::TypeElectron;

namespace {

Bytes b(std::string_view text) {
    return Bytes(text.begin(), text.end());
}

Entity entity(std::string_view word, std::string_view category = "",
              std::vector<std::string_view> types = {}) {
    Entity e{.word = b(word), .category = b(category), .types = {}};
    for (const std::string_view t : types) {
        e.types.push_back(b(t));
    }
    return e;
}

MetadataElectron metadata(std::string_view category, std::string_view type,
                          std::vector<Entity> entities) {
    const AtomOperations ops;
    return ops.metadata(CategoryElectron{b(category)}, TypeElectron{b(type)},
                        EntitiesElectron{std::move(entities)});
}

bool starts_with(const Bytes& whole, const Bytes& prefix) {
    return whole.size() >= prefix.size() && std::equal(prefix.begin(), prefix.end(), whole.begin());
}

}  // namespace

TEST(from_text_and_bits) {
    const AtomOperations ops;
    const larry::Sentence atom = ops.from_text("A");
    CHECK(atom.size() == 8);
    CHECK(ops.to_bits(atom) == "01000001");
    CHECK(ops.text(atom) == "A");
    CHECK(ops.bytes(atom).size() == 1 && ops.bytes(atom)[0] == 0x41);
    const larry::Sentence utf8 = ops.from_text("céu");
    CHECK(utf8.size() == 32);
    CHECK(ops.text(utf8) == "céu");
}

TEST(different_electrons_give_different_metadata) {
    const MetadataElectron base = metadata("affirmation", "", {entity("the", "determiner")});
    CHECK(base.bytes != metadata("question", "", {entity("the", "determiner")}).bytes);
    CHECK(base.bytes != metadata("affirmation", "t", {entity("the", "determiner")}).bytes);
    CHECK(base.bytes != metadata("affirmation", "", {entity("a", "determiner")}).bytes);
    CHECK(base.bytes != metadata("affirmation", "", {entity("the", "noun")}).bytes);
    CHECK(base.bytes != metadata("affirmation", "", {entity("the", "determiner", {"x"})}).bytes);
    CHECK(base.bytes != metadata("affirmation", "", {}).bytes);
    CHECK(base.bytes !=
          metadata("affirmation", "", {entity("the", "determiner"), entity("", "")}).bytes);
}

TEST(parts_cannot_shift_into_each_other) {
    // The same bytes split differently between parts give different metadata.
    CHECK(metadata("ab", "", {}).bytes != metadata("a", "b", {}).bytes);
    CHECK(metadata("", "", {entity("ab", "")}).bytes != metadata("", "", {entity("a", "b")}).bytes);
    CHECK(metadata("", "", {entity("a", "b")}).bytes !=
          metadata("", "", {entity("a", "", {"b"})}).bytes);
    CHECK(metadata("", "", {entity("a", "", {"b", "c"})}).bytes !=
          metadata("", "", {entity("a", "", {"b"}), entity("c", "")}).bytes);
}

TEST(words_with_a_zero_byte) {
    const Bytes zero_inside = {'a', 0x00, 'b'};
    const Bytes zero_end = {'a', 0x00};
    const Entity with_inside{.word = zero_inside, .category = {}, .types = {}};
    const Entity with_end{.word = zero_end, .category = {}, .types = {}};
    const MetadataElectron m1 = metadata("", "", {with_inside});
    const MetadataElectron m2 = metadata("", "", {with_end, entity("b")});
    const MetadataElectron m3 = metadata("", "", {entity("a"), entity("b")});
    CHECK(m1.bytes != m2.bytes);
    CHECK(m1.bytes != m3.bytes);
    CHECK(m2.bytes != m3.bytes);
    const AtomOperations ops;
    CHECK(ops.electrons(m1).entities.entities[0].word == zero_inside);
    CHECK(ops.electrons(m2).entities.entities[0].word == zero_end);
}

TEST(atoms_that_share_leading_parts_sort_together) {
    const AtomOperations ops;
    const Entity the = entity("the", "determiner");
    const Entity sky = entity("sky", "noun");
    const Entity sea = entity("sea", "noun");
    const Entity is = entity("is", "auxiliary verb");
    const MetadataElectron sky_blue = metadata("affirmation", "", {the, sky, is, entity("blue", "adjective")});
    const MetadataElectron sky_big = metadata("affirmation", "", {the, sky, is, entity("big", "adjective")});
    const MetadataElectron sea_blue = metadata("affirmation", "", {the, sea, is, entity("blue", "adjective")});
    const MetadataElectron question = metadata("question", "", {the, sky, is, entity("blue", "adjective")});

    const Bytes prefix = ops.metadata_prefix(CategoryElectron{b("affirmation")}, TypeElectron{},
                                             std::vector<Entity>{the, sky});
    CHECK(starts_with(sky_blue.bytes, prefix));
    CHECK(starts_with(sky_big.bytes, prefix));
    CHECK(!starts_with(sea_blue.bytes, prefix));
    CHECK(!starts_with(question.bytes, prefix));

    // Sorted by bytes, the two atoms with the prefix are neighbours.
    std::vector<Bytes> keys{sea_blue.bytes, sky_blue.bytes, question.bytes, sky_big.bytes};
    std::ranges::sort(keys);
    const auto first = std::ranges::find_if(keys, [&](const Bytes& k) { return starts_with(k, prefix); });
    CHECK(first != keys.end());
    CHECK(first + 1 != keys.end() && starts_with(*(first + 1), prefix));
    CHECK(first + 2 == keys.end() || !starts_with(*(first + 2), prefix));
}

TEST(electrons_round_trip) {
    const AtomOperations ops;
    const CategoryElectron category{b("affirmation")};
    const TypeElectron type{b("")};
    const EntitiesElectron entities{{entity("The", "determiner"), entity("sky", "noun", {"singular"}),
                                     entity("is", "auxiliary verb", {"present", "third person"}),
                                     entity("", ""), entity("blue", "")}};
    const MetadataElectron m = ops.metadata(category, type, entities);
    const Electrons e = ops.electrons(m);
    CHECK(e.category.bytes == category.bytes);
    CHECK(e.type.bytes == type.bytes);
    CHECK(e.entities.entities.size() == entities.entities.size());
    for (std::size_t i = 0; i < entities.entities.size(); ++i) {
        CHECK(e.entities.entities[i].word == entities.entities[i].word);
        CHECK(e.entities.entities[i].category == entities.entities[i].category);
        CHECK(e.entities.entities[i].types == entities.entities[i].types);
    }
    // Writing the electrons again gives the same bytes.
    CHECK(ops.metadata(e.category, e.type, e.entities).bytes == m.bytes);
    // An atom with no entities round trips too.
    const MetadataElectron empty = ops.metadata(CategoryElectron{}, TypeElectron{}, EntitiesElectron{});
    CHECK(ops.electrons(empty).entities.entities.empty());
}

TEST(bytes_that_are_not_metadata_are_rejected) {
    const AtomOperations ops;
    CHECK_THROWS(ops.electrons(MetadataElectron{}), std::invalid_argument);
    CHECK_THROWS(ops.electrons(MetadataElectron{{0x00}}), std::invalid_argument);
    CHECK_THROWS(ops.electrons(MetadataElectron{{'a'}}), std::invalid_argument);
    CHECK_THROWS(ops.electrons(MetadataElectron{{0x00, 0x02}}), std::invalid_argument);
    const MetadataElectron good = metadata("a", "b", {entity("c", "d")});
    MetadataElectron truncated = good;
    truncated.bytes.pop_back();
    CHECK_THROWS(ops.electrons(truncated), std::invalid_argument);
    MetadataElectron trailing = good;
    trailing.bytes.push_back('x');
    CHECK_THROWS(ops.electrons(trailing), std::invalid_argument);
    MetadataElectron no_entity_end = good;
    no_entity_end.bytes.resize(no_entity_end.bytes.size() - 2);
    CHECK_THROWS(ops.electrons(no_entity_end), std::invalid_argument);
}

TEST(fold_lowers_ascii_only) {
    const AtomOperations ops;
    CHECK(ops.fold(b("Sky")) == b("sky"));
    CHECK(ops.fold(b("NASA")) == b("nasa"));
    CHECK(ops.fold(b("Éclair")) == b("Éclair"));
    CHECK(ops.fold(b("3.14")) == b("3.14"));
    CHECK(ops.fold(b("")) == b(""));
}

int main() {
    return larry::test::run();
}
