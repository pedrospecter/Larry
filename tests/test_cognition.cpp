// Tests for Cognition: categorizing and qualifying.

#include "larry/cognition.hpp"

#include "larry/atom_operations.hpp"
#include "larry/base_rules.hpp"

#include "check.hpp"

#include <stdexcept>
#include <string_view>
#include <vector>

using larry::Bytes;
using larry::Cognition;
using larry::EntitiesElectron;
using larry::Entity;

namespace {

Bytes b(std::string_view text) {
    return Bytes(text.begin(), text.end());
}

EntitiesElectron words(std::vector<std::string_view> list) {
    EntitiesElectron out;
    for (const std::string_view word : list) {
        out.entities.push_back(Entity{.word = b(word), .category = {}, .types = {}});
    }
    return out;
}

std::vector<Bytes> categories(std::vector<std::string_view> list) {
    std::vector<Bytes> out;
    for (const std::string_view item : list) {
        out.push_back(b(item));
    }
    return out;
}

const larry::BaseRules& rules() {
    static const larry::BaseRules instance{larry::Language::English};
    return instance;
}

}  // namespace

TEST(categorize_stores_each_category) {
    const Cognition cognition;
    EntitiesElectron entities = words({"the", "sky", "is", "blue"});
    cognition.categorize(entities, categories({"determiner", "noun", "auxiliary verb", "adjective"}),
                         rules());
    CHECK(entities.entities[0].category == b("determiner"));
    CHECK(entities.entities[1].category == b("noun"));
    CHECK(entities.entities[2].category == b("auxiliary verb"));
    CHECK(entities.entities[3].category == b("adjective"));
}

TEST(categorize_rejects_the_wrong_count) {
    const Cognition cognition;
    EntitiesElectron entities = words({"the", "sky"});
    CHECK_THROWS(cognition.categorize(entities, categories({"determiner"}), rules()),
                 std::invalid_argument);
    CHECK_THROWS(cognition.categorize(entities, categories({"determiner", "noun", "noun"}), rules()),
                 std::invalid_argument);
    CHECK(entities.entities[0].category.empty());
}

TEST(categorize_rejects_an_unknown_category) {
    const Cognition cognition;
    EntitiesElectron entities = words({"the", "sky"});
    CHECK_THROWS(cognition.categorize(entities, categories({"determiner", "colour"}), rules()),
                 std::invalid_argument);
    CHECK_THROWS(cognition.categorize(entities, categories({"Determiner", "noun"}), rules()),
                 std::invalid_argument);
    CHECK(entities.entities[0].category.empty());
    CHECK(entities.entities[1].category.empty());
}

TEST(qualification_names) {
    CHECK(larry::name(larry::Qualification::Affirmation) == "affirmation");
    CHECK(larry::name(larry::Qualification::Question) == "question");
    CHECK(larry::name(larry::Qualification::Order) == "order");
    CHECK(larry::name(larry::Qualification::Assumption) == "assumption");
    CHECK(larry::name(larry::Qualification::Expression) == "expression");
}

TEST(qualify_has_no_rules_yet) {
    const Cognition cognition;
    const larry::AtomOperations ops;
    CHECK_THROWS(cognition.qualify(ops.from_text("the sky is blue"), words({"the", "sky", "is", "blue"}),
                                   rules()),
                 std::logic_error);
}

int main() {
    return larry::test::run();
}
