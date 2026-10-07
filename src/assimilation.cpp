#include "larry/assimilation.hpp"

#include "larry/atom_operations.hpp"
#include "larry/utf8.hpp"

#include <algorithm>
#include <limits>
#include <string>

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

Assimilation::Assimilation(const BaseRules& rules)
    : rules_(&rules), punctuation_(sorted(rules.punctuation())),
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

}  // namespace larry
