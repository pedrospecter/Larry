#include "larry/assimilation.hpp"
#include "larry/atom_operations.hpp"
#include "larry/base_rules.hpp"
#include "larry/babi.hpp"
#include "larry/bench.hpp"
#include "larry/measure.hpp"
#include "larry/brain.hpp"
#include "larry/cognition.hpp"
#include "larry/constellation.hpp"
#include "larry/content.hpp"
#include "larry/description.hpp"
#include "larry/dictionary.hpp"
#include "larry/electron.hpp"
#include "larry/grammar.hpp"
#include "larry/harness.hpp"
#include "larry/lesson.hpp"
#include "larry/memory.hpp"
#include "larry/study.hpp"
#include "larry/tolerance.hpp"
#include "larry/advisor.hpp"
#include "larry/code.hpp"
#include "larry/web.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <limits>
#include <memory>
#include <optional>
#include <print>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <unistd.h>

namespace {

constexpr std::string_view usage = R"(usage: larry <command> [arguments]

  show <text>...               describe a sentence without storing it
  tell <text> [category ...]   describe a sentence and store it as a
                               conception; with the category of each entity,
                               in order, it is taught
  read <file>                  tell every fact in a text file: the sentences that
                               are conceptions become proposed ones; questions,
                               headings, references and the rest are listed
  answer <sentence> <word> <category>
                               A5: answer a question read left: the word's
                               category in that sentence, taught and stored
  classify <file or text>      what each sentence of content is: a fact, context,
                               a question, an instruction, speech, a heading, a
                               reference or a fragment, and why
  lessons [push | pull <name> [dir] | show <name>]
                               N2f: the lesson files here and the lessons in the
                               cloud (drafts of study, taught ones); push puts
                               the files in the cloud, pull writes one out to
                               correct it, show prints it
  teach <file or name>                 tell every lesson in a lesson file: a sentence,
                               then the category of each entity, in order
  rebuild                      empty memory and teach every lesson in
                               lessons/<locale>/, in name order
  compare <text> <text>        compare two sentences: C1 to C5
  groups <text>                A8: the word each word attaches to, by its group
  themes                       T1: the index of what Larry holds, by theme
                               (themes.txt and the kinds of A10)
  theme <name>                 T1: the conceptions about one theme
  understand <text>            P1: what a sentence is (a command, a question,
                               ...), what it names, and what Larry holds of it
  goal <order>                 S1: the state the order would make, whether it
                               is so already, and the plan to reach it (S2)
  explain <observation>        R6: the assumption that would explain it, from
                               the rules Larry holds ("If it rains, ...", R9)
  translate <text> <locale>    G7: the sentence said in the other constellation
                               (en, pt), word by word on its image
  consult <claim or question>  W5: Larry's answer, then a language model's
                               second opinion; nothing stored (the key comes
                               from ANTHROPIC_API_KEY in .env; the model from
                               LARRY_LLM_MODEL, claude-opus-5-5 by default)
  extract <file or text> [n]   W5: the facts the model reads in the text, at
                               most n (20), each heard as a proposal from the
                               source llm:<model>, for you to validate
  code <file> [--store]        the definitions, uses and calls of a source
                               file (C++, C#, JavaScript, Python) as sentences,
                               by base_rules/code/<language>.txt; --store hears
                               them as proposals from code:<file>
  grammar <text>               which grammar pattern each sentence fits and the
                               role of each word, or where it breaks and what
                               was expected there; "grammar" alone lists the
                               patterns
  harness <text>               the context harness: for each relation of a
                               sentence (an attribute of a thing, the subject
                               or object of a verb), whether the conceptions
                               know it, find it plausible or never saw it, and
                               what they know instead
  recognize <text>             what each sentence is (a statement, a question,
                               a request, an assumption, an expression), its
                               emotion (sarcasm, joy, anger, ...), the command
                               it is, the calculation it asks, and its content
                               class, each with the reason
  qualify <text>               what kind of sentence each one is: an
                               affirmation (a declaration), a question, an
                               order (a command), an assumption or an
                               expression, by the rules and by the conceptions
                               of the same structure, with the reasons
  ask <text>                   answer a question, or judge a claim, and store
                               nothing: a question gets yes, no, the conception
                               that answers it or "I don't know"; a claim gets
                               true, false or I don't know; a calculation its
                               result ("What is two plus three?"); with the
                               reasons, the reading of a sentence off the
                               grammar, and what is unusual in it
  say <text>                   hear one sentence and reply: an affirmation is
                               stored, a question answered, an order done when
                               it is a command Larry knows (see
                               base_rules/<locale>/commands.txt: "search for",
                               "define", "tell me about", "compare", ...), an
                               assumption noted, an expression returned
  chat [name]                  hear a line at a time from standard input; with
                               a name, the person is "chat:<name>", no
                               validator: what goes against memory is denied,
                               what is new waits for the validator;
                               "why?" explains the last reply, "bye" ends; the
                               reply streams word by word on a terminal
                               (LARRY_STREAM=0 prints it at once), and a line
                               in brackets says what Larry is doing meanwhile
  sync [n]                     push the cache's conceptions to the cloud and
                               pull the cloud's n most recent (100) into it
  validate                     go through the proposed conceptions one by one:
                               y validates, n withdraws, s skips, q stops
  validate list                list the proposed conceptions with their ids
  validate accept <id>         validate one; validate reject <id> withdraws it
  validators                   who may validate; validators add <name> adds one
                               (anyone adds the first, then only a validator)
  words [word]                 the vocabulary: every word with its categories
                               and uses, or one word with its types and contexts
  search <words>               ask Wikipedia for the pages about these words:
                               titles and snippets
  fetch <title or url>         keep a Wikipedia article, or any web page, as
                               plain text under content/<locale>/, with its
                               source and date
  define <word>                ask Wiktionary what the word is: its parts of
                               speech as categories, with the first meanings,
                               beside what memory and the dictionary say
  study <file, title or url>   fetch the content when it is not a file, classify
                               its sentences, propose the facts as conceptions,
                               gather the words to learn, and write a lesson
                               draft in lessons/<locale>/drafts/ for you to
                               correct and teach
  molecules                    the molecules (N4): each text read and each
                               conversation, with its conceptions in order
  molecule <name>              one molecule: its conceptions in order, who said
                               each and when
  plan <goal>                  a plan for a goal (S2) from the actions Larry was
                               told ("To open the door, turn the key."): the
                               steps in order, the goal last
  think [seconds]              what Larry does with no input (S7): finds the
                               conflicts among its conceptions, proposes
                               general atoms from examples as assumptions
                               (R5), and lists what waits for you (S3)
  attention                    what Larry would think about (S3): the words to
                               ask about, the conflicts to settle, the
                               proposals waiting
  know <word>                  what Larry knows about a subject (S4): its
                               conceptions by status, the categories it was
                               taught or seen with, its guessed uses, its
                               conflicts and bonds, and what it cannot answer
  forms <word>                 what the word is a form of (A4): its base, the
                               category and feature its ending or its irregular
                               pair gives, the rule, and its "form of" bonds
  near <word or sentence>      the neighbours in memory (N5), nearest first:
                               the conceptions that share its words, the words
                               and conceptions it is bonded to, two steps out;
                               "near <text> <steps> <limit>" changes the reach
                               way, or all of them: kind, both ends, origins
  bond <from> <kind> <to>      record a bond between two ends: a word, or a
                               sentence Larry holds as a conception
  count                        how many conceptions, word uses, bonds and
                               molecules memory holds
  measure [n ...]              A6: the accuracy of Larry's categories on the
                               Universal Dependencies test set of the language
                               (English EWT, Portuguese Bosque), taught n
                               training sentences (100 300 1000 3000 all), from
                               memory alone and with the dictionary; the
                               treebank comes from scripts/ud.sh [en|pt]
  babi [task]                  the bAbI tasks (Weston and others, 2015): each
                               story heard, its questions answered and judged;
                               one task, or every task scripts/babi.sh fetched
  bench [n]                    measure Larry with n generated atoms (10000) in a
                               scratch file: sentences stored and described per
                               second, lookups per second, the time to answer,
                               the start-up time, the bytes per atom

LARRY_LANGUAGE picks the constellation: en (default) or pt (A12).
Memory, the cache on this machine, is the file LARRY_MEMORY names, or
memory/<locale>.atoms. When the file does not exist yet, Larry rebuilds it
from the lessons first. The cloud, the record of conceptions, is the
PostgreSQL server LARRY_DB names, in libpq's form ("host=... dbname=larry
user=..."), as a URI, or as Azure shows it ("Host=...;Database={0}");
without it, Larry has only the cache. LARRY_USER names who is talking
(default: the login name); only a validator decides what is true. Larry
reads these from ./.env and from the repository's .env too, so they can
stay there, out of git.
)";

std::string_view as_text(const larry::Bytes& b) {
    return {reinterpret_cast<const char*>(b.data()), b.size()};
}

std::string_view name(larry::Language language) {
    switch (language) {
    case larry::Language::English:
        return "English";
    case larry::Language::Portuguese:
        return "Portuguese";
    }
    return "unknown";
}

std::string_view stored_name(larry::Stored stored) {
    switch (stored) {
    case larry::Stored::New:
        return "new";
    case larry::Stored::Same:
        return "same";
    case larry::Stored::SameForm:
        return "same form";
    }
    return "";
}

struct Tally {
    std::size_t sentences = 0;
    std::size_t stored = 0;
    std::size_t same = 0;
    std::size_t same_form = 0;

    void add(larry::Stored stored_as) {
        ++sentences;
        switch (stored_as) {
        case larry::Stored::New:
            ++stored;
            break;
        case larry::Stored::Same:
            ++same;
            break;
        case larry::Stored::SameForm:
            ++same_form;
            break;
        }
    }

    void add(const Tally& other) {
        sentences += other.sentences;
        stored += other.stored;
        same += other.same;
        same_form += other.same_form;
    }

    void print() const {
        std::println("{} sentences: {} stored, {} already there, {} in the same form", sentences,
                     stored, same, same_form);
    }
};

/// Reads a .env file: KEY=VALUE lines, '#' comments, an optional "export",
/// quotes around a value allowed. A variable already in the environment
/// wins. Larry reads ./.env and then the repository's .env, so the cloud
/// and the user's name are always there without an export.
void read_env_file(const std::filesystem::path& file) {
    std::ifstream in{file};
    if (!in) {
        return;
    }
    for (std::string line; std::getline(in, line);) {
        const auto trim = [](std::string_view s) {
            while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) {
                s.remove_prefix(1);
            }
            while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) {
                s.remove_suffix(1);
            }
            return s;
        };
        std::string_view text = trim(line);
        if (text.empty() || text.front() == '#') {
            continue;
        }
        if (text.starts_with("export ")) {
            text = trim(text.substr(7));
        }
        const std::size_t equals = text.find('=');
        if (equals == std::string_view::npos) {
            continue;
        }
        const std::string key{trim(text.substr(0, equals))};
        std::string_view value = trim(text.substr(equals + 1));
        if (value.size() >= 2 && (value.front() == '"' || value.front() == '\'') &&
            value.back() == value.front()) {
            value = value.substr(1, value.size() - 2);
        }
        const bool valid_key = !key.empty() && std::ranges::all_of(key, [](char c) {
            return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                   c == '_';
        });
        if (valid_key && std::getenv(key.c_str()) == nullptr) {
            setenv(key.c_str(), std::string{value}.c_str(), 0);
        }
    }
}

/// Who is talking: LARRY_USER, or the login name.
std::string user_name() {
    for (const char* variable : {"LARRY_USER", "USER", "LOGNAME"}) {
        const char* value = std::getenv(variable);
        if (value != nullptr && *value != '\0') {
            return value;
        }
    }
    return "user";
}

/// The dictionary of a language, when its file is there.
std::unique_ptr<larry::Dictionary> open_dictionary(larry::Language language) {
    const std::filesystem::path file = larry::Dictionary::file_for(language);
    if (!std::filesystem::exists(file)) {
        std::println(stderr, "larry: no dictionary at {}; run scripts/dictionary.sh", file.string());
        return nullptr;
    }
    return std::make_unique<larry::Dictionary>(file);
}

/// A12: the constellation from LARRY_LANGUAGE ("en", "pt"); English otherwise.
larry::Language language_from_environment() {
    const char* const code = std::getenv("LARRY_LANGUAGE");
    if (code != nullptr && *code != '\0') {
        if (const std::optional<larry::Language> language = larry::language_named(code)) {
            return *language;
        }
        std::println(stderr, "larry: no constellation \"{}\"; English it is", code);
    }
    return larry::Language::English;
}

std::string content_of(const std::filesystem::path& file);

struct Larry {
    std::string user{user_name()};
    larry::Constellation constellation{language_from_environment()};
    larry::BaseRules rules{constellation.language()};
    std::unique_ptr<larry::Dictionary> dictionary{open_dictionary(constellation.language())};
    larry::Grammar grammar{rules};
    larry::Assimilation assimilation{rules, dictionary.get(), &grammar};
    larry::Cognition cognition;
    larry::AtomOperations ops;
    larry::Memory memory{larry::Memory::file_from_environment(constellation.language())};
    std::unique_ptr<larry::Database> cloud;
    larry::Brain brain{rules, memory, nullptr, dictionary.get(), &grammar};
    /// Whom what is heard comes from: "user:<name>" (the user), or "chat:<name>" for a person in the chat.
    std::string source = "user:" + user;

    Larry() {
        const std::string connection = larry::Database::connection_from_environment();
        if (!connection.empty()) {
            try {
                cloud = larry::Database::open(connection);
            } catch (const std::exception& e) {
                std::println(stderr, "larry: no cloud: {}", e.what());
            }
        }
        brain = larry::Brain{rules, memory, cloud.get(), dictionary.get(), &grammar};
        if (!std::filesystem::exists(memory.file())) {
            std::println(stderr, "larry: no memory at {}; rebuilding it from the lessons",
                         memory.file().string());
            rebuild(false);
        }
        // A cloud with nothing in it gets what the cache holds: the first
        // contact of a machine that already learned. Otherwise the cloud is
        // the record: the cache takes its standing for what it holds (N2c).
        if (cloud && cloud->count() == 0 && memory.count() > 0) {
            const larry::Brain::Synced synced = brain.sync(0);
            std::println(stderr, "larry: the cloud was empty; pushed {} conceptions to it", synced.pushed);
        } else if (cloud) {
            try {
                if (const std::int64_t refreshed = brain.refresh(); refreshed > 0) {
                    std::println(stderr, "larry: the cloud changed the standing of {} cached conception{}",
                                 refreshed, refreshed == 1 ? "" : "s");
                }
            } catch (const std::exception& e) {
                std::println(stderr, "larry: the cloud did not answer for the standings: {}", e.what());
            }
        }
    }

    /// The locale of the constellation, as the cloud keys pages and lessons.
    [[nodiscard]] std::string locale() const { return std::string{larry::locale(constellation.language())}; }

    /// Stores every lesson of a text in the lesson format, named as its
    /// source says ("lesson:<name>").
    Tally teach_text(std::string_view text, const std::string& name, bool verbose) {
        Tally tally;
        for (const larry::Lesson& lesson : larry::read_lessons_text(text, name)) {
            larry::Description d;
            try {
                d = assimilation.describe(ops.from_text(lesson.sentence), &memory, lesson.categories);
            } catch (const std::invalid_argument& e) {
                throw std::runtime_error(std::format("{} line {}: {}", name, lesson.line, e.what()));
            }
            const larry::Stored stored = brain.remember(d, larry::Status::Proposed, "lesson:" + name);
            tally.add(stored);
            if (verbose) {
                std::println("{:<10} {}", stored_name(stored), lesson.sentence);
            }
        }
        return tally;
    }

    /// Stores every lesson in a lesson file; with a cloud, the file goes
    /// there too, as a taught lesson under its name (N2f): the cloud is the
    /// record of the lessons as of the conceptions.
    Tally teach(const std::filesystem::path& file, bool verbose) {
        const std::string text = content_of(file);
        const Tally tally = teach_text(text, file.filename().string(), verbose);
        if (cloud) {
            try {
                cloud->store_lesson(locale(), file.stem().string(), text, "taught");
            } catch (const std::exception& e) {
                std::println(stderr, "larry: the cloud did not keep the lesson {}: {}", file.stem().string(), e.what());
            }
        }
        return tally;
    }

    /// Teaches a lesson the cloud holds, by name, and marks it taught.
    Tally teach_from_cloud(const larry::Database::CloudLesson& lesson, bool verbose) {
        const Tally tally = teach_text(lesson.text, lesson.name + ".txt", verbose);
        cloud->set_lesson_status(locale(), lesson.name, "taught");
        return tally;
    }

    /// Empties memory and teaches every lesson file, in name order (F7), then
    /// the taught lessons of the cloud that are no file here, in the order
    /// they were taught (N2f).
    void rebuild(bool verbose) {
        memory.clear();
        Tally total;
        const std::vector<std::filesystem::path> files =
            larry::lesson_files(constellation.language());
        std::vector<std::string> names;
        for (const std::filesystem::path& file : files) {
            names.push_back(file.stem().string());
            const Tally tally = teach(file, false);
            if (verbose) {
                std::println("{:<40} {} lessons, {} stored", file.filename().string(),
                             tally.sentences, tally.stored);
            }
            total.add(tally);
        }
        std::size_t from_cloud = 0;
        if (cloud) {
            try {
                for (const larry::Database::CloudLesson& lesson : cloud->lessons(locale())) {
                    if (lesson.status != "taught" || std::ranges::contains(names, lesson.name)) {
                        continue;
                    }
                    const Tally tally = teach_text(lesson.text, lesson.name + ".txt", false);
                    ++from_cloud;
                    if (verbose) {
                        std::println("{:<40} {} lessons, {} stored (cloud)", lesson.name, tally.sentences, tally.stored);
                    }
                    total.add(tally);
                }
            } catch (const std::exception& e) {
                std::println(stderr, "larry: the cloud did not give its lessons: {}", e.what());
            }
        }
        if (verbose) {
            std::println("{} lesson files, {} lessons from the cloud", files.size(), from_cloud);
            total.print();
            std::println("{} conceptions, {} word uses, in {}", memory.count(),
                         memory.count_words(), memory.file().string());
        }
    }
};

std::string source(const larry::EntityNote& note) {
    switch (note.source) {
    case larry::Source::Taught:
        return "taught";
    case larry::Source::Memory:
        return "memory";
    case larry::Source::Unknown: {
        std::string out = "unknown";
        for (const larry::Bytes& near : note.near) {
            out += out == "unknown" ? "; near: " : ", ";
            out += as_text(near);
        }
        return out;
    }
    case larry::Source::Open: {
        std::string out = "open:";
        for (const larry::Bytes& candidate : note.candidates) {
            out += ' ';
            out += as_text(candidate);
        }
        if (!note.context.empty()) {
            out += "; " + note.context + " picks " + std::string{as_text(note.candidates.front())};  // A6
        }
        return out;
    }
    case larry::Source::Guess: {
        if (!note.form.empty()) {
            return "guess from the form: " + note.form;  // A4
        }
        std::string out = "guess from the context:";
        for (const larry::Bytes& candidate : note.candidates) {
            out += ' ';
            out += as_text(candidate);
        }
        return out;
    }
    case larry::Source::Dictionary:
        return "dictionary";
    case larry::Source::Rule:
        return "base rules (a pronoun, a number word or a conjunction)";
    }
    return "";
}

void print(const Larry& larry, const larry::Description& d) {
    std::println("constellation : {}", name(larry.constellation.language()));
    std::println("base rules    : {} categories", larry.rules.categories().size());
    std::println("dictionary    : {} words", larry.dictionary ? larry.dictionary->size() : 0);
    std::println("memory        : {} conceptions", larry.memory.count());
    std::println("atom          : {} bits", d.atom.size());
    std::println("                {}", larry.ops.to_bits(d.atom));
    std::println("category      : {} ({} bytes)", as_text(d.category.bytes),
                 d.category.bytes.size());
    std::println("type          : {} ({} bytes)", as_text(d.type.bytes), d.type.bytes.size());
    std::println("pattern       : {}", d.pattern.empty() ? "none fits; roles by position" : d.pattern);
    const larry::Reading reading = larry.brain.read(d);
    std::string tolerance = reading.accepted ? (reading.changed ? std::format("read as \"{}\"", larry.ops.text(reading.meant.atom))
                                                                : std::string{"as said"})
                                             : "not read: " + reading.reason;
    for (const std::string& deviation : reading.deviations) {
        tolerance += "; " + deviation;
    }
    std::println("tolerance     : {} ({} deviation{}, {} counted, {} allowed)", tolerance,
                 reading.deviations.size(), reading.deviations.size() == 1 ? "" : "s", reading.counted,
                 reading.allowed);
    const larry::Report report = larry.brain.judge(reading.changed ? reading.meant : d);
    const std::vector<std::string> unusual = report.unusual();
    std::println("harness       : {} relation{}, {} unusual", report.judgements.size(),
                 report.judgements.size() == 1 ? "" : "s", unusual.size());
    for (const std::string& line : unusual) {
        std::println("                {}", line);
    }
    std::println("entities      : {}", d.entities.entities.size());
    for (std::size_t i = 0; i < d.entities.entities.size(); ++i) {
        const larry::Entity& entity = d.entities.entities[i];
        const std::string category =
            entity.category.empty() ? std::string{"?"} : std::string{as_text(entity.category)};
        const std::string from = i < d.notes.size() ? source(d.notes[i]) : std::string{};
        std::string types;
        for (const larry::Bytes& type : entity.types) {
            types += types.empty() ? "" : ", ";
            types += as_text(type);
        }
        std::println("  {:<14} {:<15} {:<36} {}", as_text(entity.word), category, types, from);
    }
    std::println("image         : {} bytes \"{}\"", d.image.bytes.size(), as_text(d.image.bytes));
    std::println("metadata      : {} bytes", d.metadata.bytes.size());
}

std::string join(std::span<const std::string_view> words) {
    std::string out;
    for (const std::string_view word : words) {
        if (!out.empty()) {
            out += ' ';
        }
        out += word;
    }
    return out;
}

std::vector<larry::Bytes> categories(std::span<const std::string_view> words) {
    std::vector<larry::Bytes> out;
    for (const std::string_view word : words) {
        out.emplace_back(word.begin(), word.end());
    }
    return out;
}

std::string read_file(const std::filesystem::path& file);

/// The text of a file: a page kept by larry fetch without its header lines,
/// or any text file as it is.
std::string content_of(const std::filesystem::path& file) {
    if (const std::optional<larry::Page> page = larry::Web::read_page(file); page && !page->source.empty()) {
        return page->text;
    }
    return read_file(file);
}

void print_classes(const larry::ContentTally& classes) {
    std::println("{} sentences: {} facts, {} context, {} questions, {} instructions, {} speech, {} headings, "
                 "{} references, {} fragments",
                 classes.total(), classes.facts, classes.context, classes.questions, classes.instructions,
                 classes.speech, classes.headings, classes.references, classes.fragments);
}

std::string read_file(const std::filesystem::path& file) {
    std::ifstream in{file, std::ios::binary};
    if (!in) {
        throw std::runtime_error(std::format("cannot read {}", file.string()));
    }
    return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

/// The words a description could not categorize, for a short report.
std::string open_words(const larry::Description& d) {
    std::string out;
    for (std::size_t i = 0; i < d.notes.size(); ++i) {
        if (d.notes[i].source == larry::Source::Unknown ||
            d.notes[i].source == larry::Source::Open ||
            d.notes[i].source == larry::Source::Guess) {
            out += out.empty() ? "  ?" : ",";
            out += ' ';
            out += as_text(d.entities.entities[i].word);
        }
    }
    return out;
}

/// G4a: whether replies stream word by word: on a terminal, unless
/// LARRY_STREAM is 0.
bool streaming() {
    const char* const setting = std::getenv("LARRY_STREAM");
    if (setting != nullptr && std::string_view{setting} == "0") {
        return false;
    }
    return isatty(fileno(stdout)) != 0;
}

/// G4a: prints a text word by word, with a short pause between words when
/// streaming, so that the reply shows as it is written.
void stream(std::string_view text) {
    if (!streaming()) {
        std::println("{}", text);
        return;
    }
    std::string word;
    const auto flush_word = [&] {
        std::print("{}", word);
        std::fflush(stdout);
        if (!word.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
        }
        word.clear();
    };
    for (const char c : text) {
        word.push_back(c);
        if (c == ' ' || c == '\n') {
            flush_word();
        }
    }
    flush_word();
    std::println("");
}

/// G4a: a notice while Larry works: "(searching the cloud for "sky"...)".
void show_notice(std::string_view what) {
    std::println("  ({}...)", what);
    std::fflush(stdout);
}

/// W4: studies a file, a Wikipedia title or a URL: the content is classified,
/// the facts proposed, the words gathered, and a lesson draft written.
larry::StudyReport study(Larry& larry, std::string_view what) {
    std::string text;
    std::string name;
    std::string source;
    if (std::filesystem::exists(what)) {
        const std::filesystem::path file{what};
        text = content_of(file);
        if (const std::optional<larry::Page> page = larry::Web::read_page(file); page && !page->source.empty()) {
            name = page->title;
            source = page->source;
        } else {
            name = file.stem().string();
            source = file.string();
        }
    } else if (std::optional<larry::Page> held = larry.cloud ? larry.cloud->page(larry.locale(), what) : std::nullopt) {
        // N2f: the cloud holds the page already.
        text = held->text;
        name = held->title;
        source = held->source;
    } else {
        const larry::Web web{larry.constellation.language()};
        show_notice("fetching \"" + std::string{what} + "\"");
        const larry::Page page = web.fetch(what, larry.cloud == nullptr);
        text = page.text;
        name = page.title;
        source = page.source;
        if (larry.cloud) {
            larry.cloud->store_page(larry.locale(), page);
            std::println(stderr, "larry: kept \"{}\" in the cloud as {}", page.title, larry::Web::slug(page.title));
        } else {
            std::println(stderr, "larry: kept \"{}\" in {}", page.title, page.file.string());
        }
    }
    const larry::Content content{larry.rules};
    const larry::Study study{larry.rules, content};
    // N2f: with a cloud the draft goes there, to pull, correct and teach; else to lessons/<locale>/drafts.
    larry::StudyReport report = study.study(text, name, source, larry.brain,
                                            larry.cloud ? std::filesystem::path{}
                                                        : larry::Study::drafts_directory(larry.constellation.language()));
    if (larry.cloud && !report.draft_text.empty()) {
        larry.cloud->store_lesson(larry.locale(), report.draft_name, report.draft_text, "draft");
        report.draft_in_cloud = report.draft_name;
    }
    return report;
}

/// W3: does a command and gives what it says, one line or several. The
/// web, the memory and the brain are the Larry struct's.
/// T1, P5: what Larry does when it holds nothing on a thing named to it: it
/// asks Wikipedia (W1), says what it found or that nothing was found (it may
/// not exist), and keeps "There is <thing>." as a proposal from the source,
/// with the research as a note, for the validator to judge.
std::string research(Larry& larry, const std::string& thing, const std::string& source) {
    std::string out;
    std::string note;
    try {
        const larry::Web web{larry.constellation.language()};
        show_notice("asking Wikipedia for \"" + thing + "\"");
        const std::vector<larry::Hit> hits = web.search(thing, 3);
        if (hits.empty()) {
            out = std::vformat(larry.brain.say("wikipedia nothing"), std::make_format_args(thing));
            note = "research:wikipedia:nothing";
        } else {
            std::string titles;
            for (const larry::Hit& hit : hits) {
                titles += (titles.empty() ? "" : "; ") + hit.title;
            }
            const std::string first = hits.front().title;
            out = std::vformat(larry.brain.say("wikipedia has"), std::make_format_args(titles, first));
            note = "research:wikipedia:" + first;
        }
    } catch (const std::exception& e) {
        out = std::format("I could not ask Wikipedia: {}", e.what());
        note = "research:none";
    }
    const std::string proposed = larry.brain.propose_thing(thing, source, note);
    out += " " + std::vformat(larry.brain.say("proposed"), std::make_format_args(proposed));
    return out;
}

std::string execute(Larry& larry, const larry::Command& command) {
    const auto argument = [&](std::size_t i) {
        return i < command.arguments.size() ? command.arguments[i] : std::string{};
    };
    // "the sea" is about the sea: leading determiners go.
    const auto thing = [&](std::string text) {
        for (const char* determiner : {"the ", "a ", "an ", "The ", "A ", "An "}) {
            if (text.starts_with(determiner)) {
                return text.substr(std::string_view{determiner}.size());
            }
        }
        return text;
    };
    const std::string& op = command.operation;
    std::string out;
    if (op == "cannot") {
        return {};  // denied with the reason already; nothing to run
    }
    if (op == "search") {
        const larry::Web web{larry.constellation.language()};
        show_notice("asking Wikipedia for \"" + argument(0) + "\"");
        const std::vector<larry::Hit> hits = web.search(argument(0));
        if (hits.empty()) {
            return "nothing found for \"" + argument(0) + "\".";
        }
        for (const larry::Hit& hit : hits) {
            out += std::format("{}{}: {}", out.empty() ? "" : "\n", hit.title, hit.snippet);
        }
        return out;
    }
    if (op == "define") {
        const larry::Web web{larry.constellation.language()};
        const std::string word = thing(argument(0));
        show_notice("asking Wiktionary for \"" + word + "\"");
        const std::vector<larry::Meaning> meanings = web.define(word);
        if (meanings.empty()) {
            return "Wiktionary has no English entry for \"" + word + "\".";
        }
        for (const larry::Meaning& m : meanings) {
            out += std::format("{}{}: {}", out.empty() ? "" : "\n", as_text(m.category),
                               m.definitions.empty() ? "" : m.definitions.front());
        }
        return out;
    }
    if (op == "fetch") {
        const larry::Web web{larry.constellation.language()};
        show_notice("fetching \"" + argument(0) + "\"");
        const larry::Page page = web.fetch(argument(0));
        return std::format("kept \"{}\" in {}: {} bytes.", page.title, page.file.string(), page.text.size());
    }
    if (op == "read") {
        const std::filesystem::path file = argument(0);
        if (!std::filesystem::exists(file)) {
            return "there is no file " + file.string() + ".";
        }
        const larry::Content content{larry.rules};
        larry::ContentTally classes;
        std::size_t stored = 0;
        const std::string source = "read:" + file.filename().string();
        for (const larry::Piece& piece : content.classify(content_of(file), larry.brain)) {
            classes.add(piece.what);
            if (piece.what == larry::ContentClass::Fact &&
                larry.brain.remember(piece.description, larry::Status::Proposed, source) == larry::Stored::New) {
                ++stored;
            }
        }
        return std::format("read {}: {} sentences, {} facts, {} new conceptions proposed.", file.string(),
                           classes.total(), classes.facts, stored);
    }
    if (op == "study") {
        const larry::StudyReport report = study(larry, argument(0));
        for (const std::string& line : report.lines()) {
            out += (out.empty() ? "" : "\n") + line;
        }
        return out;
    }
    if (op == "confirm") {
        // The claim, or the latest affirmation heard in this conversation
        // ("confirm this"), judged against what Larry holds; nothing stored.
        std::string claim = argument(0);
        std::string folded = claim;
        for (char& c : folded) {
            c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
        }
        if (folded == "this" || folded == "that" || folded == "it" || folded.starts_with("this ") || folded.starts_with("that ")) {
            claim.clear();
            for (const larry::StoredAtom& atom : larry.memory.recent(20)) {
                if (as_text(atom.description.category.bytes) == "affirmation" && atom.status != larry::Status::Withdrawn &&
                    std::ranges::any_of(atom.sources, [&](const std::string& src) { return src.starts_with("user:") || src.starts_with("chat:"); })) {
                    claim = std::string{larry.ops.text(atom.description.atom)};
                    break;
                }
            }
            if (claim.empty()) {
                return "I have nothing to confirm: tell me the claim.";
            }
        }
        const larry::Reply judged = larry.brain.answer(larry.ops.from_text(claim));
        out = std::format("\"{}\": {}", claim, judged.text);
        for (const std::string& because : judged.because) {
            out += "\n  because: " + because;
        }
        return out;
    }
    if (op == "about") {
        const std::string word = thing(argument(0));
        const larry::Bytes folded = larry.ops.fold(larry::Bytes(word.begin(), word.end()));
        std::vector<larry::StoredAtom> found = larry.memory.containing(folded);
        if (found.empty() && larry.cloud) {
            found = larry.cloud->containing(folded);
        }
        std::size_t shown = 0;
        for (const larry::StoredAtom& atom : found) {
            if (atom.status == larry::Status::Withdrawn || as_text(atom.description.category.bytes) != "affirmation") {
                continue;
            }
            out += std::format("{}{}", out.empty() ? "" : " ", larry.brain.restate(atom));  // G6: said again (G1)
            if (++shown == 5) {
                break;
            }
        }
        if (!out.empty()) {
            return out;
        }
        // T1: where to search when Larry does not know: the theme, then the web.
        const larry::Description about = larry.assimilation.describe(larry.ops.from_text(word), &larry.memory);
        const larry::Bytes theme = larry.brain.theme_of(about);
        out = std::vformat(larry.brain.say("no knowledge"), std::make_format_args(word));
        if (!theme.empty()) {
            const std::string theme_name(theme.begin(), theme.end());
            const std::size_t held = larry.brain.about_theme(theme).size();
            out += " " + std::vformat(larry.brain.say("on theme"), std::make_format_args(theme_name, held));
        }
        return out + " " + research(larry, word, larry.source);
    }
    if (op == "show") {
        const larry::Description d = larry.assimilation.describe(larry.ops.from_text(argument(0)), &larry.memory);
        out = std::format("{} ({})", as_text(d.category.bytes), d.pattern.empty() ? "no pattern fits" : d.pattern);
        for (const larry::Entity& e : d.entities.entities) {
            out += std::format("\n  {} {}{}", as_text(e.word), e.category.empty() ? "?" : as_text(e.category),
                               e.types.empty() ? "" : ", " + std::string{as_text(e.types.back())});
        }
        return out;
    }
    if (op == "compare") {
        const larry::Description a = larry.assimilation.describe(larry.ops.from_text(argument(0)), &larry.memory);
        const larry::Description b = larry.assimilation.describe(larry.ops.from_text(argument(1)), &larry.memory);
        const auto yes = [](bool holds) { return holds ? "yes" : "no"; };
        return std::format("identity {}, same form {}, aligned {}, different {}, same structure {}, same meaning {}.",
                           yes(larry.cognition.identity(a, b).holds), yes(larry.cognition.same_form(a, b).holds),
                           yes(larry.cognition.align(a, b).holds), yes(larry.cognition.difference(a, b).holds),
                           yes(larry.cognition.same_structure(a, b).holds), yes(larry.cognition.same_meaning(a, b).holds));
    }
    if (op == "count") {
        return std::format("{} conceptions and {} word uses here{}.", larry.memory.count(), larry.memory.count_words(),
                           larry.cloud ? std::format(", {} conceptions in the cloud", larry.cloud->count()) : "");
    }
    if (op == "grammar") {
        for (const larry::Grammar::Pattern& p : larry.grammar.patterns()) {
            out += (out.empty() ? "" : ", ") + p.name;
        }
        return std::format("{} patterns: {}.", larry.grammar.size(), out);
    }
    if (op == "withdraw") {
        if (!larry.brain.is_validator(larry.user)) {
            return "only a validator withdraws a conception, and \"" + larry.user + "\" is not one.";
        }
        const larry::Description d = larry.assimilation.describe(larry.ops.from_text(argument(0)), &larry.memory);
        if (!larry.brain.decide(d.metadata, larry::Status::Withdrawn, larry.user)) {
            return "I hold no conception \"" + argument(0) + "\".";
        }
        return "withdrawn: " + argument(0) + ".";
    }
    if (op == "tell") {
        const larry::Reply told = larry.brain.hear(larry.ops.from_text(argument(0) + "."), "user:" + larry.user);
        return told.text;
    }
    return "I do not know how to " + op + " yet.";
}

int run(std::span<const std::string_view> args) {
    if (args.empty() || args[0] == "help" || args[0] == "--help" || args[0] == "-h") {
        std::print("{}", usage);
        return args.empty() ? 1 : 0;
    }
    const std::string_view command = args[0];
    const std::span<const std::string_view> rest = args.subspan(1);
    if (command == "measure") {
        // A6: the constellation's rules, a scratch file, the treebank of its
        // language under content/ud/ (English EWT, Portuguese Bosque).
        const larry::Language language = language_from_environment();
        const std::string treebank = language == larry::Language::Portuguese ? "pt_bosque" : "en_ewt";
        const std::filesystem::path dir = std::filesystem::path{LARRY_CONTENT_DIR} / "ud";
        const std::filesystem::path train = dir / (treebank + "-ud-train.conllu");
        const std::filesystem::path test = dir / (treebank + "-ud-test.conllu");
        if (!std::filesystem::exists(train) || !std::filesystem::exists(test)) {
            throw std::runtime_error(std::format("measure needs the treebank: run scripts/ud.sh {} first",
                                                 larry::locale(language)));
        }
        std::vector<std::int64_t> counts;
        for (const std::string_view given : rest) {
            counts.push_back(given == "all" ? std::numeric_limits<std::int64_t>::max() : std::stoll(std::string{given}));
        }
        if (counts.empty()) {
            counts = {100, 300, 1000, 3000, std::numeric_limits<std::int64_t>::max()};
        }
        const larry::BaseRules rules{language};
        const std::vector<larry::UdSentence> training = larry::read_conllu(train);
        const std::vector<larry::UdSentence> testing = larry::read_conllu(test);
        std::println("{}: {} training sentences, {} test sentences", treebank, training.size(), testing.size());
        const std::unique_ptr<larry::Dictionary> dictionary = open_dictionary(language);
        for (const bool with_dictionary : {false, true}) {
            if (with_dictionary && !dictionary) {
                break;
            }
            std::println("{}", with_dictionary ? "with the dictionary:" : "from memory alone:");
            const std::vector<larry::Score> curve =
                larry::measure(rules, training, testing, counts, std::filesystem::temp_directory_path() / "larry_measure.atoms",
                               with_dictionary ? dictionary.get() : nullptr,
                               [](std::string_view what) { std::println(stderr, "larry: {}", what); });
            for (const larry::Score& score : curve) {
                std::println("  {}", score.text());
            }
        }
        return 0;
    }
    if (command == "babi") {
        const std::filesystem::path dir = std::filesystem::path{LARRY_CONTENT_DIR} / "babi";
        // The original files, when the user has them: content/babi/en/qa<task>_*_test.txt.
        const auto text_file = [&](int task) -> std::filesystem::path {
            const std::filesystem::path en = dir / "en";
            if (std::filesystem::is_directory(en)) {
                for (const auto& entry : std::filesystem::directory_iterator{en}) {
                    const std::string name = entry.path().filename().string();
                    if (name.starts_with(std::format("qa{}_", task)) && name.ends_with("_test.txt")) {
                        return entry.path();
                    }
                }
            }
            return {};
        };
        std::vector<int> tasks;
        if (!rest.empty()) {
            tasks.push_back(std::stoi(std::string{rest[0]}));
        } else {
            for (int task = 1; task <= 20; ++task) {
                if (std::filesystem::exists(dir / std::format("qa{}_test_0.json", task)) || !text_file(task).empty()) {
                    tasks.push_back(task);
                }
            }
        }
        if (tasks.empty()) {
            throw std::runtime_error("babi needs the stories: run scripts/babi.sh first");
        }
        const larry::BaseRules rules{larry::Language::English};
        for (const int task : tasks) {
            std::vector<larry::BabiStory> stories;
            if (const std::filesystem::path text = text_file(task); !text.empty()) {
                stories = larry::read_babi_text(text);
            }
            for (const int offset : {0, 100}) {
                if (!stories.empty()) {
                    break;
                }
                const std::filesystem::path file = dir / std::format("qa{}_test_{}.json", task, offset);
                if (std::filesystem::exists(file)) {
                    std::vector<larry::BabiStory> page = larry::read_babi(file);
                    stories.insert(stories.end(), page.begin(), page.end());
                }
            }
            if (stories.empty()) {
                std::println("task {}: no stories under {}; run scripts/babi.sh, or put the original "
                             "tasks_1-20_v1-2/en/*.txt files under {}/en/", task, dir.string(), dir.string());
                continue;
            }
            const larry::BabiResult result =
                larry::run_babi(rules, task, stories, std::filesystem::temp_directory_path() / "larry_babi.atoms",
                                [](std::string_view what) { std::println(stderr, "larry: {}", what); });
            std::println("{}", result.text());
            for (const std::string& miss : result.misses) {
                std::println("  miss: {}", miss);
            }
        }
        return 0;
    }
    if (command == "bench") {
        // F8: its own rules and scratch file; the user's memory and the cloud stay out of it.
        std::int64_t atoms = 10000;
        if (!rest.empty()) {
            atoms = std::stoll(std::string{rest[0]});
        }
        const larry::BaseRules rules{larry::Language::English};
        const larry::Benchmark result =
            larry::bench(rules, atoms, std::filesystem::temp_directory_path() / "larry_bench.atoms",
                         [](std::string_view what) { std::println(stderr, "larry: {}", what); });
        std::print("{}", result.text());
        return 0;
    }
    Larry larry;

    if (command == "show") {
        if (rest.empty()) {
            throw std::runtime_error("show needs a sentence");
        }
        print(larry, larry.assimilation.describe(larry.ops.from_text(join(rest)), &larry.memory));
        return 0;
    }
    if (command == "tell") {
        if (rest.empty()) {
            throw std::runtime_error("tell needs a sentence");
        }
        const std::vector<larry::Bytes> taught = categories(rest.subspan(1));
        const larry::Description d =
            larry.assimilation.describe(larry.ops.from_text(rest[0]), &larry.memory, taught);
        const larry::Stored stored =
            larry.brain.remember(d, larry::Status::Proposed, "user:" + larry.user);
        print(larry, d);
        std::println("stored        : {}", stored_name(stored));
        return 0;
    }
    if (command == "read") {
        if (rest.size() != 1) {
            throw std::runtime_error("read needs one file");
        }
        // W2: the facts become proposed conceptions; the rest is listed by class.
        const std::string text = content_of(rest[0]);
        Tally tally;
        larry::ContentTally classes;
        const std::string source = "read:" + std::filesystem::path{rest[0]}.filename().string();
        // N4: the file is one molecule: its facts in order, with the source and the time.
        const std::string molecule = source + ":" + larry::Brain::now();
        larry.brain.molecule(larry::Bytes(molecule.begin(), molecule.end()));
        const larry::Content content{larry.rules};
        std::vector<larry::Brain::Question> questions;  // A5: one per word, the first sentence it is in
        for (const larry::Piece& piece : content.classify(text, larry.brain)) {
            classes.add(piece.what);
            if (piece.what != larry::ContentClass::Fact) {
                std::println("{:<11} {}", larry::name(piece.what), larry.ops.text(piece.sentence));
                continue;
            }
            const larry::Stored stored = larry.brain.remember(piece.description, larry::Status::Proposed, source);
            tally.add(stored);
            std::println("{:<11} {}{}", stored_name(stored), larry.ops.text(piece.sentence),
                         open_words(piece.description));
            for (larry::Brain::Question& q : larry.brain.questions(piece.description)) {
                if (std::ranges::none_of(questions, [&](const larry::Brain::Question& held) { return held.word == q.word; })) {
                    questions.push_back(std::move(q));
                }
            }
        }
        print_classes(classes);
        tally.print();
        if (!questions.empty()) {
            std::println("questions     : {} (answer with: larry answer \"<sentence>\" <word> <category>)", questions.size());
            for (const larry::Brain::Question& q : questions) {
                std::println("  {}{}", q.text, q.guess.empty() ? "" : "  (" + q.guess + ")");
            }
        }
        if (const std::optional<larry::Molecule> m = larry.memory.molecule(larry.brain.molecule())) {
            std::println("molecule      : {} ({} conceptions, in order)", molecule, m->members.size());
        }
        larry.brain.molecule({});
        return 0;
    }
    if (command == "answer") {
        if (rest.size() != 3) {
            throw std::runtime_error("answer needs \"<sentence>\" <word> <category>");
        }
        const std::optional<larry::Stored> stored =
            larry.brain.teach(larry.ops.from_text(rest[0]), rest[1], rest[2], "user:" + larry.user);
        if (!stored) {
            throw std::runtime_error(std::format("\"{}\" is not in the sentence, or \"{}\" is not a category I know",
                                                 rest[1], rest[2]));
        }
        std::println("{} is {} in \"{}\": {}", rest[1], rest[2], rest[0], stored_name(*stored));
        return 0;
    }
    if (command == "classify") {
        if (rest.empty()) {
            throw std::runtime_error("classify needs a file or a text");
        }
        const std::string text = rest.size() == 1 && std::filesystem::exists(rest[0]) ? content_of(rest[0])
                                                                                       : join(rest);
        larry::ContentTally classes;
        const larry::Content content{larry.rules};
        for (const larry::Piece& piece : content.classify(text, larry.brain)) {
            classes.add(piece.what);
            std::println("{:<11} {}", larry::name(piece.what), larry.ops.text(piece.sentence));
            std::println("            {}", piece.reason);
        }
        print_classes(classes);
        return 0;
    }
    if (command == "teach") {
        if (rest.size() != 1) {
            throw std::runtime_error("teach needs one lesson file, or the name of a lesson in the cloud");
        }
        if (std::filesystem::exists(rest[0])) {
            larry.teach(rest[0], true).print();
            return 0;
        }
        // N2f: a lesson the cloud holds, by its name.
        if (!larry.cloud) {
            throw std::runtime_error(std::format("no file {} and no cloud to hold a lesson of that name", rest[0]));
        }
        const std::optional<larry::Database::CloudLesson> lesson = larry.cloud->lesson(larry.locale(), rest[0]);
        if (!lesson) {
            throw std::runtime_error(std::format("no file {} and no lesson of that name in the cloud (larry lessons)", rest[0]));
        }
        larry.teach_from_cloud(*lesson, true).print();
        std::println("the cloud lesson {} is taught", lesson->name);
        return 0;
    }
    if (command == "lessons") {
        // N2f: the lessons: the files here and the ones in the cloud; push
        // puts the files in the cloud, pull writes one out to correct it.
        const std::vector<std::filesystem::path> files = larry::lesson_files(larry.constellation.language());
        if (rest.empty()) {
            for (const std::filesystem::path& file : files) {
                std::println("{:<40} file, {} lessons", file.filename().string(), larry::read_lessons(file).size());
            }
            if (larry.cloud) {
                for (const larry::Database::CloudLesson& lesson : larry.cloud->lessons(larry.locale())) {
                    std::println("{:<40} cloud, {}, written {}{}", lesson.name, lesson.status, lesson.written,
                                 lesson.taught.empty() ? "" : ", taught " + lesson.taught);
                }
            } else {
                std::println("(no cloud: the drafts of study go to {})", larry::Study::drafts_directory(larry.constellation.language()).string());
            }
            return 0;
        }
        const std::string_view what = rest.front();
        if (what == "push") {
            if (!larry.cloud) {
                throw std::runtime_error("lessons push needs the cloud (LARRY_DB)");
            }
            for (const std::filesystem::path& file : files) {
                larry.cloud->store_lesson(larry.locale(), file.stem().string(), content_of(file), "taught");
                std::println("{:<40} pushed", file.stem().string());
            }
            std::println("{} lesson files in the cloud", files.size());
            return 0;
        }
        if (what == "pull" || what == "show") {
            if (rest.size() < 2) {
                throw std::runtime_error(std::format("lessons {} needs the name of a lesson", what));
            }
            if (!larry.cloud) {
                throw std::runtime_error("the cloud holds the lessons to pull or show (LARRY_DB)");
            }
            const std::optional<larry::Database::CloudLesson> lesson = larry.cloud->lesson(larry.locale(), rest[1]);
            if (!lesson) {
                throw std::runtime_error(std::format("no lesson {} in the cloud (larry lessons)", rest[1]));
            }
            if (what == "show") {
                std::print("{}", lesson->text);
                return 0;
            }
            const std::filesystem::path dir = rest.size() > 2 ? std::filesystem::path{rest[2]}
                                                              : larry::Study::drafts_directory(larry.constellation.language());
            std::filesystem::create_directories(dir);
            const std::filesystem::path file = dir / (lesson->name + ".txt");
            std::ofstream out{file, std::ios::binary | std::ios::trunc};
            out << lesson->text;
            std::println("{} ({}) written to {}; correct it, then: larry teach {}", lesson->name, lesson->status,
                         file.string(), file.string());
            return 0;
        }
        throw std::runtime_error("lessons takes nothing, push, pull <name> [directory] or show <name>");
    }
    if (command == "rebuild") {
        larry.rebuild(true);
        return 0;
    }
    if (command == "themes") {
        // T1: the index of what Larry holds, by theme.
        for (const larry::Brain::Theme& t : larry.brain.themes()) {
            std::string sample;
            for (const std::string& text : t.sample) {
                sample += (sample.empty() ? "" : " ") + text;
            }
            std::println("{:<14} {:>6}  {}", std::string(t.name.begin(), t.name.end()), t.count, sample);
        }
        return 0;
    }
    if (command == "theme") {
        if (rest.empty()) {
            throw std::runtime_error("theme needs a name: larry themes lists them");
        }
        const larry::Bytes name(rest.front().begin(), rest.front().end());
        const std::vector<larry::StoredAtom> about = larry.brain.about_theme(name);
        for (const larry::StoredAtom& atom : about) {
            std::println("{:>6}  {}", atom.id, larry.ops.text(atom.description.atom));
        }
        std::println("{} conceptions about {}", about.size(), rest.front());
        return 0;
    }
    if (command == "understand") {
        if (rest.empty()) {
            throw std::runtime_error("understand needs a sentence");
        }
        // P1: what it is, what it names, and what Larry holds of it.
        for (const larry::Sentence& sentence : larry.assimilation.sentences(join(rest))) {
            const larry::Description d = larry.assimilation.describe(sentence, &larry.memory);
            std::println("{}", larry.brain.understand(d).text());
        }
        return 0;
    }
    if (command == "groups") {
        if (rest.empty()) {
            throw std::runtime_error("groups needs a sentence");
        }
        // A8: each word, and the word it attaches to.
        const larry::Description d = larry.assimilation.describe(larry.ops.from_text(join(rest)), &larry.memory);
        const std::vector<std::size_t> heads = larry.assimilation.attachments(d);
        for (std::size_t i = 0; i < d.entities.entities.size(); ++i) {
            const larry::Entity& e = d.entities.entities[i];
            const std::string head = heads[i] == larry::Assimilation::root
                                         ? std::string{"root"}
                                         : std::string(d.entities.entities[heads[i]].word.begin(),
                                                       d.entities.entities[heads[i]].word.end());
            std::println("{:<16} -> {}", std::string(e.word.begin(), e.word.end()), head);
        }
        return 0;
    }
    if (command == "goal") {
        if (rest.empty()) {
            throw std::runtime_error("goal needs an order");
        }
        // S1: the state the order would make, whether it is so, and the plan.
        const larry::Description d = larry.assimilation.describe(larry.ops.from_text(join(rest)), &larry.memory);
        const std::optional<larry::Brain::Goal> g = larry.brain.goal(d);
        if (!g) {
            std::println("{}", larry.brain.say("cannot do"));
            return 0;
        }
        std::println("{}", std::vformat(larry.brain.say("goal"), std::make_format_args(g->state)));
        if (g->satisfied) {
            std::println("{}", std::vformat(larry.brain.say("already so"), std::make_format_args(g->state)));
        } else {
            std::println("{}", g->plan.text());
        }
        for (const std::string& because : g->because) {
            std::println("  because: {}", because);
        }
        return 0;
    }
    if (command == "consult") {
        if (rest.empty()) {
            throw std::runtime_error("consult needs a claim or a question");
        }
        // W5: Larry's own answer, then the model's second opinion. Nothing is
        // stored: the model is a source, never a validator (R2b, Q24).
        const larry::Advisor advisor;
        for (const larry::Sentence& sentence : larry.assimilation.sentences(join(rest))) {
            const larry::Reply reply = larry.brain.answer(sentence);
            std::println("larry: {}", reply.text);
            for (const std::string& because : reply.because) {
                std::println("  because: {}", because);
            }
            const larry::Description d = larry.assimilation.describe(sentence, &larry.memory);
            const std::string text{larry.ops.text(sentence)};
            try {
                if (as_text(d.category.bytes) == "question") {
                    const std::string answer = advisor.ask(text);
                    std::println("{}: {}", advisor.model(), answer.empty() ? "(declined)" : answer);
                } else {
                    const larry::Opinion opinion = advisor.opinion(text);
                    std::println("{}: {}{}{}", advisor.model(), opinion.refused ? "(declined)" : opinion.verdict,
                                 opinion.reason.empty() ? "" : ": ", opinion.reason);
                }
            } catch (const std::exception& e) {
                std::println(stderr, "larry: {}", e.what());
                return 1;
            }
        }
        std::println("  (nothing stored: the model is a source, not a validator)");
        return 0;
    }
    if (command == "extract") {
        if (rest.empty()) {
            throw std::runtime_error("extract needs a file or a text");
        }
        // W5: the facts the model reads in the text, each heard as a proposal
        // from the source llm:<model>, for the user to validate.
        std::size_t limit = 20;
        std::span<const std::string_view> given = rest;
        if (given.size() > 1 && std::ranges::all_of(given.back(), [](char c) { return c >= '0' && c <= '9'; })) {
            limit = std::stoull(std::string{given.back()});
            given = given.subspan(0, given.size() - 1);
        }
        std::string text = join(given);
        if (given.size() == 1 && std::filesystem::is_regular_file(given.front())) {
            std::ifstream in{std::filesystem::path{given.front()}, std::ios::binary};
            std::stringstream buffer;
            buffer << in.rdbuf();
            text = buffer.str();
        }
        const larry::Advisor advisor;
        std::vector<std::string> facts;
        try {
            facts = advisor.propose(text, limit);
        } catch (const std::exception& e) {
            std::println(stderr, "larry: {}", e.what());
            return 1;
        }
        if (facts.empty()) {
            std::println("{} proposed nothing.", advisor.model());
            return 0;
        }
        for (const std::string& fact : facts) {
            for (const larry::Sentence& sentence : larry.assimilation.sentences(fact)) {
                const larry::Reply reply = larry.brain.hear(sentence, advisor.source());
                std::println("{}  ->  {}", fact, reply.text);
            }
        }
        std::println("{} facts proposed by {}; larry validate judges them.", facts.size(), advisor.model());
        return 0;
    }
    if (command == "code") {
        if (rest.empty()) {
            throw std::runtime_error("code needs a source file");
        }
        // Code as sentences: what the file defines, uses and calls; with
        // --store, heard as proposals from the source code:<file>.
        const bool store = rest.back() == "--store";
        const std::filesystem::path file{store ? rest.front() : rest.back()};
        const larry::Code code;
        const std::vector<larry::CodeFact> facts = code.read(file);
        for (const larry::CodeFact& fact : facts) {
            if (store) {
                const larry::Reply reply = larry.brain.hear(larry.ops.from_text(fact.sentence),
                                                           "code:" + file.filename().string());
                std::println("{}  ->  {}", fact.sentence, reply.text);
            } else {
                std::println("{:>5}: {}", fact.line, fact.sentence);
            }
        }
        std::println("{} sentences from {} ({}){}", facts.size(), file.filename().string(), larry::Code::language_of(file),
                     store ? ", stored as proposals" : "");
        return 0;
    }
    if (command == "translate") {
        if (rest.size() < 2) {
            throw std::runtime_error("translate needs a sentence and a locale (en, pt)");
        }
        // G7: the image here, its words by to_<locale>.txt, said there with
        // the other constellation's rules and memory.
        const std::string_view locale = rest.back();
        const std::optional<larry::Language> named = larry::language_named(locale);
        if (!named) {
            throw std::runtime_error(std::format("translate: no constellation named {} (en, pt)", locale));
        }
        const larry::Language target = *named;
        const larry::BaseRules there{target};
        const std::unique_ptr<larry::Dictionary> dictionary = open_dictionary(target);
        const larry::Grammar grammar{there};
        const larry::Assimilation other{there, dictionary.get(), &grammar};
        larry::Memory memory_there{larry::Memory::file_from_environment(target)};
        const std::string text = join(rest.subspan(0, rest.size() - 1));
        for (const larry::Sentence& sentence : larry.assimilation.sentences(text)) {
            const larry::Description d = larry.assimilation.describe(sentence, &larry.memory);
            std::vector<larry::Bytes> missing;
            const larry::ImageElectron image = larry.assimilation.translate(d.image, larry.rules.translations(locale), &missing);
            const std::string said = other.sentence_of(image, &memory_there);
            std::println("{}", said.empty() ? std::string{"?"} : said);
            std::println("  image: {}", as_text(image.bytes));
            for (const larry::Bytes& word : missing) {
                const std::string w(word.begin(), word.end());
                std::println("  {}", std::vformat(larry.brain.say("no translation"), std::make_format_args(w)));
            }
        }
        return 0;
    }
    if (command == "explain") {
        if (rest.empty()) {
            throw std::runtime_error("explain needs an observation");
        }
        // R6: the assumption that would explain it, from the rules (R9).
        const larry::Description d = larry.assimilation.describe(larry.ops.from_text(join(rest)), &larry.memory);
        const std::vector<larry::Brain::Explanation> found = larry.brain.explain(d);
        if (found.empty()) {
            std::println("{}", larry.brain.say("no explanation"));
            return 0;
        }
        for (const larry::Brain::Explanation& e : found) {
            std::println("{}", std::vformat(larry.brain.say("perhaps"), std::make_format_args(e.assumption)));
            std::println("  because: {}", larry.ops.text(e.rule.atom.description.atom));
            std::println("  known: {}", e.known == larry::Truth::True ? "true" : e.known == larry::Truth::False ? "false" : "unknown");
        }
        return 0;
    }
    if (command == "grammar") {
        if (rest.empty()) {
            std::println("{} patterns, {} learned from validated conceptions", larry.grammar.size(),
                         larry.grammar.learned());
            for (const larry::Grammar::Pattern& p : larry.grammar.patterns()) {
                std::println("{}: {}", p.name, p.text);
            }
            return 0;
        }
        const larry::Grammar& grammar = larry.grammar;
        for (const larry::Sentence& sentence : larry.assimilation.sentences(join(rest))) {
            const larry::Description d = larry.assimilation.describe(sentence, &larry.memory);
            std::println("{}", larry.ops.text(sentence));
            std::vector<larry::Bytes> categories;
            std::string shown;
            for (const larry::Entity& e : d.entities.entities) {
                categories.push_back(e.category);
                shown += shown.empty() ? "" : ", ";
                shown += e.category.empty() ? "?" : std::string{as_text(e.category)};
            }
            std::println("categories : {}", shown);
            if (std::ranges::any_of(categories, [](const larry::Bytes& c) { return c.empty(); })) {
                std::println("no pattern : a word has no category{}", open_words(d));
                continue;
            }
            const bool question = as_text(d.category.bytes) == "question";
            const larry::Fit fit = grammar.fit(categories, question);
            if (fit.fits) {
                std::println("pattern    : {}", fit.pattern);
                std::println("             {}", grammar.text(fit.pattern));
                for (std::size_t i = 0; i < d.entities.entities.size(); ++i) {
                    std::println("  {:<14} {:<15} {}", as_text(d.entities.entities[i].word),
                                 as_text(categories[i]), as_text(fit.roles[i]));
                }
                continue;
            }
            std::string expected;
            for (const larry::Bytes& e : fit.expected) {
                expected += expected.empty() ? "" : ", ";
                expected += as_text(e) == "end" ? "the end of the sentence" : std::string{as_text(e)};
            }
            if (fit.breaks_at < d.entities.entities.size()) {
                std::println("no pattern : breaks at word {} \"{}\" ({}); expected: {}", fit.breaks_at + 1,
                             as_text(d.entities.entities[fit.breaks_at].word),
                             as_text(categories[fit.breaks_at]), expected);
            } else {
                std::println("no pattern : the sentence ends too early; expected: {}", expected);
            }
            std::println("nearest    : {}", fit.pattern);
            std::println("             {}", grammar.text(fit.pattern));
        }
        return 0;
    }
    if (command == "harness") {
        if (rest.empty()) {
            throw std::runtime_error("harness needs a sentence");
        }
        for (const larry::Sentence& sentence : larry.assimilation.sentences(join(rest))) {
            const larry::Description said = larry.assimilation.describe(sentence, &larry.memory);
            const larry::Reading reading = larry.brain.read(said);
            const larry::Description& d = reading.changed ? reading.meant : said;
            std::println("{}", larry.ops.text(sentence));
            if (reading.changed) {
                std::println("read as    : {}", larry.ops.text(d.atom));
            }
            const larry::Report report = larry.brain.judge(d);
            if (report.judgements.empty()) {
                std::println("relations  : none{}", open_words(d));
                continue;
            }
            for (const larry::Judgement& j : report.judgements) {
                std::println("{:<10} : {}, {} {}", larry::name(j.standing), as_text(j.relation.dependent),
                             as_text(j.relation.kind), as_text(j.relation.head));
                std::println("             {}{}", j.text(), j.from_cloud ? " (from the cloud)" : "");
            }
        }
        return 0;
    }
    if (command == "recognize") {
        if (rest.empty()) {
            throw std::runtime_error("recognize needs a sentence");
        }
        const larry::Content content{larry.rules};
        for (const larry::Sentence& sentence : larry.assimilation.sentences(join(rest))) {
            const larry::Brain::Recognition r = larry.brain.recognize(sentence);
            std::println("{}", larry.ops.text(sentence));
            std::println("kind       : {} ({}), by {}", r.kind, larry::name(r.qualification), r.kind_reason);
            std::println("emotion    : {}, by {}", r.emotion, r.emotion_reason);
            if (r.command && r.command->operation != "request") {
                std::string arguments;
                for (const std::string& a : r.command->arguments) {
                    arguments += " \"" + a + "\"";
                }
                std::println("command    : {}{}", r.command->operation, arguments);
            } else {
                std::println("command    : none");
            }
            if (r.calculation) {
                std::println("arithmetic : {}", r.calculation->rule());
            }
            const larry::Piece piece = content.classify(sentence, larry.brain);
            std::println("content    : {}, {}", larry::name(piece.what), piece.reason);
        }
        return 0;
    }
    if (command == "qualify") {
        if (rest.empty()) {
            throw std::runtime_error("qualify needs a sentence");
        }
        for (const larry::Sentence& sentence : larry.assimilation.sentences(join(rest))) {
            const larry::Description d = larry.assimilation.describe(sentence, &larry.memory);
            const larry::Qualifying q = larry.brain.qualify(d);
            std::println("{}", larry.ops.text(sentence));
            std::println("qualification : {} ({})", larry::name(q.by_rules), larry::user_name(q.by_rules));
            std::println("by the rules  : {}", q.rule);
            if (q.examples.empty()) {
                std::println("by example    : no conception has the same structure{}", open_words(d));
            } else {
                std::println("by example    : {}{}, from {} {} conception{} of the same structure{}",
                             q.by_examples ? larry::name(*q.by_examples) : "no majority",
                             q.by_examples ? std::format(" ({})", larry::user_name(*q.by_examples)) : "",
                             q.examples.size(), q.validated ? "validated" : "proposed",
                             q.examples.size() == 1 ? "" : "s", q.from_cloud ? " (from the cloud)" : "");
                for (std::size_t i = 0; i < q.examples.size() && i < 5; ++i) {
                    std::println("                {} ({})", larry.ops.text(q.examples[i].description.atom),
                                 as_text(q.examples[i].description.category.bytes));
                }
            }
            std::println("agreement     : {}", q.examples.empty() ? "the rules alone decide"
                                                : !q.by_examples   ? "the examples disagree among themselves; the rules decide"
                                                : q.agree()        ? "the rules and the examples agree"
                                                                   : "the rules and the examples disagree; Larry says both");
        }
        return 0;
    }
    if (command == "compare") {
        if (rest.size() != 2) {
            throw std::runtime_error("compare needs two sentences");
        }
        const larry::Description a =
            larry.assimilation.describe(larry.ops.from_text(rest[0]), &larry.memory);
        const larry::Description b =
            larry.assimilation.describe(larry.ops.from_text(rest[1]), &larry.memory);
        const auto word = [](const larry::Description& d, std::size_t i) {
            return as_text(d.entities.entities[i].word);
        };
        const auto yes = [](bool holds) { return holds ? "yes" : "no"; };
        const auto structure = [](const larry::Description& d) {
            std::string out;
            for (const larry::Entity& e : d.entities.entities) {
                out += out.empty() ? "" : " ";
                out += e.category.empty() ? "?" : as_text(e.category);
            }
            return out;
        };
        std::println("a                : {}", rest[0]);
        std::println("                   {}", structure(a));
        std::println("b                : {}", rest[1]);
        std::println("                   {}", structure(b));
        std::println("C1 identity      : {}", yes(larry.cognition.identity(a, b).holds));
        std::println("C2 same form     : {}", yes(larry.cognition.same_form(a, b).holds));
        const larry::Comparison alignment = larry.cognition.align(a, b);
        std::string pairs;
        for (const larry::Match& m : alignment.matches) {
            pairs += std::format(" {}{}{}", word(a, m.a), m.same_word ? '=' : '~', word(b, m.b));
        }
        for (const std::size_t i : alignment.only_a) {
            pairs += std::format(" {}~", word(a, i));
        }
        for (const std::size_t i : alignment.only_b) {
            pairs += std::format(" ~{}", word(b, i));
        }
        std::println("C3 alignment     : {}{}", yes(alignment.holds), pairs);
        const larry::Comparison difference = larry.cognition.difference(a, b);
        std::size_t differing = difference.only_a.size() + difference.only_b.size();
        std::string differences;
        for (const larry::Match& m : difference.matches) {
            if (!m.same_word) {
                ++differing;
                differences += std::format(" {} / {}", word(a, m.a), word(b, m.b));
            }
        }
        std::println("C4 difference    : {} ({} differing){}{}", yes(difference.holds), differing,
                     differences,
                     difference.pattern.empty()
                         ? std::string{}
                         : std::format("  pattern: {}", as_text(difference.pattern)));
        std::println("C5 same structure: {}", yes(larry.cognition.same_structure(a, b).holds));
        return 0;
    }
    if (command == "ask") {
        if (rest.empty()) {
            throw std::runtime_error("ask needs a question or a claim");
        }
        for (const larry::Sentence& sentence : larry.assimilation.sentences(join(rest))) {
            const larry::Reply reply = larry.brain.answer(sentence);
            std::println("{}", reply.text);
            for (const std::string& because : reply.because) {
                std::println("  because: {}", because);
            }
        }
        return 0;
    }
    if (command == "say") {
        if (rest.empty()) {
            throw std::runtime_error("say needs a sentence");
        }
        const larry::Reply reply = larry.brain.hear(larry.ops.from_text(join(rest)), larry.source);
        std::println("{}", reply.text);
        for (const std::string& because : reply.because) {
            std::println("  because: {}", because);
        }
        if (reply.command) {
            const std::string done = execute(larry, *reply.command);
            if (!done.empty()) {
                std::println("{}", done);
            }
        }
        return 0;
    }
    if (command == "chat") {
        larry::Brain& brain = larry.brain;
        brain.notice(show_notice);
        // The chat exposed to people: with a name, what is heard comes from
        // "chat:<name>", who is no validator: what goes against memory is
        // denied, and what is new waits for the validator (P4, P5).
        if (!rest.empty()) {
            larry.source = "chat:" + std::string{rest.front()};
        }
        // N4: the conversation is one molecule.
        const std::string conversation = "chat:" + (rest.empty() ? larry.user : std::string{rest.front()}) + ":" + larry::Brain::now();
        brain.molecule(larry::Bytes(conversation.begin(), conversation.end()));
        larry::Reply last;
        std::println("Larry: hello. I hold {} conceptions{}. Say \"bye\" to end, \"why?\" to ask why.",
                     larry.memory.count(),
                     larry.cloud ? std::format(" here and {} in the cloud", larry.cloud->count())
                                 : std::string{", and no cloud"});
        for (std::string line; std::print("> "), std::getline(std::cin, line);) {
            const std::vector<larry::Sentence> sentences = larry.assimilation.sentences(line);
            if (sentences.empty()) {
                continue;
            }
            for (const larry::Sentence& sentence : sentences) {
                const std::string text{larry.ops.text(sentence)};
                std::string folded = text;
                for (char& c : folded) {
                    c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
                }
                if (folded == "why" || folded == "why?") {
                    if (last.because.empty()) {
                        std::println("Larry: I have not said anything yet.");
                    }
                    for (const std::string& because : last.because) {
                        std::println("Larry: because {}", because);
                    }
                    continue;
                }
                if (folded == "bye" || folded == "bye." || folded == "bye!" || folded == "quit" ||
                    folded == "exit") {
                    const std::optional<larry::Molecule> m = larry.memory.molecule(brain.molecule());
                    std::println("Larry: bye.{}", m && !m->members.empty()
                                                       ? std::format(" This conversation is molecule {} ({} conceptions).",
                                                                     conversation, m->members.size())
                                                       : std::string{});
                    return 0;
                }
                last = brain.hear(sentence, larry.source);
                // T1, P1: what it was about, and what it named that Larry holds nothing about.
                if (!last.theme.empty() || !last.unknown_things.empty()) {
                    std::string about = last.theme.empty() ? std::string{} : std::vformat(brain.say("about"), std::make_format_args(last.theme));
                    for (const std::string& thing : last.unknown_things) {
                        about += (about.empty() ? "" : "; ") + std::vformat(brain.say("unknown to me"), std::make_format_args(thing));
                    }
                    std::println("  ({})", about);
                }
                std::print("Larry: ");
                stream(last.text);
                // P5: a question about a thing Larry holds nothing about: the research, and the thing proposed.
                if (!last.unknown_things.empty() && !last.command && last.text.starts_with(brain.say("unknown"))) {
                    std::string longest;
                    for (const std::string& thing : last.unknown_things) {
                        if (thing.size() > longest.size()) {
                            longest = thing;
                        }
                    }
                    std::print("Larry: ");
                    stream(research(larry, longest, larry.source));
                }
                if (last.command && last.command->operation == "cannot") {
                    // One request per line: what follows a denied request goes with it.
                    if (&sentence != &sentences.back()) {
                        std::println("Larry: {}", brain.say("the rest"));
                    }
                    break;
                }
                if (last.command) {
                    try {
                        const std::string done = execute(larry, *last.command);
                        std::print("Larry: ");
                        stream(done);
                    } catch (const std::exception& e) {
                        std::println("Larry: I could not: {}", e.what());
                    }
                }
            }
        }
        std::println("");
        return 0;
    }
    if (command == "validate") {
        const auto line = [&](const larry::StoredAtom& atom) {
            std::string sources;
            for (const std::string& s : atom.sources) {
                sources += sources.empty() ? "" : ", ";
                sources += s;
            }
            return std::format("{:>6}  {}  [{}]{}", atom.id, larry.ops.text(atom.description.atom),
                               sources.empty() ? "no source" : sources,
                               atom.reading.empty() ? "" : std::format("  (read as: {})", atom.reading));
        };
        if (!larry.brain.is_validator(larry.user)) {
            throw std::runtime_error(std::format(
                "only a validator decides what is true, and \"{}\" is not one; "
                "see larry validators",
                larry.user));
        }
        if (rest.empty()) {
            const std::vector<larry::StoredAtom> waiting = larry.brain.proposed();
            std::println("{} proposed conceptions{}, deciding as {}", waiting.size(),
                         larry.cloud ? " in the cloud" : " in the cache", larry.user);
            for (const larry::StoredAtom& atom : waiting) {
                std::print("{}\n  validate? [y/n/s/q] ", line(atom));
                std::string answer;
                if (!std::getline(std::cin, answer)) {
                    std::println("");
                    break;
                }
                if (answer == "y") {
                    larry.brain.decide(atom.description.metadata, larry::Status::Validated, larry.user);
                    std::println("  validated");
                } else if (answer == "n") {
                    larry.brain.decide(atom.description.metadata, larry::Status::Withdrawn, larry.user);
                    std::println("  withdrawn");
                } else if (answer == "q") {
                    break;
                }
            }
            return 0;
        }
        if (rest[0] == "list") {
            for (const larry::StoredAtom& atom : larry.brain.proposed()) {
                std::println("{}", line(atom));
            }
            return 0;
        }
        if ((rest[0] == "accept" || rest[0] == "reject") && rest.size() == 2) {
            const std::optional<larry::StoredAtom> atom =
                larry.brain.conception(std::stoll(std::string{rest[1]}));
            if (!atom) {
                throw std::runtime_error(std::format("there is no conception {}", rest[1]));
            }
            const larry::Status status =
                rest[0] == "accept" ? larry::Status::Validated : larry::Status::Withdrawn;
            larry.brain.decide(atom->description.metadata, status, larry.user);
            std::println("{} by {}: {}", larry::name(status), larry.user,
                         larry.ops.text(atom->description.atom));
            return 0;
        }
        throw std::runtime_error("validate takes nothing, list, accept <id> or reject <id>");
    }
    if (command == "validators") {
        if (rest.size() == 2 && rest[0] == "add") {
            if (!larry.brain.add_validator(rest[1], larry.user)) {
                throw std::runtime_error(std::format(
                    "only a validator adds another, and \"{}\" is not one", larry.user));
            }
            std::println("{} may validate", rest[1]);
            return 0;
        }
        if (!rest.empty()) {
            throw std::runtime_error("validators takes nothing or add <name>");
        }
        const std::vector<std::string> names = larry.brain.validators();
        if (names.empty()) {
            std::println("nobody may validate yet: larry validators add <your name>");
        }
        for (const std::string& name : names) {
            std::println("{}{}", name, name == larry.user ? "  (you)" : "");
        }
        std::println("you are {} (LARRY_USER)", larry.user);
        return 0;
    }
    if (command == "sync") {
        if (!larry.cloud) {
            throw std::runtime_error("sync needs a cloud: set LARRY_DB");
        }
        std::int64_t pull = 100;
        if (!rest.empty()) {
            pull = std::stoll(std::string{rest[0]});
        }
        const larry::Brain::Synced synced = larry.brain.sync(pull);
        std::println("{} conceptions pushed to the cloud, {} pulled into the cache, {} described anew in the cloud, "
                     "{} cached ones given the cloud's standing",
                     synced.pushed, synced.pulled, synced.redescribed, synced.refreshed);
        std::println("{} conceptions here, {} in the cloud", larry.memory.count(),
                     larry.cloud->count());
        return 0;
    }
    if (command == "words") {
        if (rest.empty()) {
            std::println("{:<20} {:<32} {:>5}  {}", "word", "categories", "uses", "context");
            for (const larry::Bytes& word : larry.memory.words()) {
                const std::vector<larry::WordUse> uses = larry.memory.uses(word);
                std::string categories;
                for (const larry::CategoryCount& c : larry.memory.categories_of(word)) {
                    categories += categories.empty() ? "" : ", ";
                    categories += std::format("{} {}", as_text(c.category), c.count);
                }
                const larry::WordUse& first = uses.front();
                std::println("{:<20} {:<32} {:>5}  {} _ {}", as_text(word),
                             categories.empty() ? "?" : categories, uses.size(),
                             first.before.empty() ? "^" : as_text(first.before),
                             first.after.empty() ? "$" : as_text(first.after));
            }
            std::println("{} words", larry.memory.words().size());
            return 0;
        }
        const larry::Bytes word = larry.ops.fold(larry::Bytes(rest[0].begin(), rest[0].end()));
        const std::vector<larry::WordUse> uses = larry.memory.uses(word);
        if (uses.empty()) {
            std::println("\"{}\" is not in the vocabulary", rest[0]);
            return 0;
        }
        std::println("{}: {} uses", as_text(word), uses.size());
        for (const larry::CategoryCount& c : larry.memory.categories_of(word)) {
            std::println("  category {}: {} uses", as_text(c.category), c.count);
        }
        std::map<std::string, std::int64_t> types;
        std::map<std::string, std::int64_t> contexts;
        for (const larry::StoredAtom& atom : larry.memory.containing(word)) {
            for (const larry::Entity& e : atom.description.entities.entities) {
                if (larry.ops.fold(e.word) != word) {
                    continue;
                }
                std::string list;
                for (const larry::Bytes& type : e.types) {
                    list += list.empty() ? "" : ", ";
                    list += as_text(type);
                }
                ++types[list];
            }
        }
        for (const larry::WordUse& use : uses) {
            ++contexts[std::format("{} _ {}", use.before.empty() ? "^" : as_text(use.before),
                                   use.after.empty() ? "$" : as_text(use.after))];
        }
        for (const auto& [list, count] : types) {
            std::println("  types {}: {} uses", list, count);
        }
        for (const auto& [context, count] : contexts) {
            std::println("  context {}: {}", context, count);
        }
        std::size_t shown = 0;
        for (const larry::StoredAtom& atom : larry.memory.containing(word)) {
            if (shown++ == 5) {
                std::println("  ...");
                break;
            }
            std::println("  in: {}", larry.ops.text(atom.description.atom));
        }
        return 0;
    }
    if (command == "search") {
        if (rest.empty()) {
            throw std::runtime_error("search needs words");
        }
        const larry::Web web{larry.constellation.language()};
        const std::vector<larry::Hit> hits = web.search(join(rest));
        if (hits.empty()) {
            std::println("nothing found for \"{}\"", join(rest));
            return 0;
        }
        for (const larry::Hit& hit : hits) {
            std::println("{}\n  {}\n  {}", hit.title, hit.snippet, hit.url);
        }
        return 0;
    }
    if (command == "fetch") {
        if (rest.empty()) {
            throw std::runtime_error("fetch needs a title or a url");
        }
        const larry::Web web{larry.constellation.language()};
        const larry::Page page = web.fetch(join(rest), larry.cloud == nullptr);
        const std::vector<larry::Sentence> sentences = larry.assimilation.sentences(page.text);
        if (larry.cloud) {
            larry.cloud->store_page(larry.locale(), page);  // N2f: the cloud keeps it
            std::println("{}: {} bytes, {} sentences, kept in the cloud as {}", page.title, page.text.size(),
                         sentences.size(), larry::Web::slug(page.title));
        } else {
            std::println("{}: {} bytes, {} sentences, kept in {}", page.title, page.text.size(), sentences.size(),
                         page.file.string());
        }
        std::println("source: {}", page.source);
        return 0;
    }
    if (command == "define") {
        if (rest.size() != 1) {
            throw std::runtime_error("define needs one word");
        }
        const larry::Web web{larry.constellation.language()};
        const std::vector<larry::Meaning> meanings = web.define(rest[0]);
        if (meanings.empty()) {
            std::println("Wiktionary has no English entry for \"{}\"", rest[0]);
        }
        for (const larry::Meaning& m : meanings) {
            std::println("{}", as_text(m.category));
            for (const std::string& d : m.definitions) {
                std::println("  {}", d);
            }
        }
        const larry::Bytes folded = larry.ops.fold(larry::Bytes(rest[0].begin(), rest[0].end()));
        std::string known;
        for (const larry::CategoryCount& c : larry.memory.categories_of(folded)) {
            known += known.empty() ? "" : ", ";
            known += std::format("{} ({})", as_text(c.category), c.count);
        }
        std::println("memory: {}", known.empty() ? "no use of the word" : known);
        if (larry.dictionary) {
            std::string listed;
            for (const larry::Bytes& c : larry.dictionary->categories(folded)) {
                listed += listed.empty() ? "" : ", ";
                listed += as_text(c);
            }
            std::println("dictionary: {}", listed.empty() ? "not listed" : listed);
        }
        return 0;
    }
    if (command == "study") {
        if (rest.empty()) {
            throw std::runtime_error("study needs a file, a Wikipedia title or a url");
        }
        const larry::StudyReport report = study(larry, join(rest));
        for (const std::string& line : report.lines()) {
            std::println("{}", line);
        }
        return 0;
    }
    if (command == "molecules") {
        if (!rest.empty()) {
            throw std::runtime_error("molecules takes nothing; molecule <name> shows one");
        }
        const std::vector<larry::Bytes> names = larry.memory.molecules();
        if (names.empty()) {
            std::println("no molecules yet: larry read <file> and larry chat make them");
        }
        for (const larry::Bytes& name : names) {
            const std::optional<larry::Molecule> m = larry.memory.molecule(name);
            std::println("{}  ({} conceptions)", as_text(name), m ? m->members.size() : 0);
        }
        return 0;
    }
    if (command == "molecule") {
        if (rest.size() != 1) {
            throw std::runtime_error("molecule needs one name; molecules lists them");
        }
        const std::optional<larry::Molecule> m =
            larry.brain.molecule_named(larry::Bytes(rest[0].begin(), rest[0].end()));
        if (!m) {
            throw std::runtime_error(std::format("no molecule \"{}\"; molecules lists them", rest[0]));
        }
        std::println("{}  ({} conceptions, in order)", rest[0], m->members.size());
        for (std::size_t i = 0; i < m->members.size(); ++i) {
            const larry::Member& member = m->members[i];
            const std::optional<larry::StoredAtom> held =
                larry.brain.conception_at(larry::BondEnd{larry::BondEnd::Kind::Atom, member.identity});
            std::println("{:>4}  {:<20} {:<22} {}", i, member.who, member.when,
                         held ? std::string{larry.ops.text(held->description.atom)} : "(a conception I do not hold)");
        }
        return 0;
    }
    if (command == "plan") {
        if (rest.empty()) {
            throw std::runtime_error("plan needs a goal");
        }
        const larry::Brain::Plan p = larry.brain.plan(join(rest));
        std::println("{}", p.text());
        for (const larry::StoredAtom& atom : p.because) {
            std::println("  because: {}", larry.ops.text(atom.description.atom));
        }
        return 0;
    }
    if (command == "think") {
        const double seconds = rest.empty() ? 2.0 : std::stod(std::string{rest[0]});
        const larry::Brain::Thought thought = larry.brain.think(seconds);
        std::println("{}", thought.text());
        for (const larry::Bond& b : thought.conflicts_found) {
            const std::optional<larry::StoredAtom> from = larry.brain.conception_at(b.from);
            const std::optional<larry::StoredAtom> to = larry.brain.conception_at(b.to);
            std::println("  conflict: \"{}\" with \"{}\"", from ? std::string{larry.ops.text(from->description.atom)} : "?",
                         to ? std::string{larry.ops.text(to->description.atom)} : "?");
        }
        for (const larry::Brain::Proposal& p : thought.proposals) {
            std::string examples;
            for (const std::string& e : p.examples) {
                examples += (examples.empty() ? "" : ", ") + e;
            }
            if (!p.counter.empty()) {
                std::println("  {}: \"{}\" from {}, stopped by \"{}\"", p.withdrawn ? "withdrawn" : "not proposed", p.sentence, examples, p.counter);
            } else {
                std::println("  {}: \"{}\" from {}", p.stored ? "proposed as an assumption" : "already proposed", p.sentence, examples);
            }
        }
        return 0;
    }
    if (command == "attention") {
        const larry::Brain::Attention a = larry.brain.attention();
        std::println("{}", a.text());
        for (const larry::Brain::Question& q : a.questions) {
            std::println("  ask: {}{}", q.text, q.guess.empty() ? "" : "  (" + q.guess + ")");
        }
        for (const larry::Bond& b : a.conflicts) {
            const std::optional<larry::StoredAtom> from = larry.brain.conception_at(b.from);
            const std::optional<larry::StoredAtom> to = larry.brain.conception_at(b.to);
            std::println("  settle: \"{}\" or \"{}\" (larry validate)", from ? std::string{larry.ops.text(from->description.atom)} : "?",
                         to ? std::string{larry.ops.text(to->description.atom)} : "?");
        }
        for (const larry::StoredAtom& atom : a.proposals) {
            std::println("  decide: \"{}\" ({})", larry.ops.text(atom.description.atom), atom.sources.empty() ? "" : atom.sources.front());
        }
        for (const larry::Brain::NewCategory& c : a.new_categories) {
            std::println("  category: {}", larry.brain.proposal_text(c));  // S6
        }
        return 0;
    }
    if (command == "know") {
        if (rest.size() != 1) {
            throw std::runtime_error("know needs one word");
        }
        const larry::Brain::Knowledge k = larry.brain.knowledge(rest[0]);
        std::println("{}", k.text());
        const auto list = [&](std::string_view status, const std::vector<larry::StoredAtom>& atoms) {
            for (std::size_t i = 0; i < atoms.size() && i < 10; ++i) {
                std::println("  {:<10} {}", status, larry.ops.text(atoms[i].description.atom));
            }
            if (atoms.size() > 10) {
                std::println("  {:<10} and {} more", status, atoms.size() - 10);
            }
        };
        list("validated", k.validated);
        list("proposed", k.proposed);
        list("withdrawn", k.withdrawn);
        for (const larry::Bond& b : k.conflicts) {
            const std::optional<larry::StoredAtom> from = larry.brain.conception_at(b.from);
            const std::optional<larry::StoredAtom> to = larry.brain.conception_at(b.to);
            std::println("  conflict   \"{}\" with \"{}\"", from ? std::string{larry.ops.text(from->description.atom)} : "?",
                         to ? std::string{larry.ops.text(to->description.atom)} : "?");
        }
        for (const larry::Bond& b : k.bonds) {
            std::println("  bond       {} --{}--> {}", as_text(b.from.bytes), as_text(b.kind), as_text(b.to.bytes));
        }
        return 0;
    }
    if (command == "forms") {
        if (rest.size() != 1) {
            throw std::runtime_error("forms needs one word");
        }
        const larry::Bytes word(rest[0].begin(), rest[0].end());
        const std::optional<larry::Form> form = larry.assimilation.form_of(word, &larry.memory);
        if (form) {
            std::println("{} is a form of {}: {} {}, by {}", as_text(form->word), as_text(form->base),
                         as_text(form->category), as_text(form->feature), form->rule);
        } else if (const std::optional<larry::Form> alone = larry.assimilation.forms().by_ending(word)) {
            std::println("{} has the ending of a {} ({}), base unknown: {}", rest[0], as_text(alone->category),
                         as_text(alone->feature), as_text(alone->base));
        } else {
            std::println("{} is a form of nothing I know", rest[0]);
        }
        for (const larry::Form& candidate : larry.assimilation.forms().candidates(word)) {
            std::println("  tried: {} as {} {} ({})", as_text(candidate.base), as_text(candidate.category),
                         as_text(candidate.feature), candidate.rule);
        }
        for (const larry::Bond& b : larry.brain.bonds_of(larry::BondEnd::entity(rest[0]))) {
            std::println("  bond: {} --{}--> {}", as_text(b.from.bytes), as_text(b.kind), as_text(b.to.bytes));
        }
        return 0;
    }
    if (command == "near") {
        if (rest.empty() || rest.size() > 3) {
            throw std::runtime_error("near needs a word or a sentence, then steps and a limit at most");
        }
        const int steps = rest.size() > 1 ? std::stoi(std::string{rest[1]}) : 2;
        const std::size_t limit = rest.size() > 2 ? static_cast<std::size_t>(std::stoul(std::string{rest[2]})) : 20;
        const std::string text{rest[0]};
        const std::vector<larry::Neighbour> found =
            text.find(' ') == std::string::npos
                ? larry.brain.near(text, steps, limit)
                : larry.brain.near(larry.assimilation.describe(larry.ops.from_text(text), &larry.memory), steps, limit);
        if (found.empty()) {
            std::println("nothing near \"{}\" in memory", text);
        }
        for (const larry::Neighbour& n : found) {
            std::string what;
            if (n.end.kind == larry::BondEnd::Kind::Entity) {
                what = "word " + std::string(n.end.bytes.begin(), n.end.bytes.end());
            } else if (const std::optional<larry::StoredAtom> held = larry.brain.conception_at(n.end)) {
                what = "\"" + std::string{larry.ops.text(held->description.atom)} + "\"";
            } else {
                what = "(a conception I do not hold)";
            }
            std::println("{:<44} step {}  shared {}  evidence {}  via {}", what, n.steps, n.shared, n.evidence, n.via);
        }
        return 0;
    }
    if (command == "bonds" || command == "bond") {
        // N3: an end is a word, or a sentence Larry holds as a conception.
        const auto end_of = [&](std::string_view given) -> larry::BondEnd {
            const std::string text{given};
            if (text.find(' ') == std::string::npos) {
                return larry::BondEnd::entity(text);
            }
            const larry::Description d = larry.assimilation.describe(larry.ops.from_text(text), &larry.memory);
            const larry::BondEnd end = larry::BondEnd::atom(d.metadata);
            if (!larry.brain.conception_at(end)) {
                throw std::runtime_error(std::format("I hold no conception \"{}\"; tell it to me first", text));
            }
            return end;
        };
        const auto end_text = [&](const larry::BondEnd& end) -> std::string {
            if (end.kind == larry::BondEnd::Kind::Entity) {
                return std::string(end.bytes.begin(), end.bytes.end());
            }
            if (const std::optional<larry::StoredAtom> held = larry.brain.conception_at(end)) {
                return "\"" + std::string{larry.ops.text(held->description.atom)} + "\"";
            }
            return "(a conception I do not hold)";
        };
        const auto show = [&](const larry::Bond& b) {
            std::string origins;
            for (const std::string& origin : b.origins) {
                origins += origins.empty() ? origin : ", " + origin;
            }
            std::println("{} --{}--> {}{}", end_text(b.from), as_text(b.kind), end_text(b.to),
                         origins.empty() ? "" : "  (" + origins + ")");
        };
        if (command == "bond") {
            if (rest.size() != 3) {
                throw std::runtime_error("bond needs <from> <kind> <to>");
            }
            const larry::Bond b{larry::Bytes(rest[1].begin(), rest[1].end()), end_of(rest[0]), end_of(rest[2]),
                                {"user:" + larry.user}};
            const bool fresh = larry.brain.bond(b);
            show(b);
            std::println("{}", fresh ? "recorded" : "already there; your origin added");
            return 0;
        }
        if (rest.size() > 1) {
            throw std::runtime_error("bonds takes nothing, a word or a sentence in quotes");
        }
        const std::vector<larry::Bond> list =
            rest.empty() ? larry.memory.bonds() : larry.brain.bonds_of(end_of(rest[0]));
        if (list.empty()) {
            std::println("no bonds{}", rest.empty() ? "" : " at " + std::string{rest[0]});
        }
        for (const larry::Bond& b : list) {
            show(b);
        }
        return 0;
    }
    if (command == "count") {
        std::println("{} conceptions, {} word uses, {} bonds, {} molecules, in {}", larry.memory.count(),
                     larry.memory.count_words(), larry.memory.count_bonds(), larry.memory.count_molecules(),
                     larry.memory.file().string());
        if (larry.cloud) {
            const std::int64_t in_cloud = larry.cloud->count();
            std::println("{} conceptions, {} word uses, {} bonds, {} molecules, in the cloud", in_cloud,
                         larry.cloud->count_words(), larry.cloud->count_bonds(), larry.cloud->count_molecules());
            if (in_cloud < larry.memory.count()) {
                std::println("the cloud may lack some of the cache's conceptions: larry sync pushes them");
            }
        } else {
            std::println("no cloud: set LARRY_DB to reach one");
        }
        return 0;
    }
    throw std::runtime_error(std::format("unknown command \"{}\"; try larry help", command));
}

}  // namespace

int main(int argc, char** argv) {
    read_env_file(".env");
    read_env_file(std::filesystem::path{LARRY_ROOT_DIR} / ".env");
    std::vector<std::string_view> args;
    for (int i = 1; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }
    try {
        return run(args);
    } catch (const std::exception& e) {
        std::println(stderr, "larry: {}", e.what());
        return 1;
    }
}
