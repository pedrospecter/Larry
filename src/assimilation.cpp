#include "larry/assimilation.hpp"

#include "larry/atom_operations.hpp"
#include "larry/cognition.hpp"
#include "larry/dictionary.hpp"
#include "larry/grammar.hpp"
#include "larry/memory.hpp"
#include "larry/utf8.hpp"

#include <algorithm>
#include <limits>
#include <array>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace larry {

namespace {

using utf8::is_ascii_digit;
using utf8::is_ascii_lower;
using utf8::is_ascii_upper;
using utf8::is_space;
using utf8::last_unit_begin;
using utf8::unit_at;
using utf8::unit_size;

constexpr std::size_t npos = std::numeric_limits<std::size_t>::max();

std::vector<std::string> sorted(const std::vector<Bytes>& items) {
    std::vector<std::string> out;
    out.reserve(items.size());
    for (const Bytes& item : items) {
        out.emplace_back(item.begin(), item.end());
    }
    std::ranges::sort(out);
    return out;
}

bool contains(const std::vector<std::string>& set, std::string_view item) noexcept {
    return std::ranges::binary_search(set, item);
}

std::string fold(std::string_view text) {
    std::string out{text};
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return out;
}

// "e.g.", "u.s.a.": letters separated by full stops, each one unit long.
bool is_initialism(std::string_view folded) {
    if (folded.size() < 4 || folded.back() != '.') {
        return false;
    }
    std::size_t segments = 0;
    std::size_t i = 0;
    while (i < folded.size()) {
        const std::size_t dot = folded.find('.', i);
        if (dot == std::string_view::npos) {
            return false;
        }
        const std::string_view segment = folded.substr(i, dot - i);
        if (segment.empty() || unit_size(segment, 0) != segment.size() ||
            is_ascii_digit(segment)) {
            return false;
        }
        ++segments;
        i = dot + 1;
    }
    return segments >= 2;
}

}  // namespace

Assimilation::Assimilation(const BaseRules& rules, const Dictionary* dictionary,
                           const Grammar* grammar)
    : rules_(&rules), forms_(rules), dictionary_(dictionary), grammar_(grammar), punctuation_(sorted(rules.punctuation())),
      sentence_ends_(sorted(rules.sentence_ends())), closers_(sorted(rules.closers())),
      joiners_(sorted(rules.joiners())), number_joiners_(sorted(rules.number_joiners())),
      abbreviations_(sorted(rules.abbreviations())), titles_(sorted(rules.titles())) {}

bool Assimilation::is_word_unit(std::string_view unit) const noexcept {
    return !is_space(unit) && !contains(punctuation_, unit);
}

bool Assimilation::is_abbreviation(std::string_view folded) const noexcept {
    return contains(abbreviations_, folded) || is_initialism(folded);
}

std::vector<Assimilation::Span> Assimilation::tokens(std::string_view s) const {
    const std::size_t n = s.size();
    // "e.g.", "U.S.A.": two or more letters each followed by a full stop, then
    // no word unit. Gives the end of the initialism, or npos.
    const auto initialism_end = [&](std::size_t begin) {
        std::size_t i = begin;
        std::size_t letters = 0;
        while (i < n) {
            const std::string_view u = unit_at(s, i);
            if (!is_word_unit(u) || is_ascii_digit(u) || i + u.size() >= n ||
                s[i + u.size()] != '.') {
                break;
            }
            ++letters;
            i += u.size() + 1;
        }
        if (letters < 2 || (i < n && is_word_unit(unit_at(s, i)))) {
            return npos;
        }
        return i;
    };
    std::vector<Span> out;
    std::size_t i = 0;
    while (i < n) {
        const std::string_view u = unit_at(s, i);
        if (!is_word_unit(u)) {
            i += u.size();
            continue;
        }
        const std::size_t begin = i;
        const std::size_t initialism = initialism_end(begin);
        if (initialism != npos) {
            out.push_back({begin, initialism});
            i = initialism;
            continue;
        }
        i += u.size();
        while (i < n) {
            const std::string_view v = unit_at(s, i);
            if (is_word_unit(v)) {
                i += v.size();
                continue;
            }
            if (is_space(v)) {
                break;
            }
            const std::size_t after = i + v.size();
            const std::string_view next = after < n ? unit_at(s, after) : std::string_view{};
            const std::size_t prev_begin = last_unit_begin(s, begin, i);
            const std::string_view prev = s.substr(prev_begin, i - prev_begin);
            const bool next_word = !next.empty() && is_word_unit(next);
            if (contains(joiners_, v) && next_word) {
                i = after;
                continue;
            }
            if (contains(number_joiners_, v) && next_word && is_ascii_digit(next) &&
                is_ascii_digit(prev)) {
                i = after;
                continue;
            }
            if (v == "." && !next_word &&
                contains(abbreviations_, fold(s.substr(begin, after - begin)))) {
                i = after;
            }
            break;
        }
        out.push_back({begin, i});
    }
    return out;
}

std::vector<Assimilation::Span>
Assimilation::join_names(std::string_view s, const std::vector<Span>& tokens) const {
    const auto capitalized = [&](const Span& t) {
        if (!is_ascii_upper(unit_at(s, t.begin))) {
            return false;
        }
        for (std::size_t i = t.begin; i < t.end; i += unit_size(s, i)) {
            if (is_ascii_lower(unit_at(s, i))) {
                return true;
            }
        }
        return false;
    };
    const auto only_spaces = [&](std::size_t from, std::size_t to) {
        for (std::size_t i = from; i < to; i += unit_size(s, i)) {
            if (!is_space(unit_at(s, i))) {
                return false;
            }
        }
        return true;
    };
    std::vector<Span> out;
    std::size_t k = 0;
    while (k < tokens.size()) {
        if (k > 0 && capitalized(tokens[k])) {
            std::size_t j = k;
            while (j + 1 < tokens.size() && capitalized(tokens[j + 1]) &&
                   only_spaces(tokens[j].end, tokens[j + 1].begin)) {
                ++j;
            }
            if (j > k) {
                out.push_back({tokens[k].begin, tokens[j].end});
                k = j + 1;
                continue;
            }
        }
        out.push_back(tokens[k]);
        ++k;
    }
    return out;
}

EntitiesElectron Assimilation::entities(const Sentence& atom) const {
    const AtomOperations ops;
    const std::string_view sentence = ops.text(atom);
    EntitiesElectron out;
    for (const Span& span : join_names(sentence, tokens(sentence))) {
        Entity entity;
        bool in_space = false;
        for (std::size_t i = span.begin; i < span.end;) {
            const std::string_view u = unit_at(sentence, i);
            if (is_space(u)) {
                if (!in_space) {
                    entity.word.push_back(' ');
                }
                in_space = true;
            } else {
                in_space = false;
                for (const char c : u) {
                    entity.word.push_back(static_cast<std::uint8_t>(c));
                }
            }
            i += u.size();
        }
        out.entities.push_back(std::move(entity));
    }
    return out;
}

std::vector<Sentence> Assimilation::sentences(std::string_view text) const {
    const AtomOperations ops;
    std::vector<Sentence> out;
    const std::size_t n = text.size();

    const auto push = [&](std::size_t start, std::size_t end) {
        while (end > start) {
            const std::size_t p = last_unit_begin(text, start, end);
            if (!is_space(text.substr(p, end - p))) {
                break;
            }
            end = p;
        }
        const std::string_view sentence = text.substr(start, end - start);
        for (std::size_t i = 0; i < sentence.size(); i += unit_size(sentence, i)) {
            if (is_word_unit(unit_at(sentence, i))) {
                out.push_back(ops.from_text(sentence));
                return;
            }
        }
    };

    std::size_t start = npos;
    std::size_t i = 0;
    while (i < n) {
        const std::string_view u = unit_at(text, i);
        if (start == npos) {
            if (is_space(u)) {
                i += u.size();
                continue;
            }
            start = i;
        }
        if (u == "\n") {
            std::size_t j = i + 1;
            while (j < n && (text[j] == ' ' || text[j] == '\t' || text[j] == '\r')) {
                ++j;
            }
            if (j < n && text[j] == '\n') {
                push(start, i);
                start = npos;
                i = j + 1;
                continue;
            }
            i += 1;
            continue;
        }
        if (!contains(sentence_ends_, u)) {
            i += u.size();
            continue;
        }
        // A run of end marks, then closing quotes and brackets.
        std::size_t j = i;
        std::size_t marks = 0;
        bool only_stops = true;
        while (j < n) {
            const std::string_view m = unit_at(text, j);
            if (!contains(sentence_ends_, m)) {
                break;
            }
            if (m != ".") {
                only_stops = false;
            }
            ++marks;
            j += m.size();
        }
        std::size_t k = j;
        while (k < n && contains(closers_, unit_at(text, k))) {
            k += unit_size(text, k);
        }
        if (k < n && !is_space(unit_at(text, k))) {
            i = j;
            continue;
        }
        std::size_t q = k;
        while (q < n && is_space(unit_at(text, q))) {
            q += unit_size(text, q);
        }
        const bool next_upper = q < n && is_ascii_upper(unit_at(text, q));
        bool ends = true;
        if ((only_stops && marks >= 2) || (marks == 1 && u == "\xE2\x80\xA6")) {
            ends = q == n || next_upper;
        } else if (marks == 1 && u == ".") {
            std::size_t wb = i;
            while (wb > start) {
                const std::size_t p = last_unit_begin(text, start, wb);
                const std::string_view pu = text.substr(p, wb - p);
                if (is_space(pu) || (contains(punctuation_, pu) && pu != "." &&
                                     !contains(joiners_, pu))) {
                    break;
                }
                wb = p;
            }
            if (wb < i) {
                const std::string candidate = fold(text.substr(wb, i + 1 - wb));
                if (is_abbreviation(candidate)) {
                    ends = !contains(titles_, candidate) && (q == n || next_upper);
                }
            }
        }
        if (ends) {
            push(start, k);
            start = npos;
            i = k;
        } else {
            i = j;
        }
    }
    if (start != npos) {
        push(start, n);
    }
    return out;
}

std::optional<Form> Assimilation::form_of(const Bytes& word, const Memory* memory) const {
    const AtomOperations ops;
    const Bytes folded = ops.fold(word);
    return forms_.base_of(folded, [&](const Bytes& base, const Bytes& category) {
        if (memory != nullptr) {
            for (const CategoryCount& c : memory->categories_of(base)) {
                if (c.category == category) {
                    return true;
                }
            }
        }
        return dictionary_ != nullptr && std::ranges::contains(dictionary_->categories(base), category);
    });
}

Description Assimilation::describe(const Sentence& atom, Memory* memory,
                                   std::span<const Bytes> taught) const {
    const AtomOperations ops;
    const Cognition cognition;
    Description d;
    d.atom = atom;
    d.entities = entities(atom);
    const std::size_t n = d.entities.entities.size();
    d.notes.assign(n, EntityNote{});
    if (!taught.empty()) {
        cognition.categorize(d.entities, taught, *rules_);
        for (EntityNote& note : d.notes) {
            note.source = Source::Taught;
        }
    } else if (memory != nullptr) {
        for (std::size_t i = 0; i < n; ++i) {
            Entity& entity = d.entities.entities[i];
            const std::vector<CategoryCount> found = memory->categories_of(ops.fold(entity.word));
            if (found.size() == 1) {
                entity.category = found.front().category;
                d.notes[i].source = Source::Memory;
            } else if (found.size() > 1) {
                d.notes[i].source = Source::Open;
                for (const CategoryCount& f : found) {
                    d.notes[i].candidates.push_back(f.category);
                }
            }
        }
        // A2b: the dictionary is the fallback for a word memory does not
        // know. One category is the word's; several are candidates.
        if (dictionary_ != nullptr) {
            for (std::size_t i = 0; i < n; ++i) {
                if (d.notes[i].source != Source::Unknown) {
                    continue;
                }
                Entity& entity = d.entities.entities[i];
                const std::vector<Bytes> found = dictionary_->categories(ops.fold(entity.word));
                if (found.size() == 1) {
                    entity.category = found.front();
                    d.notes[i].source = Source::Dictionary;
                } else if (found.size() > 1) {
                    d.notes[i].source = Source::Open;
                    d.notes[i].candidates = found;
                }
            }
        }
        // The base rules know the pronouns, the number words and the
        // conjunctions: a word nobody else knows takes its category from them.
        for (std::size_t i = 0; i < n; ++i) {
            if (d.notes[i].source != Source::Unknown) {
                continue;
            }
            Entity& entity = d.entities.entities[i];
            const Bytes word = ops.fold(entity.word);
            const bool is_pronoun = std::ranges::any_of(rules_->pronouns(), [&](const auto& pair) { return pair.first == word; });
            const bool is_number = std::ranges::any_of(rules_->number_words(), [&](const auto& pair) { return pair.first == word; });
            const bool is_conjunction = std::ranges::contains(rules_->conjunctions(), word);
            if (is_pronoun || is_number || is_conjunction) {
                entity.category = is_pronoun ? Bytes{'p', 'r', 'o', 'n', 'o', 'u', 'n'}
                                  : is_number ? Bytes{'n', 'u', 'm', 'e', 'r', 'a', 'l'}
                                              : Bytes{'c', 'o', 'n', 'j', 'u', 'n', 'c', 't', 'i', 'o', 'n'};
                d.notes[i].source = Source::Rule;
            }
        }
        // A4: a word nobody knows may be a form of a word somebody knows
        // ("skies" of "sky"), or carry an ending that says its category by
        // itself ("zorping"). It is a guess, marked as one, with the form noted.
        for (std::size_t i = 0; i < n; ++i) {
            if (d.notes[i].source != Source::Unknown) {
                continue;
            }
            Entity& entity = d.entities.entities[i];
            const Bytes word = ops.fold(entity.word);
            std::optional<Form> form = form_of(word, memory);
            if (!form) {
                form = forms_.by_ending(word);
            }
            if (!form) {
                continue;
            }
            entity.category = form->category;
            d.notes[i].source = Source::Guess;
            d.notes[i].candidates = {form->category};
            d.notes[i].form = std::string(word.begin(), word.end()) + " is a form of " +
                              std::string(form->base.begin(), form->base.end()) + " (" + form->rule + ")";
        }
        // A2b: a word nobody knows may be a slip: the dictionary words one
        // slip away, the ones memory knows first. Such a word is asked about,
        // not guessed.
        if (dictionary_ != nullptr) {
            for (std::size_t i = 0; i < n; ++i) {
                if (d.notes[i].source != Source::Unknown) {
                    continue;
                }
                std::vector<Bytes> near = dictionary_->near(ops.fold(d.entities.entities[i].word));
                std::ranges::stable_partition(near, [&](const Bytes& w) {
                    return !memory->uses(w).empty();
                });
                d.notes[i].near = std::move(near);
            }
        }
        // A6 (the most specific context): a word memory knows with several
        // categories takes the one its own uses have in the most specific
        // context that matches: the same words on both sides, then the same
        // word on one side, then the same categories on both sides, then on
        // one side, then the most used. A level decides when it has votes and
        // one winner; a tie falls to the next level. Two passes: the first
        // decides by the context alone, so the second sees the categories the
        // first chose on its neighbours; the second may fall to the most used.
        // The word stays Open (it is known, with several categories), with the
        // chosen category first among its candidates and the level in the note.
        static const std::array<std::string_view, 7> levels = {
            "the words on both sides", "the word before",     "the word after",  "the categories on both sides",
            "the category before",     "the category after", "the most used"};
        for (const bool last : {false, true}) {
            for (std::size_t i = 0; i < n; ++i) {
                EntityNote& note = d.notes[i];
                if (note.source != Source::Open || note.candidates.empty() || !d.entities.entities[i].category.empty()) {
                    continue;
                }
                const std::vector<WordUse>& uses = memory->uses(ops.fold(d.entities.entities[i].word));
                if (uses.empty()) {
                    continue;  // the dictionary's candidates: no use of its own to read
                }
                const Bytes before = i > 0 ? ops.fold(d.entities.entities[i - 1].word) : Bytes{};
                const Bytes after = i + 1 < n ? ops.fold(d.entities.entities[i + 1].word) : Bytes{};
                const Bytes& before_category = i > 0 ? d.entities.entities[i - 1].category : before;
                const Bytes& after_category = i + 1 < n ? d.entities.entities[i + 1].category : after;
                std::array<std::map<Bytes, std::int64_t>, 7> votes;
                for (const WordUse& use : uses) {
                    if (!std::ranges::contains(note.candidates, use.category)) {
                        continue;
                    }
                    // At the start or the end of the sentence the neighbour is
                    // empty, and an empty neighbour in a use is the same place.
                    const bool word_before = use.before == before;
                    const bool word_after = use.after == after;
                    const bool category_before = i == 0 ? use.before.empty()
                                                        : (!before_category.empty() && use.before_category == before_category);
                    const bool category_after = i + 1 == n ? use.after.empty()
                                                           : (!after_category.empty() && use.after_category == after_category);
                    if (word_before && word_after) {
                        ++votes[0][use.category];
                    }
                    if (word_before) {
                        ++votes[1][use.category];
                    }
                    if (word_after) {
                        ++votes[2][use.category];
                    }
                    if (category_before && category_after) {
                        ++votes[3][use.category];
                    }
                    if (category_before) {
                        ++votes[4][use.category];
                    }
                    if (category_after) {
                        ++votes[5][use.category];
                    }
                    ++votes[6][use.category];
                }
                const std::size_t deepest = last ? votes.size() : votes.size() - 1;
                for (std::size_t level = 0; level < deepest; ++level) {
                    std::vector<std::pair<std::int64_t, Bytes>> ranked;
                    for (const auto& [category, count] : votes[level]) {
                        ranked.emplace_back(count, category);
                    }
                    std::ranges::sort(ranked, [](const auto& a, const auto& b) { return a.first > b.first; });
                    if (ranked.empty() || (ranked.size() > 1 && ranked[0].first == ranked[1].first)) {
                        continue;
                    }
                    d.entities.entities[i].category = ranked.front().second;
                    note.context = levels[level];
                    std::vector<Bytes> ordered{ranked.front().second};
                    for (const Bytes& candidate : note.candidates) {
                        if (candidate != ranked.front().second) {
                            ordered.push_back(candidate);
                        }
                    }
                    note.candidates = std::move(ordered);
                    break;
                }
            }
        }
        // A6 (first step): an unknown or open word takes the category that
        // known words have in the same context, the words before and after
        // it, when the votes have one winner. It is a guess, marked as one.
        // For an open word only its candidates may win. A word one slip away
        // from a known one (A2b) is guessed the same way and asked about too.
        for (std::size_t i = 0; i < n; ++i) {
            const bool open = d.notes[i].source == Source::Open;
            if ((d.notes[i].source != Source::Unknown && !open) || !d.entities.entities[i].category.empty()) {
                continue;
            }
            const std::vector<Bytes> allowed = d.notes[i].candidates;
            std::map<Bytes, std::int64_t> votes;
            if (i > 0) {
                for (const WordUse& use : memory->uses(ops.fold(d.entities.entities[i - 1].word))) {
                    if (!use.after_category.empty()) {
                        ++votes[use.after_category];
                    }
                }
            }
            if (i + 1 < n) {
                for (const WordUse& use : memory->uses(ops.fold(d.entities.entities[i + 1].word))) {
                    if (!use.before_category.empty()) {
                        ++votes[use.before_category];
                    }
                }
            }
            std::vector<std::pair<std::int64_t, Bytes>> ranked;
            for (const auto& [category, count] : votes) {
                if (allowed.empty() || std::ranges::contains(allowed, category)) {
                    ranked.emplace_back(count, category);
                }
            }
            std::ranges::sort(ranked, [](const auto& a, const auto& b) { return a.first > b.first; });
            if (ranked.empty() || (ranked.size() > 1 && ranked[0].first == ranked[1].first)) {
                continue;
            }
            d.entities.entities[i].category = ranked.front().second;
            d.notes[i].source = Source::Guess;
            d.notes[i].candidates.clear();
            for (const auto& [count, category] : ranked) {
                d.notes[i].candidates.push_back(category);
            }
        }
    }
    // A guessed category is marked before the qualification, which does not
    // count it; the qualification before the types, since the grammar reads
    // a question by its own patterns (K2).
    for (std::size_t i = 0; i < n; ++i) {
        if (d.notes[i].source == Source::Guess) {
            d.entities.entities[i].types.push_back(Bytes{'g', 'u', 'e', 's', 's', 'e', 'd'});
        }
    }
    const std::string_view qualification = name(cognition.qualify(d.atom, d.entities, *rules_));
    d.category.bytes.assign(qualification.begin(), qualification.end());
    types(d);
    d.image = image(d, memory);
    d.metadata = ops.metadata(d.category, d.type, d.entities);
    return d;
}

void Assimilation::redescribe(Description& d) const {
    const AtomOperations ops;
    const Cognition cognition;
    static const Bytes guessed{'g', 'u', 'e', 's', 's', 'e', 'd'};
    for (Entity& entity : d.entities.entities) {
        const bool was_guessed = std::ranges::contains(entity.types, guessed);
        entity.types.clear();
        if (was_guessed) {
            entity.types.push_back(guessed);
        }
    }
    const std::string_view qualification = name(cognition.qualify(d.atom, d.entities, *rules_));
    d.category.bytes.assign(qualification.begin(), qualification.end());
    types(d);
    d.image = image(d, nullptr);
    d.metadata = ops.metadata(d.category, d.type, d.entities);
}


namespace {

Bytes bytes_of(std::string_view text) {
    return Bytes(text.begin(), text.end());
}

std::vector<Bytes> split(const Bytes& text, char separator) {
    std::vector<Bytes> out;
    Bytes current;
    for (const std::uint8_t b : text) {
        if (b == static_cast<std::uint8_t>(separator)) {
            out.push_back(current);
            current.clear();
        } else {
            current.push_back(b);
        }
    }
    out.push_back(current);
    return out;
}

bool ends_with(const Bytes& word, const Bytes& ending) {
    return word.size() >= ending.size() + 2 &&
           std::equal(ending.rbegin(), ending.rend(), word.rbegin());
}

bool has_suffix(const Bytes& word, std::string_view suffix) {
    return word.size() > suffix.size() &&
           std::equal(suffix.rbegin(), suffix.rend(), word.rbegin());
}

bool all_digits(const Bytes& word, std::size_t end) {
    if (end == 0) {
        return false;
    }
    for (std::size_t i = 0; i < end; ++i) {
        if (word[i] < '0' || word[i] > '9') {
            return false;
        }
    }
    return true;
}

}  // namespace

Assimilation::Emotion Assimilation::emotion(const Description& d) const {
    const AtomOperations ops;
    std::vector<Bytes> folded;
    for (const Entity& e : d.entities.entities) {
        folded.push_back(ops.fold(e.word));
    }
    const std::size_t n = folded.size();
    for (const Bytes& marker : rules_->sarcasm()) {
        const std::vector<Bytes> words = split(marker, ' ');
        for (std::size_t i = 0; i + words.size() <= n; ++i) {
            if (std::equal(words.begin(), words.end(), folded.begin() + static_cast<std::ptrdiff_t>(i))) {
                return {bytes_of("sarcasm"), std::string(marker.begin(), marker.end())};
            }
        }
    }
    for (std::size_t i = 0; i < n; ++i) {
        for (const auto& [word, feeling] : rules_->emotions()) {
            if (word == folded[i]) {
                return {feeling, std::string(word.begin(), word.end())};
            }
        }
    }
    return {bytes_of("neutral"), {}};
}

void Assimilation::types(Description& d) const {
    const AtomOperations ops;
    static const Bytes noun = bytes_of("noun");
    static const Bytes verb = bytes_of("verb");
    static const Bytes adjective = bytes_of("adjective");
    static const Bytes adverb = bytes_of("adverb");
    static const Bytes pronoun = bytes_of("pronoun");
    static const Bytes auxiliary_verb = bytes_of("auxiliary verb");
    static const Bytes numeral = bytes_of("numeral");
    static const Bytes proper_noun = bytes_of("proper noun");
    static const Bytes preposition = bytes_of("preposition");
    static const Bytes interjection = bytes_of("interjection");
    static const Bytes conjunction = bytes_of("conjunction");
    const std::vector<Bytes>& copulas = rules_->copulas();  // copulas.txt (A7)

    std::vector<Entity>& entities = d.entities.entities;
    const std::size_t n = entities.size();
    std::vector<Bytes> folded;
    folded.reserve(n);
    for (const Entity& e : entities) {
        folded.push_back(ops.fold(e.word));
    }

    // 1. Features, from the form of the word and its category.
    const auto features_of = [&](std::size_t i) {
        const Bytes& word = folded[i];
        const Bytes& category = entities[i].category;
        std::vector<Bytes> out;
        if (category.empty()) {
            return out;
        }
        for (const auto& [form, value] : rules_->forms()) {
            if (form != word) {
                continue;
            }
            const std::vector<Bytes> parts = split(value, ':');
            if (parts.size() == 2 && parts[0] == category) {
                return split(parts[1], ';');
            }
        }
        const auto by_word = [&](const std::vector<std::pair<Bytes, Bytes>>& table) {
            for (const auto& [key, value] : table) {
                if (key == word) {
                    out = split(value, ';');
                    std::erase(out, bytes_of("support"));  // a mark for the image (A9), not a feature
                    return true;
                }
            }
            return false;
        };
        if (category == pronoun && by_word(rules_->pronouns())) {
            return out;
        }
        if (category == auxiliary_verb && by_word(rules_->auxiliaries())) {
            return out;
        }
        for (const auto& [ending, value] : rules_->endings()) {
            const std::vector<Bytes> parts = split(value, ':');
            if (parts.size() == 2 && parts[0] == category && ends_with(word, ending)) {
                out.push_back(parts[1]);
                return out;
            }
        }
        if (category == noun || category == proper_noun) {
            out.push_back(bytes_of("singular"));
        } else if (category == verb) {
            out.push_back(bytes_of("base"));
        } else if (category == adjective) {
            out.push_back(bytes_of("positive"));
        } else if (category == numeral) {
            const bool ordinal = word.size() > 2 && all_digits(word, word.size() - 2) &&
                                 (has_suffix(word, "st") || has_suffix(word, "nd") ||
                                  has_suffix(word, "rd") || has_suffix(word, "th"));
            out.push_back(bytes_of(ordinal ? "ordinal" : "cardinal"));
        }
        return out;
    };

    // 2. Roles: from the grammar pattern the categories fit (K2), else by
    // position around the first verb.
    d.pattern.clear();
    std::vector<Bytes> roles(n);
    bool fitted = false;
    if (grammar_ != nullptr && n > 0) {
        std::vector<Bytes> categories;
        categories.reserve(n);
        for (const Entity& e : entities) {
            categories.push_back(e.category);
        }
        static const Bytes question = bytes_of("question");
        Fit fit = grammar_->fit(categories, d.category.bytes == question);
        if (fit.fits) {
            fitted = true;
            roles = std::move(fit.roles);
            d.pattern = std::move(fit.pattern);
        }
    }
    std::size_t predicate = n;
    for (std::size_t i = 0; i < n; ++i) {
        if (entities[i].category == verb || entities[i].category == auxiliary_verb) {
            predicate = i;
            break;
        }
    }
    // The copula may be inside a contraction ("isn't"): its first word counts.
    Bytes head = predicate < n ? folded[predicate] : Bytes{};
    for (const auto& [contraction, expansion] : rules_->contractions()) {
        if (contraction == head) {
            const std::vector<Bytes> parts = split(expansion, ' ');
            if (!parts.empty()) {
                head = parts.front();
            }
            break;
        }
    }
    bool copula = predicate < n && entities[predicate].category == auxiliary_verb && std::ranges::contains(copulas, head);
    for (std::size_t i = predicate + 1; copula && i < n; ++i) {
        if (entities[i].category == verb) {
            copula = false;
        }
    }
    bool in_complement = false;
    for (std::size_t i = 0; !fitted && i < n; ++i) {
        const Bytes& category = entities[i].category;
        if (category == interjection) {
            roles[i] = bytes_of("none");
        } else if (category == conjunction) {
            roles[i] = bytes_of("link");
            in_complement = false;
        } else if (category == adverb) {
            roles[i] = bytes_of("modifier");
        } else if (i < predicate) {
            roles[i] = bytes_of("subject");
        } else if (i == predicate || (i > predicate && (category == verb || category == auxiliary_verb) && !in_complement)) {
            roles[i] = bytes_of("predicate");
        } else if (category == preposition) {
            roles[i] = bytes_of("complement");
            in_complement = true;
        } else if (in_complement) {
            roles[i] = bytes_of("complement");
        } else {
            roles[i] = bytes_of(copula ? "attribute" : "object");
        }
    }

    // 3. The emotion of the atom.
    const Bytes emotion = this->emotion(d).feeling;

    // 4. Write the types: the entity's features and its role, then the mark of
    // a guessed category; the atom's roles and emotion.
    Bytes type;
    for (std::size_t i = 0; i < n; ++i) {
        static const Bytes guessed_mark = bytes_of("guessed");
        const bool guessed = std::ranges::contains(entities[i].types, guessed_mark);
        entities[i].types = features_of(i);
        entities[i].types.push_back(roles[i]);
        if (guessed) {
            entities[i].types.push_back(guessed_mark);
        }
        if (i > 0) {
            type.push_back(' ');
        }
        type.insert(type.end(), roles[i].begin(), roles[i].end());
    }
    if (n > 0) {
        type.push_back(' ');
    }
    type.push_back('/');
    type.push_back(' ');
    type.insert(type.end(), emotion.begin(), emotion.end());
    d.type.bytes = std::move(type);
}

ImageElectron Assimilation::image(const Description& d, const Memory* memory) const {
    const AtomOperations ops;
    static const Bytes interjection = bytes_of("interjection");
    static const Bytes proper_noun = bytes_of("proper noun");
    static const Bytes auxiliary_verb = bytes_of("auxiliary verb");
    static const Bytes guessed = bytes_of("guessed");
    static const Bytes support = bytes_of("support");
    static const std::array<std::string_view, 8> order = {"subject", "predicate", "object",   "attribute",
                                                          "complement", "modifier", "link", "none"};
    // The marks that carry meaning; "present", "third person", "singular" and
    // "base" are the plain case and are not written.
    static const std::vector<Bytes> marks = {bytes_of("plural"),      bytes_of("past"),        bytes_of("progressive"),
                                             bytes_of("participle"),  bytes_of("comparative"), bytes_of("superlative")};
    const auto role_of = [&](const Entity& e) -> std::string_view {
        for (auto it = e.types.rbegin(); it != e.types.rend(); ++it) {
            if (*it == guessed) {
                continue;
            }
            for (const std::string_view role : order) {
                if (std::string_view{reinterpret_cast<const char*>(it->data()), it->size()} == role) {
                    return role;
                }
            }
            break;  // the role is the last type: the features come before it
        }
        return "none";
    };
    std::map<std::string_view, std::vector<std::string>> groups;
    bool negated = false;
    for (const Entity& e : d.entities.entities) {
        if (e.category == interjection) {
            continue;
        }
        const std::string_view role = role_of(e);
        const Bytes folded = ops.fold(e.word);
        // A contraction is its words.
        std::vector<Bytes> words;
        for (const auto& [contraction, expansion] : rules_->contractions()) {
            if (contraction == folded) {
                for (const Bytes& part : split(expansion, ' ')) {
                    words.push_back(part);
                }
                break;
            }
        }
        if (words.empty()) {
            words.push_back(folded);
        }
        std::vector<std::string> pending_marks;  // do-support leaves its time to the next word
        for (const Bytes& word : words) {
            if (std::ranges::contains(rules_->articles(), word)) {
                continue;
            }
            if (std::ranges::contains(rules_->negation_words(), word)) {
                negated = !negated;
                continue;
            }
            bool is_number = false;
            for (const auto& [number, digits] : rules_->number_words()) {
                if (number == word) {
                    groups[role].emplace_back(digits.begin(), digits.end());
                    is_number = true;
                    break;
                }
            }
            if (is_number) {
                continue;
            }
            // Do-support (auxiliaries.txt, "support"): dropped, its time kept.
            // The word of an auxiliary verb, of a contraction ("don't"), or of
            // an entity nobody categorized; a verb "do" stays ("I do my work").
            if (e.category == auxiliary_verb || e.category.empty() || words.size() > 1) {
                bool supports = false;
                for (const auto& [auxiliary, features] : rules_->auxiliaries()) {
                    if (auxiliary == word) {
                        const std::vector<Bytes> parts = split(features, ';');
                        supports = std::ranges::contains(parts, support);
                        if (supports) {
                            for (const Bytes& part : parts) {
                                if (std::ranges::contains(marks, part)) {
                                    pending_marks.emplace_back(part.begin(), part.end());
                                }
                            }
                        }
                        break;
                    }
                }
                if (supports) {
                    continue;
                }
            }
            // The base form, by the pairs and the endings (A4); an ending
            // counts only when its category is the entity's.
            std::optional<Form> form = form_of(word, memory);
            if (!form && !e.category.empty()) {
                form = forms_.by_ending(word);
                if (form && form->category != e.category) {
                    form.reset();
                }
            }
            std::string text = e.category == proper_noun ? std::string(e.word.begin(), e.word.end())
                                                         : std::string(word.begin(), word.end());
            std::vector<std::string> word_marks = std::move(pending_marks);
            pending_marks.clear();
            if (form) {
                text.assign(form->base.begin(), form->base.end());
                if (std::ranges::contains(marks, form->feature)) {
                    word_marks.emplace_back(form->feature.begin(), form->feature.end());
                }
            }
            for (const std::string& mark : word_marks) {
                text += " (" + mark + ")";
            }
            groups[role].push_back(std::move(text));
        }
    }
    std::string out(d.category.bytes.begin(), d.category.bytes.end());
    for (const std::string_view role : order) {
        const auto found = groups.find(role);
        if (found == groups.end() || found->second.empty()) {
            continue;
        }
        out += " | ";
        out += role;
        out += ":";
        for (const std::string& word : found->second) {
            out += " " + word;
        }
    }
    if (negated) {
        out += " | not";
    }
    ImageElectron image;
    image.bytes.assign(out.begin(), out.end());
    return image;
}

Bytes Assimilation::word_form(const Bytes& base, const Bytes& category, const Bytes& feature,
                              const Memory* memory) const {
    const std::vector<Bytes> candidates = forms_.forms_of(base, category, feature);
    if (candidates.empty()) {
        return base;
    }
    if (memory != nullptr) {
        for (const Bytes& candidate : candidates) {
            if (!memory->uses(candidate).empty()) {
                return candidate;
            }
        }
    }
    if (dictionary_ != nullptr) {
        for (const Bytes& candidate : candidates) {
            if (dictionary_->contains(candidate)) {
                return candidate;
            }
        }
    }
    return candidates.front();
}

std::string Assimilation::sentence_of(const ImageElectron& image, const Memory* memory) const {
    static const Bytes noun = bytes_of("noun");
    static const Bytes verb = bytes_of("verb");
    static const Bytes adjective = bytes_of("adjective");
    static const Bytes auxiliary_verb = bytes_of("auxiliary verb");
    static const Bytes pronoun = bytes_of("pronoun");
    static const Bytes support = bytes_of("support");
    static const Bytes plural = bytes_of("plural");
    static const Bytes singular = bytes_of("singular");
    static const Bytes past = bytes_of("past");
    static const Bytes present = bytes_of("present");
    static const Bytes third_person = bytes_of("third person");
    static const Bytes first_person = bytes_of("first person");
    static const Bytes second_person = bytes_of("second person");
    static const Bytes question = bytes_of("question");
    static const std::array<std::string_view, 8> order = {"subject", "predicate", "object",   "attribute",
                                                          "complement", "modifier", "link", "none"};
    struct Word {
        Bytes base;
        std::vector<Bytes> marks;
    };
    // 1. Read the image.
    const std::string text(image.bytes.begin(), image.bytes.end());
    std::vector<std::string> parts;
    for (std::size_t from = 0;;) {
        const std::size_t at = text.find(" | ", from);
        parts.push_back(text.substr(from, at == std::string::npos ? std::string::npos : at - from));
        if (at == std::string::npos) {
            break;
        }
        from = at + 3;
    }
    if (parts.empty() || parts.front().empty()) {
        return {};
    }
    const Bytes qualification = bytes_of(parts.front());
    bool negated = false;
    std::map<std::string_view, std::vector<Word>> groups;
    for (std::size_t i = 1; i < parts.size(); ++i) {
        const std::string& part = parts[i];
        if (part == "not") {
            negated = true;
            continue;
        }
        const std::size_t colon = part.find(": ");
        if (colon == std::string::npos) {
            return {};
        }
        const auto role = std::ranges::find(order, std::string_view{part}.substr(0, colon));
        if (role == order.end()) {
            return {};
        }
        std::vector<Word>& words = groups[*role];
        for (const Bytes& token : split(bytes_of(part.substr(colon + 2)), ' ')) {
            if (token.size() > 2 && token.front() == '(' && token.back() == ')') {
                if (!words.empty()) {
                    words.back().marks.emplace_back(token.begin() + 1, token.end() - 1);
                }
            } else {
                words.push_back({token, {}});
            }
        }
    }
    // 2. What a base is, for the rules that follow: memory's most used
    // category, else the category its irregular pairs give it, else the
    // dictionary's first, else what the role suggests.
    const auto category_of = [&](const Bytes& base, std::string_view role) -> Bytes {
        if (memory != nullptr) {
            Bytes best;
            std::int64_t most = 0;
            for (const CategoryCount& c : memory->categories_of(base)) {
                if (c.count > most) {
                    most = c.count;
                    best = c.category;
                }
            }
            if (!best.empty()) {
                return best;
            }
        }
        for (const auto& [form, value] : rules_->irregular()) {
            const std::vector<Bytes> value_parts = split(value, ':');
            if (value_parts.size() == 3 && value_parts[0] == base) {
                return value_parts[1];
            }
        }
        if (dictionary_ != nullptr) {
            const std::vector<Bytes> found = dictionary_->categories(base);
            if (!found.empty()) {
                return found.front();
            }
        }
        if (role == "predicate") {
            return verb;
        }
        if (role == "attribute") {
            return adjective;
        }
        return noun;
    };
    const auto features_of_auxiliary = [&](const Bytes& word) -> std::vector<Bytes> {
        for (const auto& [auxiliary, features] : rules_->auxiliaries()) {
            if (auxiliary == word) {
                return split(features, ';');
            }
        }
        return {};
    };
    // 3. The subject's number and person, which the predicate agrees with.
    std::vector<Bytes> wanted = {third_person, singular};
    for (const Word& w : groups["subject"]) {
        if (std::ranges::contains(w.marks, plural)) {
            wanted = {plural};
            break;
        }
        for (const auto& [word, features] : rules_->pronouns()) {
            if (word == w.base) {
                const std::vector<Bytes> parts_of = split(features, ';');
                std::vector<Bytes> person_number;
                for (const Bytes& f : parts_of) {
                    if (f == first_person || f == second_person || f == third_person || f == singular || f == plural) {
                        person_number.push_back(f);
                    }
                }
                if (!person_number.empty()) {
                    wanted = person_number;
                }
                break;
            }
        }
    }
    // An auxiliary's features fit when none of its person or number features
    // is outside what is wanted.
    const auto fits = [&](const Bytes& word, const Bytes& time) {
        const std::vector<Bytes> features = features_of_auxiliary(word);
        if (features.empty() || !std::ranges::contains(features, time)) {
            return false;
        }
        for (const Bytes& f : features) {
            if ((f == first_person || f == second_person || f == third_person || f == singular || f == plural) &&
                !std::ranges::contains(wanted, f)) {
                return false;
            }
        }
        return true;
    };
    // The form of a verb for a time, agreeing with the subject.
    const auto verb_form = [&](const Bytes& base, const Bytes& category, const Bytes& time) -> Bytes {
        if (category == auxiliary_verb) {
            std::vector<Bytes> candidates = forms_.forms_of(base, category, time);
            for (const Bytes& candidate : forms_.forms_of(base, category, third_person)) {
                candidates.push_back(candidate);
            }
            for (const Bytes& candidate : candidates) {
                if (fits(candidate, time)) {
                    return candidate;
                }
            }
            if (time == present && fits(base, time)) {
                return base;
            }
            return candidates.empty() ? base : candidates.front();
        }
        if (time == present) {
            return std::ranges::contains(wanted, third_person) && std::ranges::contains(wanted, singular)
                       ? word_form(base, category, third_person, memory)
                       : base;
        }
        return word_form(base, category, time, memory);
    };
    // The article memory saw before a common noun most.
    const auto article_before = [&](const Bytes& word) -> Bytes {
        if (memory == nullptr) {
            return {};
        }
        std::map<Bytes, std::int64_t> counts;
        for (const WordUse& use : memory->uses(word)) {
            if (std::ranges::contains(rules_->articles(), use.before)) {
                ++counts[use.before];
            }
        }
        Bytes best;
        std::int64_t most = 0;
        for (const auto& [article, count] : counts) {
            if (count > most) {
                most = count;
                best = article;
            }
        }
        return best;
    };
    // 4. Render each group.
    const Bytes negation = rules_->negation_words().empty() ? Bytes{} : rules_->negation_words().front();
    Bytes support_word;  // do-support, when the rules have it and the predicate needs it
    const auto render = [&](std::string_view role, std::vector<Bytes>& out) {
        const auto found = groups.find(role);
        if (found == groups.end()) {
            return;
        }
        bool first_auxiliary_done = false;
        for (const Word& w : found->second) {
            const bool number = !w.base.empty() && std::ranges::all_of(w.base, [](std::uint8_t c) { return c >= '0' && c <= '9'; });
            if (number) {
                out.push_back(w.base);
                continue;
            }
            const Bytes category = category_of(w.base, role);
            Bytes word = w.base;
            if (role == "predicate" && (category == verb || category == auxiliary_verb)) {
                Bytes time = present;
                for (const Bytes& mark : w.marks) {
                    if (mark == past || mark == bytes_of("progressive") || mark == bytes_of("participle")) {
                        time = mark;
                    }
                }
                if (negated && category == verb && !first_auxiliary_done && support_word.empty()) {
                    // Do-support carries the time and the negation: "did not fly".
                    for (const auto& [auxiliary, features] : rules_->auxiliaries()) {
                        const std::vector<Bytes> parts_of = split(features, ';');
                        if (std::ranges::contains(parts_of, support) && fits(auxiliary, time)) {
                            support_word = auxiliary;
                            break;
                        }
                    }
                    if (!support_word.empty()) {
                        out.push_back(support_word);
                        out.push_back(negation);
                        first_auxiliary_done = true;
                        out.push_back(w.base);
                        continue;
                    }
                }
                word = verb_form(w.base, category, time);
                out.push_back(word);
                if (negated && !first_auxiliary_done && category == auxiliary_verb) {
                    out.push_back(negation);
                    first_auxiliary_done = true;
                }
                continue;
            }
            if (category == noun) {
                if (std::ranges::contains(w.marks, plural)) {
                    word = word_form(w.base, noun, plural, memory);
                }
                const Bytes article = article_before(word);
                if (!article.empty() && (out.empty() || !std::ranges::contains(rules_->articles(), out.back()))) {
                    out.push_back(article);
                }
                out.push_back(word);
                continue;
            }
            for (const Bytes& mark : w.marks) {
                word = word_form(w.base, category, mark, memory);
            }
            out.push_back(word);
        }
    };
    std::vector<Bytes> out;
    const bool asks = qualification == question;
    std::vector<std::string_view> sequence(order.begin(), order.end());
    if (asks) {
        // A question word leads ("What is the sky?"), else the auxiliary ("Is the sky blue?").
        std::string_view leading;
        for (const std::string_view role : order) {
            const auto found = groups.find(role);
            if (found != groups.end() && !found->second.empty() &&
                std::ranges::contains(rules_->question_words(), found->second.front().base)) {
                leading = role;
                break;
            }
        }
        if (!leading.empty() && leading != "subject") {
            sequence = {leading, "subject", "predicate", "object", "attribute", "complement", "modifier", "link", "none"};
            std::erase(sequence, leading);
            sequence.insert(sequence.begin(), leading);
            sequence.insert(sequence.begin() + 1, "predicate");
            sequence.erase(std::ranges::find(sequence.begin() + 2, sequence.end(), std::string_view{"predicate"}));
        } else if (leading.empty()) {
            sequence = {"predicate", "subject", "object", "attribute", "complement", "modifier", "link", "none"};
        }
    }
    // The negation of a verb without an auxiliary when the rules have no
    // do-support: the negation word before the predicate ("não é").
    bool has_support = false;
    for (const auto& [auxiliary, features] : rules_->auxiliaries()) {
        if (std::ranges::contains(split(features, ';'), support)) {
            has_support = true;
            break;
        }
    }
    for (const std::string_view role : sequence) {
        if (role == "predicate" && negated && !has_support && !negation.empty()) {
            out.push_back(negation);
            negated = false;  // placed
        }
        render(role, out);
    }
    if (out.empty()) {
        return {};
    }
    std::string sentence;
    for (const Bytes& word : out) {
        sentence += (sentence.empty() ? "" : " ") + std::string(word.begin(), word.end());
    }
    if (sentence[0] >= 'a' && sentence[0] <= 'z') {
        sentence[0] = static_cast<char>(sentence[0] - 'a' + 'A');
    }
    sentence += asks ? "?" : ".";
    return sentence;
}

}  // namespace larry
