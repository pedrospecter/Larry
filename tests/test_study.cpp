// Tests for Study: content to proposed facts, words and a lesson draft (W4),
// on the fixture article, without the network.

#include "larry/study.hpp"

#include "larry/assimilation.hpp"
#include "larry/atom_operations.hpp"
#include "larry/base_rules.hpp"
#include "larry/brain.hpp"
#include "larry/content.hpp"
#include "larry/description.hpp"
#include "larry/grammar.hpp"
#include "larry/lesson.hpp"
#include "larry/memory.hpp"
#include "larry/web.hpp"

#include "check.hpp"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <print>
#include <string>
#include <string_view>
#include <vector>

using larry::Bytes;
using larry::Study;
using larry::StudyReport;

namespace {

const larry::BaseRules& rules() {
    static const larry::BaseRules instance{larry::Language::English};
    return instance;
}

std::string contents(const std::filesystem::path& file) {
    std::ifstream in{file, std::ios::binary};
    return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

}  // namespace

TEST(an_article_studies_into_facts_words_and_a_draft) {
    const std::filesystem::path file = std::filesystem::temp_directory_path() / "larry_test_study.atoms";
    std::filesystem::remove(file);
    larry::Memory cache{file};
    larry::Grammar grammar{rules()};
    larry::Brain brain{rules(), cache, nullptr, nullptr, &grammar};
    const larry::Assimilation assimilation{rules(), nullptr, &grammar};
    const larry::AtomOperations ops;
    const auto teach = [&](std::string_view text, std::vector<std::string_view> categories) {
        std::vector<Bytes> taught;
        for (const std::string_view c : categories) {
            taught.emplace_back(c.begin(), c.end());
        }
        const larry::Description d = assimilation.describe(ops.from_text(text), &cache, taught);
        cache.store(d.atom, d.metadata);
    };
    // The words of the article, but not all: "mammals" and "Old English" stay unknown.
    teach("The sea is deep.", {"determiner", "noun", "auxiliary verb", "adjective"});
    teach("The sky is blue.", {"determiner", "noun", "auxiliary verb", "adjective"});
    teach("The sea is the body of water that covers most of the Earth.",
          {"determiner", "noun", "auxiliary verb", "determiner", "noun", "preposition", "noun", "pronoun", "verb", "determiner", "preposition", "determiner", "proper noun"});
    teach("It is salty.", {"pronoun", "auxiliary verb", "adjective"});
    teach("The word comes from Paris.", {"determiner", "noun", "verb", "preposition", "proper noun"});
    teach("Fish live in the sea.", {"noun", "verb", "preposition", "determiner", "noun"});
    teach("Birds fly over the sea.", {"noun", "verb", "preposition", "determiner", "noun"});
    teach("Whales are big.", {"noun", "auxiliary verb", "adjective"});
    teach("Many people think so.", {"determiner", "noun", "verb", "adverb"});
    teach("I love the sea.", {"pronoun", "verb", "determiner", "noun"});
    teach("Is the sea blue?", {"auxiliary verb", "determiner", "noun", "adjective"});

    const std::filesystem::path article = std::filesystem::path{LARRY_TEST_DATA_DIR} / "en" / "article.txt";
    const std::optional<larry::Page> page = larry::Web::read_page(article);
    CHECK(page.has_value());
    if (!page) {
        return;
    }
    const std::filesystem::path drafts = std::filesystem::temp_directory_path() / "larry_test_study_drafts";
    std::filesystem::remove_all(drafts);
    const larry::Content content{rules()};
    const Study study{rules(), content};
    const std::int64_t before = cache.count();
    const StudyReport report = study.study(page->text, page->title, page->source, brain, drafts);
    for (const std::string& line : report.lines()) {
        std::println(stderr, "  {}", line);
    }
    CHECK(report.title == "Sea");
    CHECK(report.source == "https://en.wikipedia.org/wiki/Sea");
    CHECK(report.classes.total() == 20);
    CHECK(report.classes.facts == 7);
    CHECK(report.facts.size() == 7);
    CHECK(std::ranges::contains(report.facts, std::string{"Whales are mammals."}));
    CHECK(std::ranges::contains(report.facts, std::string{"The word comes from Old English."}));
    // The facts are proposed conceptions now, with the source of the study;
    // those taught already are known.
    CHECK(report.proposed + report.known == 7);
    CHECK(report.known >= 4);
    CHECK(cache.count() == before + static_cast<std::int64_t>(report.proposed));
    const std::optional<larry::StoredAtom> whales =
        cache.find(assimilation.describe(ops.from_text("Whales are mammals."), &cache).metadata);
    CHECK(whales.has_value());
    if (whales) {
        CHECK(whales->status == larry::Status::Proposed);
        CHECK(whales->sources == (std::vector<std::string>{"study:sea"}));
    }
    // The words to learn, most used first, with where their category came
    // from: the context guessed them (A6), and the guess for "mammals" is
    // wrong, which is what the draft is for.
    CHECK(report.words.size() == 2);
    std::vector<std::string> words;
    for (const larry::WordToLearn& w : report.words) {
        words.push_back(w.word + " " + w.category + " " + w.from);
    }
    CHECK(std::ranges::contains(words, std::string{"mammals adjective guessed from context"}));
    CHECK(std::ranges::contains(words, std::string{"old english proper noun guessed from context"}));
    // The draft, in the lesson format, reads as lessons once it is corrected.
    CHECK(report.draft == drafts / "sea.txt");
    CHECK(std::filesystem::exists(report.draft));
    const std::string draft = contents(report.draft);
    CHECK(draft.starts_with("# Draft lesson from \"Sea\" (https://en.wikipedia.org/wiki/Sea), studied "));
    CHECK(draft.find("# Words to learn (2, most used first): mammals (adjective, guessed from context), old english (proper noun, guessed from context)") != std::string::npos);
    CHECK(draft.find("# check: mammals (adjective, guessed from context)\nWhales are mammals.\nnoun, auxiliary verb, adjective\n") != std::string::npos);
    CHECK(draft.find("\nThe sea is deep.\ndeterminer, noun, auxiliary verb, adjective\n") != std::string::npos);
    std::string corrected = draft;
    const std::string wrong = "Whales are mammals.\nnoun, auxiliary verb, adjective\n";
    const std::size_t at = corrected.find(wrong);
    CHECK(at != std::string::npos);
    if (at != std::string::npos) {
        corrected.replace(at, wrong.size(), "Whales are mammals.\nnoun, auxiliary verb, noun\n");
    }
    const std::filesystem::path fixed = drafts / "sea_corrected.txt";
    {
        std::ofstream out{fixed, std::ios::binary};
        out << corrected;
    }
    const std::vector<larry::Lesson> lessons = larry::read_lessons(fixed);
    CHECK(lessons.size() == 7);
    CHECK(std::ranges::any_of(lessons, [](const larry::Lesson& l) { return l.sentence == "Whales are mammals."; }));
    // The report's lines.
    const std::vector<std::string> lines = report.lines();
    CHECK(lines.size() == 4);
    CHECK(!lines.empty() && lines.front().starts_with("Sea: 20 sentences: 7 facts"));
    CHECK(lines.size() > 2 && lines[2].starts_with("words to learn: 2: "));
    // Nothing to study leaves no draft.
    const StudyReport empty = study.study("== Notes ==\n\nRetrieved 2024.", "Notes", "", brain, drafts);
    CHECK(empty.classes.facts == 0);
    CHECK(empty.draft.empty());
    CHECK(empty.lines().size() == 3);
}

int main() {
    return larry::test::run();
}
