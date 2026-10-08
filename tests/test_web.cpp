// Tests for Web: the web as a source (W1), on recorded answers. The live
// checks run only with LARRY_TEST_WEB=1.

#include "larry/web.hpp"

#include "check.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <print>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using larry::Bytes;
using larry::Hit;
using larry::Meaning;
using larry::Page;
using larry::Web;

namespace {

Bytes b(std::string_view text) {
    return Bytes(text.begin(), text.end());
}

std::string recorded(std::string_view name) {
    const std::filesystem::path file = std::filesystem::path{LARRY_TEST_DATA_DIR} / "web" / name;
    std::ifstream in{file, std::ios::binary};
    return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

std::filesystem::path scratch() {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "larry_test_web_content";
    std::filesystem::remove_all(dir);
    return dir;
}

}  // namespace

TEST(search_answers_parse_into_hits) {
    const std::vector<Hit> hits = Web::parse_search(recorded("search_sky.json"));
    CHECK(hits.size() == 5);
    CHECK(!hits.empty() && hits.front().title == "Sky");
    CHECK(!hits.empty() && hits.front().snippet.starts_with("The sky is an unobstructed view upward"));
    CHECK(!hits.empty() && hits.front().snippet.find('<') == std::string::npos);
    CHECK(!hits.empty() && hits.front().url == "https://en.wikipedia.org/wiki/Sky");
    CHECK(hits.size() > 1 && hits[1].title == "Sky (disambiguation)");
    CHECK(hits.size() > 1 && hits[1].url == "https://en.wikipedia.org/wiki/Sky_%28disambiguation%29");
    CHECK(Web::parse_search("{\"query\":{\"search\":[]}}").empty());
    CHECK(Web::parse_search("{}").empty());
    CHECK_THROWS(Web::parse_search("not json"), std::runtime_error);
    CHECK_THROWS(Web::parse_search("{\"query\": [1, 2"), std::runtime_error);
}

TEST(extract_answers_parse_into_text) {
    const std::string extract = Web::parse_extract(recorded("wiktionary_vast.json"));
    CHECK(extract.find("== English ==") != std::string::npos);
    CHECK(extract.find("=== Adjective ===") != std::string::npos);
    CHECK(Web::parse_extract("{\"query\":{\"pages\":{}}}").empty());
    // Escapes: a quote, a newline, a unicode character and a surrogate pair.
    const std::string escaped = Web::parse_extract(
        "{\"query\":{\"pages\":{\"1\":{\"extract\":\"a \\\"b\\\"\\nc \\u00e9 \\ud83d\\ude00\"}}}}");
    CHECK(escaped == "a \"b\"\nc \xC3\xA9 \xF0\x9F\x98\x80");
}

TEST(wiktionary_gives_parts_of_speech_as_categories) {
    const std::vector<Meaning> vast = Web::parse_wiktionary(Web::parse_extract(recorded("wiktionary_vast.json")));
    CHECK(vast.size() == 2);
    if (vast.size() == 2) {
        CHECK(vast[0].category == b("adjective"));
        CHECK(vast[0].definitions.size() == 3);
        CHECK(!vast[0].definitions.empty() &&
              vast[0].definitions.front() == "Very large or wide (literally or figuratively).");
        CHECK(vast[1].category == b("noun"));
        CHECK(vast[1].definitions == (std::vector<std::string>{"(poetic) A vast space."}));
    }
    // The Catalan and Dutch sections are not read; a level-4 heading under an etymology is.
    const std::vector<Meaning> sky = Web::parse_wiktionary(Web::parse_extract(recorded("wiktionary_sky.json")));
    CHECK(sky.size() == 2);
    if (sky.size() == 2) {
        CHECK(sky[0].category == b("noun"));
        CHECK(!sky[0].definitions.empty() && sky[0].definitions.front().starts_with("The atmosphere above a given point"));
        CHECK(sky[1].category == b("verb"));
    }
    CHECK(Web::parse_wiktionary("== Catalan ==\n=== Noun ===\nx\n\ny\n").empty());
    CHECK(Web::parse_wiktionary("").empty());
}

TEST(html_is_stripped_to_text) {
    CHECK(Web::strip_html("<p>The <b>sky</b> is blue.</p><script>x = 1 < 2;</script><p>Snow &amp; ice.</p>") ==
          "The sky is blue.\n\nSnow & ice.");
    CHECK(Web::strip_html("a&nbsp;b &#39;c&#x27; &eacute;") == "a b 'c' &eacute;");
    CHECK(Web::strip_html("<style>p {color: red}</style>  lots   of   space \n\n\n\n lines") == "lots of space\n\nlines");
    CHECK(Web::strip_html("").empty());
}

TEST(names_and_addresses) {
    CHECK(Web::slug("Sky Blue Sky") == "sky-blue-sky");
    CHECK(Web::slug("Sky (disambiguation)") == "sky-disambiguation");
    CHECK(Web::slug("  ") == "page");
    CHECK(Web::url_encode("New York") == "New%20York");
    CHECK(Web::url_encode("a&b=c") == "a%26b%3Dc");
    CHECK(Web::url_encode("sky") == "sky");
    CHECK(Web::is_url("https://example.org/x"));
    CHECK(!Web::is_url("Sky"));
    CHECK(!Web::user_agent().empty());
}

TEST(a_web_with_recorded_answers_searches_fetches_and_defines) {
    std::vector<std::string> asked;
    const Web::Transport transport = [&](const std::string& url) -> std::string {
        asked.push_back(url);
        if (url.find("list=search") != std::string::npos) {
            return recorded("search_sky.json");
        }
        if (url.find("wiktionary") != std::string::npos) {
            return recorded("wiktionary_vast.json");
        }
        if (url.find("titles=Sky") != std::string::npos) {
            return "{\"query\":{\"pages\":{\"195193\":{\"title\":\"Sky\",\"extract\":\"The sky is above.\\n\\nIt is blue.\"}}}}";
        }
        if (url.find("titles=Nothing") != std::string::npos) {
            return "{\"query\":{\"pages\":{\"-1\":{\"missing\":\"\"}}}}";
        }
        if (url.starts_with("https://example.org/")) {
            return "<html><body><h1>A page</h1><p>Water is wet.</p></body></html>";
        }
        throw std::runtime_error("no recorded answer for " + url);
    };
    const std::filesystem::path dir = scratch();
    const Web web{larry::Language::English, transport, dir};
    CHECK(web.directory() == dir);
    const std::vector<Hit> hits = web.search("sky", 5);
    CHECK(hits.size() == 5);
    CHECK(!asked.empty() && asked.back().find("srsearch=sky") != std::string::npos);
    CHECK(!asked.empty() && asked.back().find("srlimit=5") != std::string::npos);
    const std::vector<Meaning> vast = web.define("vast");
    CHECK(vast.size() == 2);
    CHECK(!asked.empty() && asked.back().find("en.wiktionary.org") != std::string::npos);
    CHECK(!asked.empty() && asked.back().find("titles=vast") != std::string::npos);
    // A fetched article is kept with its source and date, and reads back.
    const Page sky = web.fetch("Sky");
    CHECK(sky.title == "Sky");
    CHECK(sky.source == "https://en.wikipedia.org/wiki/Sky");
    CHECK(sky.text == "The sky is above.\n\nIt is blue.");
    CHECK(sky.file == dir / "sky.txt");
    CHECK(std::filesystem::exists(sky.file));
    const std::optional<Page> again = Web::read_page(sky.file);
    CHECK(again.has_value());
    if (again) {
        CHECK(again->title == "Sky");
        CHECK(again->source == sky.source);
        CHECK(again->text == sky.text);
    }
    CHECK(!Web::read_page(dir / "missing.txt").has_value());
    // Any URL: the page's text, with the tags stripped.
    const Page page = web.fetch("https://example.org/water.html");
    CHECK(page.text == "A page\nWater is wet.");
    CHECK(page.title == "water.html");
    CHECK(page.file == dir / "water-html.txt");
    CHECK(web.text_of("https://example.org/water.html") == "A page\nWater is wet.");
    // A page that is not there is an error, not an empty file.
    CHECK_THROWS(web.fetch("Nothing"), std::runtime_error);
    CHECK(!std::filesystem::exists(dir / "nothing.txt"));
}

TEST(the_live_web_when_asked_for) {
    const char* live = std::getenv("LARRY_TEST_WEB");
    if (live == nullptr || std::string_view{live} != "1") {
        std::println("live web checks skipped: set LARRY_TEST_WEB=1");
        return;
    }
    const Web web{larry::Language::English, {}, scratch()};
    const std::vector<Hit> hits = web.search("sky", 3);
    CHECK(!hits.empty());
    const std::vector<Meaning> vast = web.define("vast");
    CHECK(!vast.empty());
}

int main() {
    return larry::test::run();
}
