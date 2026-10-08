#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace larry {

/// A JSON value, as Larry reads the answers of the web (W1) and the stories
/// of a test set: objects (in the order written), arrays, strings, numbers,
/// booleans and null.
struct Json;
using JsonObject = std::vector<std::pair<std::string, Json>>;
using JsonArray = std::vector<Json>;

struct Json {
    std::variant<std::nullptr_t, bool, double, std::string, JsonArray, JsonObject> value;

    /// The value under a key of an object, or null.
    [[nodiscard]] const Json* get(std::string_view key) const {
        if (const auto* object = std::get_if<JsonObject>(&value)) {
            for (const auto& [k, v] : *object) {
                if (k == key) {
                    return &v;
                }
            }
        }
        return nullptr;
    }
    [[nodiscard]] const std::string* string() const { return std::get_if<std::string>(&value); }
    [[nodiscard]] const JsonArray* array() const { return std::get_if<JsonArray>(&value); }
    [[nodiscard]] const JsonObject* object() const { return std::get_if<JsonObject>(&value); }
    [[nodiscard]] const double* number() const { return std::get_if<double>(&value); }
    [[nodiscard]] const bool* boolean() const { return std::get_if<bool>(&value); }
};

/// Reads a JSON text. Throws std::runtime_error, naming `who`, on bytes
/// that are not JSON.
[[nodiscard]] Json parse_json(std::string_view text, std::string_view who = "Json");

}  // namespace larry
