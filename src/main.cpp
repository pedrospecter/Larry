#include "larry/atom_operations.hpp"
#include "larry/base_rules.hpp"
#include "larry/cognition.hpp"
#include "larry/constellation.hpp"
#include "larry/electron.hpp"

#include <cstdio>
#include <exception>
#include <print>
#include <string_view>
#include <vector>

namespace {

larry::Bytes bytes(std::string_view text) {
    return larry::Bytes(text.begin(), text.end());
}

std::string_view as_text(const larry::Bytes& b) {
    return {reinterpret_cast<const char*>(b.data()), b.size()};
}

std::string_view name(larry::Language language) {
    switch (language) {
    case larry::Language::English:
        return "English";
    }
    return "unknown";
}

void run() {
    constexpr std::string_view text = "the sky is blue";

    const larry::Constellation constellation{larry::Language::English};
    const larry::BaseRules base_rules{constellation.language()};
    const larry::AtomOperations ops{};
    const larry::Cognition cognition{};
    const larry::Sentence atom = ops.from_text(text);

    const larry::TypeElectron type{};
    const larry::CategoryElectron category{};
    larry::EntitiesElectron entities{.entities = {
        {.word = bytes("the"), .category = {}, .types = {}},
        {.word = bytes("sky"), .category = {}, .types = {}},
        {.word = bytes("is"), .category = {}, .types = {}},
        {.word = bytes("blue"), .category = {}, .types = {}},
    }};
    const std::vector<larry::Bytes> categories{bytes("determiner"), bytes("noun"), bytes("verb"),
                                               bytes("adjective")};
    cognition.categorize(entities, categories, base_rules);
    const larry::ImageElectron image{.bytes = bytes(text)};
    const larry::MetadataElectron metadata = ops.metadata(category, type, entities);

    std::println("constellation : {}", name(constellation.language()));
    std::println("base rules    : {} categories", base_rules.categories().size());
    std::println("atom          : {} bits", atom.size());
    std::println("                {}", ops.to_bits(atom));
    std::println("type          : {} bytes", type.bytes.size());
    std::println("category      : {} bytes", category.bytes.size());
    std::println("entities      : {}", entities.entities.size());
    for (const larry::Entity& entity : entities.entities) {
        std::println("  {:<4}  category: {:<10}  types: {}", as_text(entity.word),
                     as_text(entity.category), entity.types.size());
    }
    std::println("image         : {} bytes \"{}\"", image.bytes.size(), as_text(image.bytes));
    std::println("metadata      : {} bytes", metadata.bytes.size());
}

}  // namespace

int main() {
    try {
        run();
    } catch (const std::exception& e) {
        std::println(stderr, "larry: {}", e.what());
        return 1;
    }
    return 0;
}
