// Tests for lessons: the lesson file reader and the lessons in the repository.

#include "larry/lesson.hpp"

#include "larry/assimilation.hpp"
#include "larry/atom_operations.hpp"
#include "larry/base_rules.hpp"
#include "larry/cognition.hpp"

#include "check.hpp"

#include <filesystem>
#include <fstream>
#include <map>
#include <print>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using larry::Bytes;
using larry::Lesson;

namespace {

Bytes b(std::string_view text) {
    return Bytes(text.begin(), text.end());
}

std::filesystem::path write(std::string_view name, std::string_view content) {
    const std::filesystem::path file = std::filesystem::temp_directory_path() / name;
    std::ofstream out{file, std::ios::binary};
    out << content;
    return file;
}

}  // namespace

TEST(reads_lessons) {
    const std::filesystem::path file =
        write("larry_lessons_ok.txt", "# A comment\n\nThe sky is blue.\ndeterminer, noun, auxiliary verb, adjective\n\n\n"
                                      "  Hello.  \r\n interjection \r\n");
    const std::vector<Lesson> lessons = larry::read_lessons(file);
    CHECK(lessons.size() == 2);
    CHECK(lessons.size() == 2 && lessons[0].sentence == "The sky is blue.");
    CHECK(lessons.size() == 2 && lessons[0].line == 3);
    CHECK(lessons.size() == 2 && lessons[0].categories.size() == 4);
    CHECK(lessons.size() == 2 && lessons[0].categories[2] == b("auxiliary verb"));
    CHECK(lessons.size() == 2 && lessons[1].sentence == "Hello.");
    CHECK(lessons.size() == 2 && lessons[1].line == 7);
    CHECK(lessons.size() == 2 && lessons[1].categories == std::vector<Bytes>{b("interjection")});
    CHECK(larry::read_lessons(write("larry_lessons_empty.txt", "# nothing\n\n")).empty());
}

TEST(rejects_a_sentence_without_categories) {
    CHECK_THROWS(larry::read_lessons(write("larry_lessons_no_cats.txt", "The sky is blue.\n")),
                 std::runtime_error);
    CHECK_THROWS(larry::read_lessons(write("larry_lessons_blank.txt", "The sky is blue.\n\ndeterminer\n")),
                 std::runtime_error);
    CHECK_THROWS(larry::read_lessons(write("larry_lessons_empty_cat.txt", "The sky.\ndeterminer,\n")),
                 std::runtime_error);
    CHECK_THROWS(larry::read_lessons(std::filesystem::temp_directory_path() / "larry_no_lessons.txt"),
                 std::runtime_error);
}

TEST(lesson_files_come_in_name_order) {
    const std::vector<std::filesystem::path> files = larry::lesson_files(larry::Language::English);
    CHECK(!files.empty());
    for (std::size_t i = 1; i < files.size(); ++i) {
        CHECK(files[i - 1] < files[i]);
    }
    for (const std::filesystem::path& file : files) {
        CHECK(file.extension() == ".txt");
        CHECK(file.parent_path().filename() == "en");
    }
}

TEST(every_lesson_in_the_repository_is_sound) {
    // Each lesson has one category per entity, every category is in the base
    // rules, and no word is taught with two categories: the first lessons
    // must let Larry describe new sentences without being told.
    const larry::BaseRules rules{larry::Language::English};
    const larry::Assimilation assimilation{rules};
    const larry::AtomOperations ops;
    const larry::Cognition cognition;
    std::map<Bytes, Bytes> seen;
    std::size_t lessons = 0;
    std::size_t failed = 0;
    for (const std::filesystem::path& file : larry::lesson_files(larry::Language::English)) {
        for (const Lesson& lesson : larry::read_lessons(file)) {
            ++lessons;
            larry::EntitiesElectron entities = assimilation.entities(ops.from_text(lesson.sentence));
            try {
                cognition.categorize(entities, lesson.categories, rules);
            } catch (const std::exception& e) {
                ++failed;
                std::println(stderr, "{} line {}: {}", file.string(), lesson.line, e.what());
                continue;
            }
            for (const larry::Entity& entity : entities.entities) {
                const Bytes word = ops.fold(entity.word);
                const auto [it, inserted] = seen.emplace(word, entity.category);
                if (!inserted && it->second != entity.category) {
                    ++failed;
                    std::println(stderr, "{} line {}: \"{}\" was taught as {} and now as {}",
                                 file.string(), lesson.line,
                                 std::string(entity.word.begin(), entity.word.end()),
                                 std::string(it->second.begin(), it->second.end()),
                                 std::string(entity.category.begin(), entity.category.end()));
                }
            }
        }
    }
    CHECK(lessons >= 50);
    CHECK(failed == 0);
}

TEST(lessons_read_from_a_text_as_from_a_file) {
    // N2f: a lesson in the cloud is a text; the same reader, named in its errors.
    const std::vector<larry::Lesson> lessons =
        larry::read_lessons_text("# a comment\nThe sky is blue.\ndeterminer, noun, auxiliary verb, adjective\n\nBirds fly.\nnoun, verb\n", "cloud:test");
    CHECK(lessons.size() == 2);
    CHECK(lessons.size() == 2 && lessons[0].sentence == "The sky is blue." && lessons[0].categories.size() == 4);
    CHECK(lessons.size() == 2 && lessons[1].sentence == "Birds fly." && lessons[1].line == 5);
    CHECK_THROWS(larry::read_lessons_text("The sky is blue.\n", "cloud:test"), std::runtime_error);
}

int main() {
    return larry::test::run();
}
