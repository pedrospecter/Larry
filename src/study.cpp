#include "larry/study.hpp"

#include "larry/atom_operations.hpp"
#include "larry/brain.hpp"
#include "larry/web.hpp"

#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <format>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace larry {

namespace {

std::string text_of(const Bytes& bytes) {
    return std::string(bytes.begin(), bytes.end());
}

std::string today() {
    const std::time_t now = std::time(nullptr);
    std::tm parts{};
#ifdef _WIN32
    gmtime_s(&parts, &now);
#else
    gmtime_r(&now, &parts);
#endif
    return std::format("{:04}-{:02}-{:02}", parts.tm_year + 1900, parts.tm_mon + 1, parts.tm_mday);
}

}  // namespace

std::vector<std::string> StudyReport::lines() const {
    std::vector<std::string> out;
    out.push_back(std::format("{}: {} sentences: {} facts, {} context, {} questions, {} instructions, {} speech, "
                              "{} headings, {} references, {} fragments",
                              title, classes.total(), classes.facts, classes.context, classes.questions,
                              classes.instructions, classes.speech, classes.headings, classes.references,
                              classes.fragments));
    out.push_back(std::format("conceptions: {} proposed, {} known already; {} read as meant; {} with something unusual",
                              proposed, known, read_as_meant, unusual));
    std::string list;
    for (std::size_t i = 0; i < words.size() && i < 12; ++i) {
        list += (i == 0 ? "" : ", ") + words[i].word + " (" + words[i].category + ")";
    }
    if (words.size() > 12) {
        list += std::format(" and {} more", words.size() - 12);
    }
    out.push_back(std::format("words to learn: {}{}", words.size(), words.empty() ? "" : ": " + list));
    if (!draft.empty()) {
        out.push_back(std::format("draft lesson: {} (correct it, then: larry teach {})", draft.string(),
                                  draft.string()));
    }
    return out;
}

Study::Study(const BaseRules& rules, const Content& content) : rules_(&rules), content_(&content) {}

std::filesystem::path Study::drafts_directory(Language language) {
    const char* const from_environment = std::getenv("LARRY_DRAFTS_DIR");
    if (from_environment != nullptr && *from_environment != '\0') {
        return from_environment;
    }
    return std::filesystem::path{LARRY_LESSONS_DIR} / locale(language) / "drafts";
}

StudyReport Study::study(std::string_view text, std::string_view name, std::string_view source, Brain& brain,
                         const std::filesystem::path& drafts) const {
    const AtomOperations ops;
    StudyReport report;
    report.title = std::string{name};
    report.source = std::string{source};
    const std::string from = "study:" + Web::slug(name);

    struct Draft {
        std::string sentence;
        std::vector<std::string> categories;
        std::vector<std::string> to_check;
    };
    std::vector<Draft> draft_lines;
    std::map<std::string, WordToLearn> words;

    for (const Piece& piece : content_->classify(text, brain)) {
        report.classes.add(piece.what);
        if (piece.what != ContentClass::Fact) {
            continue;
        }
        const Description& d = piece.description;
        report.facts.emplace_back(ops.text(piece.sentence));
        // The reading and the harness, as larry say would have them.
        const Reading reading = brain.read(d);
        if (reading.changed) {
            ++report.read_as_meant;
        }
        const Description& meant = reading.changed ? reading.meant : d;
        if (!brain.judge(meant).unusual().empty()) {
            ++report.unusual;
        }
        const std::string read_as = reading.changed ? std::string{ops.text(meant.atom)} : std::string{};
        if (brain.remember(d, Status::Proposed, from, read_as) == Stored::New) {
            ++report.proposed;
        } else {
            ++report.known;
        }
        // The words to learn, and the draft lines.
        Draft line;
        line.sentence = std::string{ops.text(piece.sentence)};
        for (std::size_t i = 0; i < d.entities.entities.size(); ++i) {
            const Entity& e = d.entities.entities[i];
            const std::string word = text_of(e.word);
            const std::string category = e.category.empty() ? "?" : text_of(e.category);
            line.categories.push_back(category);
            const Source where = i < d.notes.size() ? d.notes[i].source : Source::Unknown;
            if (where == Source::Memory || where == Source::Taught) {
                continue;
            }
            std::string how;
            switch (where) {
            case Source::Guess:
                how = "guessed from context";
                break;
            case Source::Rule:
            case Source::Dictionary:
                how = "the dictionary";
                break;
            case Source::Open: {
                how = "open:";
                for (const Bytes& c : d.notes[i].candidates) {
                    how += " " + text_of(c);
                }
                break;
            }
            default:
                how = "unknown";
                break;
            }
            line.to_check.push_back(std::format("{} ({}, {})", word, category, how));
            const std::string key = text_of(ops.fold(e.word));
            auto held = words.find(key);
            if (held == words.end()) {
                held = words.emplace(key, WordToLearn{key, category, how, 0}).first;
            }
            ++held->second.uses;
        }
        draft_lines.push_back(std::move(line));
    }
    for (auto& [key, word] : words) {
        report.words.push_back(std::move(word));
    }
    std::ranges::stable_sort(report.words, [](const WordToLearn& a, const WordToLearn& b) {
        return a.uses > b.uses;
    });

    // The draft, in the lesson format, with the words to check above each sentence.
    if (!draft_lines.empty()) {
        std::filesystem::create_directories(drafts);
        report.draft = drafts / (Web::slug(name) + ".txt");
        std::ofstream out{report.draft, std::ios::binary | std::ios::trunc};
        if (!out) {
            throw std::runtime_error(std::format("Study: cannot write {}", report.draft.string()));
        }
        out << "# Draft lesson from \"" << name << "\"";
        if (!source.empty()) {
            out << " (" << source << ")";
        }
        out << ", studied " << today() << " by Larry: " << draft_lines.size() << " facts of "
            << report.classes.total() << " sentences.\n"
            << "# Check the categories marked ? or noted as guessed, open or from the dictionary; then\n"
            << "# teach the file (larry teach <file>) or move it into lessons/" << locale(rules_->language())
            << "/.\n";
        if (!report.words.empty()) {
            out << "# Words to learn (" << report.words.size() << ", most used first):";
            for (std::size_t i = 0; i < report.words.size() && i < 40; ++i) {
                out << (i == 0 ? " " : ", ") << report.words[i].word << " (" << report.words[i].category << ", "
                    << report.words[i].from << ")";
            }
            if (report.words.size() > 40) {
                out << " and " << report.words.size() - 40 << " more, each noted above its sentence";
            }
            out << "\n";
        }
        for (const Draft& line : draft_lines) {
            out << "\n";
            if (!line.to_check.empty()) {
                out << "# check:";
                for (std::size_t i = 0; i < line.to_check.size(); ++i) {
                    out << (i == 0 ? " " : "; ") << line.to_check[i];
                }
                out << "\n";
            }
            out << line.sentence << "\n";
            for (std::size_t i = 0; i < line.categories.size(); ++i) {
                out << (i == 0 ? "" : ", ") << line.categories[i];
            }
            out << "\n";
        }
    }
    return report;
}

}  // namespace larry
