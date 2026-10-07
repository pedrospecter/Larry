#include "larry/assimilation.hpp"
#include "larry/atom_operations.hpp"
#include "larry/base_rules.hpp"
#include "larry/brain.hpp"
#include "larry/cognition.hpp"
#include "larry/constellation.hpp"
#include "larry/description.hpp"
#include "larry/electron.hpp"
#include "larry/lesson.hpp"
#include "larry/memory.hpp"

#include <cstdio>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
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
  count                        how many conceptions and word uses memory holds

Memory is the file LARRY_MEMORY names, or memory/<locale>.atoms. When the
file does not exist yet, Larry rebuilds it from the lessons first.
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

struct Larry {
    larry::Constellation constellation{larry::Language::English};
    larry::BaseRules rules{constellation.language()};
    larry::Assimilation assimilation{rules};
    larry::Cognition cognition;
    larry::AtomOperations ops;
    larry::Memory memory{larry::Memory::file_from_environment(constellation.language())};

    Larry() {
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
            const larry::Stored stored = memory.store(d.atom, d.metadata);
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
    case larry::Source::Unknown:
        return "unknown";
    case larry::Source::Open: {
        std::string out = "open:";
        for (const larry::Bytes& candidate : note.candidates) {
            out += ' ';
            out += as_text(candidate);
        }
        return out;
    }
    }
    return "";
}

void print(const Larry& larry, const larry::Description& d) {
    std::println("constellation : {}", name(larry.constellation.language()));
    std::println("base rules    : {} categories", larry.rules.categories().size());
    std::println("memory        : {} conceptions", larry.memory.count());
    std::println("atom          : {} bits", d.atom.size());
    std::println("                {}", larry.ops.to_bits(d.atom));
    std::println("category      : {} ({} bytes)", as_text(d.category.bytes),
                 d.category.bytes.size());
    std::println("type          : {} bytes", d.type.bytes.size());
    std::println("entities      : {}", d.entities.entities.size());
    for (std::size_t i = 0; i < d.entities.entities.size(); ++i) {
        const larry::Entity& entity = d.entities.entities[i];
        const std::string category =
            entity.category.empty() ? std::string{"?"} : std::string{as_text(entity.category)};
        const std::string from = i < d.notes.size() ? source(d.notes[i]) : std::string{};
        std::println("  {:<14} category: {:<15} types: {}  {}", as_text(entity.word), category,
                     entity.types.size(), from);
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
            d.notes[i].source == larry::Source::Open) {
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
        const larry::Stored stored = larry.memory.store(d.atom, d.metadata);
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
        for (const larry::Sentence& sentence : larry.assimilation.sentences(text)) {
            const larry::Description d = larry.assimilation.describe(sentence, &larry.memory);
            const larry::Stored stored = larry.memory.store(d.atom, d.metadata);
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
        const larry::Brain brain{larry.rules, larry.memory};
        const larry::Verdict verdict = brain.truth(larry.ops.from_text(join(rest)));
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
            std::println("because: {}", larry.ops.text(atom.description.atom));
        }
        for (const larry::StoredAtom& atom : verdict.nearest) {
            std::println("I know: {}", larry.ops.text(atom.description.atom));
        }
        return 0;
    }
    if (command == "count") {
        std::println("{} conceptions, {} word uses, in {}", larry.memory.count(),
                     larry.memory.count_words(), larry.memory.file().string());
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
