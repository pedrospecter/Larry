#include "larry/assimilation.hpp"

#include "larry/atom_operations.hpp"
#include "larry/cognition.hpp"
#include "larry/dictionary.hpp"
#include "larry/grammar.hpp"
#include "larry/memory.hpp"
#include "larry/utf8.hpp"

#include <algorithm>
#include <limits>
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
        // A6 (first step): an unknown or open word takes the category that
        // known words have in the same context, the words before and after
        // it, when the votes have one winner. It is a guess, marked as one.
        // For an open word only its candidates may win.
        for (std::size_t i = 0; i < n; ++i) {
            const bool open = d.notes[i].source == Source::Open;
            if ((d.notes[i].source != Source::Unknown && !open) || !d.notes[i].near.empty()) {
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
    const std::span<const std::uint8_t> bytes = ops.bytes(atom);
    d.image.bytes.assign(bytes.begin(), bytes.end());
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
    const std::span<const std::uint8_t> bytes = ops.bytes(d.atom);
    d.image.bytes.assign(bytes.begin(), bytes.end());
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
    static const std::vector<Bytes> copulas = {bytes_of("is"), bytes_of("are"), bytes_of("was"),
                                               bytes_of("were"), bytes_of("am")};

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
    bool copula = predicate < n && entities[predicate].category == auxiliary_verb &&
                  std::ranges::contains(copulas, folded[predicate]);
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

}  // namespace larry
