#include "larry/measure.hpp"

#include "larry/atom_operations.hpp"
#include "larry/memory.hpp"

#include <algorithm>
#include <chrono>
#include <format>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace larry {

namespace {

std::vector<std::string> split_tabs(const std::string& line) {
    std::vector<std::string> out;
    std::string current;
    for (const char c : line) {
        if (c == '\t') {
            out.push_back(current);
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    out.push_back(current);
    return out;
}

bool all_digits(std::string_view s) {
    return !s.empty() && std::ranges::all_of(s, [](char c) { return c >= '0' && c <= '9'; });
}

std::string without_spaces(std::string_view s) {
    std::string out;
    for (const char c : s) {
        if (c != ' ') {
            out.push_back(c);
        }
    }
    return out;
}

}  // namespace

std::vector<UdSentence> read_conllu(const std::filesystem::path& file) {
    std::ifstream in{file};
    if (!in) {
        throw std::runtime_error(std::format("measure: cannot read {}", file.string()));
    }
    std::vector<UdSentence> out;
    UdSentence current;
    const auto flush = [&] {
        if (!current.tokens.empty()) {
            out.push_back(std::move(current));
        }
        current = UdSentence{};
    };
    for (std::string line; std::getline(in, line);) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            flush();
            continue;
        }
        if (line.starts_with("# text = ")) {
            current.text = line.substr(9);
            continue;
        }
        if (line.starts_with('#')) {
            continue;
        }
        const std::vector<std::string> fields = split_tabs(line);
        if (fields.size() < 4 || !all_digits(fields[0])) {
            continue;  // a range "1-2", an empty node "1.1", or not a token
        }
        current.tokens.push_back({fields[1], fields[3]});
    }
    flush();
    return out;
}

Bytes category_for(std::string_view upos) {
    static const std::map<std::string_view, std::string_view> table = {
        {"NOUN", "noun"},        {"PROPN", "proper noun"},  {"VERB", "verb"},      {"AUX", "auxiliary verb"},
        {"ADJ", "adjective"},    {"ADV", "adverb"},         {"ADP", "preposition"}, {"DET", "determiner"},
        {"PRON", "pronoun"},     {"NUM", "numeral"},        {"CCONJ", "conjunction"}, {"SCONJ", "conjunction"},
        {"INTJ", "interjection"}, {"PART", "particle"},
    };
    const auto found = table.find(upos);
    if (found == table.end()) {
        return {};
    }
    return Bytes(found->second.begin(), found->second.end());
}

std::optional<Aligned> align(const UdSentence& sentence, const Assimilation& assimilation) {
    if (sentence.text.empty()) {
        return std::nullopt;
    }
    const std::vector<Sentence> atoms = assimilation.sentences(sentence.text);
    if (atoms.size() != 1) {
        return std::nullopt;  // Larry cuts it elsewhere: not comparable
    }
    Aligned out{atoms.front(), {}};
    const EntitiesElectron entities = assimilation.entities(out.atom);
    std::size_t j = 0;
    for (const Entity& entity : entities.entities) {
        const std::string wanted = without_spaces(std::string_view{reinterpret_cast<const char*>(entity.word.data()), entity.word.size()});
        // Standalone punctuation before the entity is skipped.
        while (j < sentence.tokens.size() && sentence.tokens[j].upos == "PUNCT" && !wanted.starts_with(sentence.tokens[j].form)) {
            ++j;
        }
        bool matched = false;
        std::string run;
        for (std::size_t k = j; k < sentence.tokens.size() && k < j + 8; ++k) {
            run += sentence.tokens[k].form;
            if (run == wanted) {
                // The category of the first token that is no punctuation.
                Bytes category;
                for (std::size_t t = j; t <= k && category.empty(); ++t) {
                    if (sentence.tokens[t].upos != "PUNCT") {
                        category = category_for(sentence.tokens[t].upos);
                    }
                }
                if (category.empty()) {
                    return std::nullopt;
                }
                out.categories.push_back(std::move(category));
                j = k + 1;
                matched = true;
                break;
            }
            if (run.size() >= wanted.size()) {
                break;
            }
        }
        if (!matched) {
            return std::nullopt;
        }
    }
    return out;
}

std::string Score::text() const {
    return std::format("taught {} sentences ({} words): {:.1f}% of {} test words right, {:.1f}% unknown, "
                       "{} test sentences ({} not aligned), in {:.1f} s",
                       taught, taught_words, accuracy() * 100.0, scored,
                       scored == 0 ? 0.0 : 100.0 * static_cast<double>(unknown) / static_cast<double>(scored),
                       sentences, skipped, seconds);
}

std::vector<Score> measure(const BaseRules& rules, const std::vector<UdSentence>& train,
                           const std::vector<UdSentence>& test, const std::vector<std::int64_t>& counts,
                           const std::filesystem::path& scratch, const Dictionary* dictionary,
                           const std::function<void(std::string_view)>& progress) {
    using Clock = std::chrono::steady_clock;
    const AtomOperations ops;
    const Assimilation assimilation{rules, dictionary};
    std::vector<Aligned> teaching;
    for (const UdSentence& s : train) {
        if (std::optional<Aligned> a = align(s, assimilation)) {
            teaching.push_back(std::move(*a));
        }
    }
    std::vector<Aligned> testing;
    std::int64_t skipped = 0;
    for (const UdSentence& s : test) {
        if (std::optional<Aligned> a = align(s, assimilation)) {
            testing.push_back(std::move(*a));
        } else {
            ++skipped;
        }
    }
    std::vector<Score> out;
    for (const std::int64_t count : counts) {
        const Clock::time_point start = Clock::now();
        const auto n = std::min<std::size_t>(static_cast<std::size_t>(std::max<std::int64_t>(count, 0)), teaching.size());
        if (progress) {
            progress(std::format("teaching {} sentences, then {} test sentences", n, testing.size()));
        }
        std::filesystem::remove(scratch);
        Memory memory{scratch};
        Score score;
        score.taught = static_cast<std::int64_t>(n);
        for (std::size_t i = 0; i < n; ++i) {
            const Aligned& a = teaching[i];
            const Description d = assimilation.describe(a.atom, &memory, a.categories);
            memory.store(d.atom, d.metadata, Status::Proposed, "ud:train");
            score.taught_words += static_cast<std::int64_t>(a.categories.size());
        }
        for (const Aligned& a : testing) {
            const Description d = assimilation.describe(a.atom, &memory);
            ++score.sentences;
            for (std::size_t i = 0; i < a.categories.size() && i < d.entities.entities.size(); ++i) {
                Bytes pick = d.entities.entities[i].category;
                if (pick.empty() && i < d.notes.size() && d.notes[i].source == Source::Open &&
                    !d.notes[i].candidates.empty()) {
                    // Nothing chose (a tie): the most used of the categories memory gives it.
                    std::int64_t most = -1;
                    for (const CategoryCount& c : memory.categories_of(ops.fold(d.entities.entities[i].word))) {
                        if (c.count > most && std::ranges::contains(d.notes[i].candidates, c.category)) {
                            most = c.count;
                            pick = c.category;
                        }
                    }
                    if (most < 0) {
                        pick = d.notes[i].candidates.front();  // the dictionary's first
                    }
                }
                ++score.scored;
                if (pick.empty()) {
                    ++score.unknown;
                } else if (pick == a.categories[i]) {
                    ++score.correct;
                }
            }
        }
        score.skipped = skipped;
        score.seconds = std::chrono::duration<double>(Clock::now() - start).count();
        out.push_back(score);
        if (progress) {
            progress(score.text());
        }
    }
    std::filesystem::remove(scratch);
    return out;
}

}  // namespace larry
