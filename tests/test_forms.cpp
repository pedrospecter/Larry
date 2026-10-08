// Tests for Forms (A4): the base of a regular or irregular form.

#include "larry/forms.hpp"

#include "larry/base_rules.hpp"

#include "check.hpp"

#include <fstream>
#include <map>
#include <print>
#include <string>
#include <string_view>
#include <vector>

using larry::Bytes;
using larry::Form;
using larry::Forms;

namespace {

const larry::BaseRules& rules() {
    static const larry::BaseRules instance{larry::Language::English};
    return instance;
}

Bytes b(std::string_view text) {
    return Bytes(text.begin(), text.end());
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) {
        s.remove_suffix(1);
    }
    return s;
}

std::vector<std::string> fields(std::string_view line) {
    std::vector<std::string> out;
    while (true) {
        const std::size_t at = line.find(" | ");
        out.emplace_back(trim(line.substr(0, at)));
        if (at == std::string_view::npos) {
            break;
        }
        line.remove_prefix(at + 3);
    }
    return out;
}

// The vocabulary the test knows: a base with its category.
const std::multimap<std::string, std::string>& vocabulary() {
    static const std::multimap<std::string, std::string> words = {
        {"sky", "noun"},    {"fly", "noun"},     {"fly", "verb"},      {"cat", "noun"},      {"box", "noun"},
        {"dish", "noun"},   {"city", "noun"},    {"walk", "verb"},     {"go", "verb"},       {"try", "verb"},
        {"love", "verb"},   {"stop", "verb"},    {"run", "verb"},      {"tall", "adjective"}, {"large", "adjective"},
        {"big", "adjective"}, {"happy", "adjective"}, {"child", "noun"}, {"be", "auxiliary verb"},
        {"good", "adjective"}, {"mouse", "noun"}, {"this", "determiner"},
    };
    return words;
}

bool known(const Bytes& base, const Bytes& category) {
    const std::string word(base.begin(), base.end());
    const std::string wanted(category.begin(), category.end());
    const auto [first, last] = vocabulary().equal_range(word);
    for (auto it = first; it != last; ++it) {
        if (it->second == wanted) {
            return true;
        }
    }
    return false;
}

}  // namespace

TEST(regular_forms_give_their_base_by_the_ending) {
    const Forms forms{rules()};
    const std::optional<Form> skies = forms.base_of(b("skies"), known);
    CHECK(skies.has_value());
    if (skies) {
        CHECK(skies->base == b("sky"));
        CHECK(skies->category == b("noun"));
        CHECK(skies->feature == b("plural"));
        CHECK(skies->rule == "ending ies (y + ies)");
    }
    const std::optional<Form> stopped = forms.base_of(b("stopped"), known);
    CHECK(stopped.has_value() && stopped->base == b("stop") && stopped->rule == "ending ed (p + ed)");
    const std::optional<Form> loved = forms.base_of(b("loved"), known);
    CHECK(loved.has_value() && loved->base == b("love") && loved->rule == "ending ed (e + d)");
    // Several candidates: the known base wins; "goes" is go + es, not goe + s.
    const std::optional<Form> goes = forms.base_of(b("goes"), known);
    CHECK(goes.has_value() && goes->base == b("go") && goes->feature == b("third person"));
    // The candidates, in the order tried.
    const std::vector<Form> cands = forms.candidates(b("walked"));
    CHECK(cands.size() == 2);
    CHECK(cands.size() == 2 && cands[0].base == b("walk") && cands[1].base == b("walke"));
    // Too short for the ending, no ending, or a base itself: nothing.
    CHECK(!forms.base_of(b("is"), known).has_value() || forms.base_of(b("is"), known)->rule == "irregular pair");
    CHECK(!forms.base_of(b("sky"), known).has_value());
    CHECK(!forms.base_of(b("run"), known).has_value());
    CHECK(!forms.base_of(b("ss"), known).has_value());
    CHECK(forms.candidates(b("us")).empty());  // "s" needs two bytes before it
    CHECK(!forms.candidates(b("bus")).empty());  // "bu" is tried, and known() says no
}

TEST(irregular_forms_come_from_the_pairs) {
    const Forms forms{rules()};
    const std::optional<Form> went = forms.base_of(b("went"), known);
    CHECK(went.has_value());
    if (went) {
        CHECK(went->base == b("go"));
        CHECK(went->category == b("verb"));
        CHECK(went->feature == b("past"));
        CHECK(went->rule == "irregular pair");
    }
    const std::optional<Form> was = forms.base_of(b("was"), known);
    CHECK(was.has_value() && was->base == b("be") && was->category == b("auxiliary verb"));
    CHECK(forms.base_of(b("children"), known)->base == b("child"));
    CHECK(forms.base_of(b("better"), known)->base == b("good"));
    // A pair wins over an ending that leads nowhere: "lives" is life + s, not live + s here.
    CHECK(forms.base_of(b("lives"), known)->base == b("life"));
}

TEST(an_ending_alone_proposes_a_category_when_it_is_unambiguous) {
    const Forms forms{rules()};
    const std::optional<Form> zorping = forms.by_ending(b("zorping"));
    CHECK(zorping.has_value());
    if (zorping) {
        CHECK(zorping->category == b("verb"));
        CHECK(zorping->feature == b("progressive"));
        CHECK(zorping->base == b("zorp"));
        CHECK(zorping->rule == "ending ing alone");
    }
    CHECK(forms.by_ending(b("zorped"))->category == b("verb"));
    CHECK(forms.by_ending(b("zorpest"))->category == b("adjective"));
    CHECK(!forms.by_ending(b("zorps")).has_value());   // a noun or a verb
    CHECK(!forms.by_ending(b("zorpies")).has_value());
    CHECK(!forms.by_ending(b("zorp")).has_value());
}

TEST(the_held_out_list_passes) {
    const Forms forms{rules()};
    std::ifstream in{std::string{LARRY_TEST_DATA_DIR} + "/en/forms.txt"};
    CHECK(in.good());
    int cases = 0;
    int failed = 0;
    std::size_t number = 0;
    for (std::string line; std::getline(in, line);) {
        ++number;
        if (line.empty() || line.starts_with('#')) {
            continue;
        }
        const std::vector<std::string> f = fields(line);
        if (f.size() < 2) {
            continue;
        }
        ++cases;
        const std::optional<Form> form = forms.base_of(b(f[0]), known);
        const std::string got_base = form ? std::string(form->base.begin(), form->base.end()) : "none";
        const std::string got_category = form ? std::string(form->category.begin(), form->category.end()) : "";
        const std::string got_feature = form ? std::string(form->feature.begin(), form->feature.end()) : "";
        const std::string want_category = f.size() > 2 ? f[2] : "";
        const std::string want_feature = f.size() > 3 ? f[3] : "";
        if (got_base != f[1] || got_category != want_category || got_feature != want_feature) {
            ++failed;
            std::println("forms.txt line {}: {} expected {} {} {}, got {} {} {}", number, f[0], f[1], want_category,
                         want_feature, got_base, got_category, got_feature);
        }
    }
    CHECK(cases >= 25);
    CHECK(failed == 0);
}

int main() {
    return larry::test::run();
}
