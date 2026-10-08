#include "larry/forms.hpp"

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace larry {

namespace {

std::string text_of(const Bytes& b) {
    return std::string(b.begin(), b.end());
}

bool ends_with(const Bytes& word, const Bytes& ending) {
    return word.size() >= ending.size() &&
           std::equal(ending.rbegin(), ending.rend(), word.rbegin());
}

std::vector<Bytes> split(const Bytes& value, char separator) {
    std::vector<Bytes> out;
    Bytes current;
    for (const std::uint8_t c : value) {
        if (c == static_cast<std::uint8_t>(separator)) {
            out.push_back(current);
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    out.push_back(current);
    return out;
}

bool is_vowel(std::uint8_t c) {
    return c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u';
}

}  // namespace

Forms::Forms(const BaseRules& rules) : rules_(&rules) {}

std::vector<Form> Forms::candidates(const Bytes& word) const {
    std::vector<Form> out;
    for (const auto& [ending, value] : rules_->endings()) {  // longest first
        if (!ends_with(word, ending) || word.size() < ending.size() + 2) {
            continue;
        }
        const std::vector<Bytes> parts = split(value, ':');
        if (parts.size() != 2) {
            continue;
        }
        const Bytes stem(word.begin(), word.end() - static_cast<std::ptrdiff_t>(ending.size()));
        std::vector<std::pair<Bytes, std::string>> bases;
        const std::string e = text_of(ending);
        if (e.starts_with("ie")) {
            // "skies": sky + ies; "tried": try + ied; "happier": happy + ier.
            Bytes y = stem;
            y.push_back('y');
            bases.emplace_back(y, "y + " + e);
        } else {
            bases.emplace_back(stem, e);
            if (e != "s") {
                // "loved": love + d; "larger": large + r.
                Bytes with_e = stem;
                with_e.push_back('e');
                bases.emplace_back(with_e, "e + " + e.substr(1));
                // "stopped": stop + p + ed.
                if (stem.size() >= 3 && stem.back() == stem[stem.size() - 2] && !is_vowel(stem.back()) &&
                    stem.back() != 's' && stem.back() != 'l' && stem.back() != 'f') {
                    Bytes undoubled(stem.begin(), stem.end() - 1);
                    bases.emplace_back(undoubled, std::string{static_cast<char>(stem.back())} + " + " + e);
                }
            }
        }
        for (const auto& [base, how] : bases) {
            out.push_back({word, base, parts[0], parts[1], "ending " + e + " (" + how + ")"});
        }
    }
    for (const auto& [form, value] : rules_->irregular()) {
        if (form != word) {
            continue;
        }
        const std::vector<Bytes> parts = split(value, ':');
        if (parts.size() == 3) {
            out.push_back({word, parts[0], parts[1], parts[2], "irregular pair"});
        }
    }
    return out;
}

std::optional<Form> Forms::base_of(const Bytes& word, const Known& known) const {
    for (const Form& form : candidates(word)) {
        if (form.base == word) {
            continue;  // "run" is a base itself
        }
        if (form.rule == "irregular pair" || known(form.base, form.category)) {
            return form;
        }
    }
    return std::nullopt;
}

std::optional<Form> Forms::by_ending(const Bytes& word) const {
    for (const auto& [ending, value] : rules_->endings()) {
        if (!ends_with(word, ending) || word.size() < ending.size() + 2) {
            continue;
        }
        // Every entry for this ending must give one category.
        Bytes category;
        Bytes feature;
        bool one = true;
        for (const auto& [other, other_value] : rules_->endings()) {
            if (other != ending) {
                continue;
            }
            const std::vector<Bytes> parts = split(other_value, ':');
            if (parts.size() != 2) {
                continue;
            }
            if (category.empty()) {
                category = parts[0];
                feature = parts[1];
            } else if (category != parts[0]) {
                one = false;
            }
        }
        if (!one || category.empty()) {
            return std::nullopt;
        }
        const Bytes stem(word.begin(), word.end() - static_cast<std::ptrdiff_t>(ending.size()));
        return Form{word, stem, category, feature, "ending " + text_of(ending) + " alone"};
    }
    return std::nullopt;
}

}  // namespace larry
