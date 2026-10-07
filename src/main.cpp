#include "larry/assimilation.hpp"
#include "larry/atom_operations.hpp"
#include "larry/base_rules.hpp"
#include "larry/brain.hpp"
#include "larry/cognition.hpp"
#include "larry/constellation.hpp"
#include "larry/description.hpp"
#include "larry/dictionary.hpp"
#include "larry/electron.hpp"
#include "larry/lesson.hpp"
#include "larry/memory.hpp"

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <print>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::string_view usage = R"(usage: larry <command> [arguments]

  show <text>...               describe a sentence without storing it
  tell <text> [category ...]   describe a sentence and store it as a
                               conception; with the category of each entity,
                               in order, it is taught
  read <file>                  tell every sentence in a text file
  teach <file>                 tell every lesson in a lesson file: a sentence,
                               then the category of each entity, in order
  rebuild                      empty memory and teach every lesson in
                               lessons/<locale>/, in name order
  compare <text> <text>        compare two sentences: C1 to C5
  ask <text>                   is a concept true, false or unknown, from the
                               conceptions in memory; a yes/no question works
  say <text>                   hear one sentence and reply: an affirmation is
                               stored, a question answered, an order refused,
                               an assumption noted, an expression returned
  chat                         hear a line at a time from standard input;
                               "why?" explains the last reply, "bye" ends
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
  count                        how many conceptions and word uses memory holds

Memory, the cache on this machine, is the file LARRY_MEMORY names, or
memory/<locale>.atoms. When the file does not exist yet, Larry rebuilds it
from the lessons first. The cloud, the record of conceptions, is the
PostgreSQL server LARRY_DB names ("host=... dbname=larry user=..."); without
it, Larry has only the cache. LARRY_USER names who is talking (default: the
login name); only a validator decides what is true.
)";

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

struct Larry {
    std::string user{user_name()};
    larry::Constellation constellation{larry::Language::English};
    larry::BaseRules rules{constellation.language()};
    std::unique_ptr<larry::Dictionary> dictionary{open_dictionary(constellation.language())};
    larry::Assimilation assimilation{rules, dictionary.get()};
    larry::Cognition cognition;
    larry::AtomOperations ops;
    larry::Memory memory{larry::Memory::file_from_environment(constellation.language())};
    std::unique_ptr<larry::Database> cloud;
    larry::Brain brain{rules, memory, nullptr, dictionary.get()};

    Larry() {
        const std::string connection = larry::Database::connection_from_environment();
        if (!connection.empty()) {
            try {
                cloud = larry::Database::open(connection);
            } catch (const std::exception& e) {
                std::println(stderr, "larry: no cloud: {}", e.what());
            }
        }
        brain = larry::Brain{rules, memory, cloud.get(), dictionary.get()};
        if (!std::filesystem::exists(memory.file())) {
            std::println(stderr, "larry: no memory at {}; rebuilding it from the lessons",
                         memory.file().string());
            rebuild(false);
        }
    }

    /// Stores every lesson in a lesson file.
    Tally teach(const std::filesystem::path& file, bool verbose) {
        Tally tally;
        for (const larry::Lesson& lesson : larry::read_lessons(file)) {
            larry::Description d;
            try {
                d = assimilation.describe(ops.from_text(lesson.sentence), &memory, lesson.categories);
            } catch (const std::invalid_argument& e) {
                throw std::runtime_error(
                    std::format("{} line {}: {}", file.string(), lesson.line, e.what()));
            }
            const larry::Stored stored =
                brain.remember(d, larry::Status::Proposed, "lesson:" + file.filename().string());
            tally.add(stored);
            if (verbose) {
                std::println("{:<10} {}", stored_name(stored), lesson.sentence);
            }
        }
        return tally;
    }

    /// Empties memory and teaches every lesson file, in name order (F7).
    void rebuild(bool verbose) {
        memory.clear();
        Tally total;
        const std::vector<std::filesystem::path> files =
            larry::lesson_files(constellation.language());
        for (const std::filesystem::path& file : files) {
            const Tally tally = teach(file, false);
            if (verbose) {
                std::println("{:<40} {} lessons, {} stored", file.filename().string(),
                             tally.sentences, tally.stored);
            }
            total.add(tally);
        }
        if (verbose) {
            std::println("{} lesson files", files.size());
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
        return out;
    }
    case larry::Source::Guess: {
        std::string out = "guess from the context:";
        for (const larry::Bytes& candidate : note.candidates) {
            out += ' ';
            out += as_text(candidate);
        }
        return out;
    }
    case larry::Source::Dictionary:
        return "dictionary";
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

int run(std::span<const std::string_view> args) {
    if (args.empty() || args[0] == "help" || args[0] == "--help" || args[0] == "-h") {
        std::print("{}", usage);
        return args.empty() ? 1 : 0;
    }
    const std::string_view command = args[0];
    const std::span<const std::string_view> rest = args.subspan(1);
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
        const std::string text = read_file(rest[0]);
        Tally tally;
        const std::string source = "read:" + std::filesystem::path{rest[0]}.filename().string();
        for (const larry::Sentence& sentence : larry.assimilation.sentences(text)) {
            const larry::Description d = larry.assimilation.describe(sentence, &larry.memory);
            const larry::Stored stored = larry.brain.remember(d, larry::Status::Proposed, source);
            tally.add(stored);
            std::println("{:<10} {}{}", stored_name(stored), larry.ops.text(sentence),
                         open_words(d));
        }
        tally.print();
        return 0;
    }
    if (command == "teach") {
        if (rest.size() != 1) {
            throw std::runtime_error("teach needs one lesson file");
        }
        larry.teach(rest[0], true).print();
        return 0;
    }
    if (command == "rebuild") {
        larry.rebuild(true);
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
            throw std::runtime_error("ask needs a sentence");
        }
        const larry::Verdict verdict = larry.brain.truth(larry.ops.from_text(join(rest)));
        switch (verdict.truth) {
        case larry::Truth::True:
            std::println("true");
            break;
        case larry::Truth::False:
            std::println("false");
            break;
        case larry::Truth::Unknown:
            std::println("I don't know");
            break;
        }
        for (const larry::StoredAtom& atom : verdict.because) {
            std::println("because: {}{}{}", larry.ops.text(atom.description.atom),
                         verdict.from_cloud ? " (from the cloud)" : "",
                         atom.status == larry::Status::Proposed
                             ? " (proposed)"
                             : atom.decided_by.empty()
                                   ? ""
                                   : std::format(" (validated by {})", atom.decided_by));
        }
        for (const larry::StoredAtom& atom : verdict.nearest) {
            std::println("I know: {}", larry.ops.text(atom.description.atom));
        }
        return 0;
    }
    if (command == "say") {
        if (rest.empty()) {
            throw std::runtime_error("say needs a sentence");
        }
        const larry::Reply reply =
            larry.brain.hear(larry.ops.from_text(join(rest)), "user:" + larry.user);
        std::println("{}", reply.text);
        for (const std::string& because : reply.because) {
            std::println("  because: {}", because);
        }
        return 0;
    }
    if (command == "chat") {
        larry::Brain& brain = larry.brain;
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
                    std::println("Larry: bye.");
                    return 0;
                }
                last = brain.hear(sentence, "user:" + larry.user);
                std::println("Larry: {}", last.text);
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
            return std::format("{:>6}  {}  [{}]", atom.id, larry.ops.text(atom.description.atom),
                               sources.empty() ? "no source" : sources);
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
        const auto [pushed, pulled] = larry.brain.sync(pull);
        std::println("{} conceptions pushed to the cloud, {} pulled into the cache", pushed, pulled);
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
    if (command == "count") {
        std::println("{} conceptions, {} word uses, in {}", larry.memory.count(),
                     larry.memory.count_words(), larry.memory.file().string());
        if (larry.cloud) {
            std::println("{} conceptions, {} word uses, in the cloud", larry.cloud->count(),
                         larry.cloud->count_words());
        } else {
            std::println("no cloud: set LARRY_DB to reach one");
        }
        return 0;
    }
    throw std::runtime_error(std::format("unknown command \"{}\"; try larry help", command));
}

}  // namespace

int main(int argc, char** argv) {
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
