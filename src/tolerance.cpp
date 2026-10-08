#include "larry/tolerance.hpp"

#include "larry/assimilation.hpp"
#include "larry/atom_operations.hpp"
#include "larry/memory.hpp"

#include <algorithm>
#include <charconv>
#include <map>
#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace larry {

namespace {

Bytes bytes_of(std::string_view text) {
    return Bytes(text.begin(), text.end());
}

std::string text_of(const Bytes& bytes) {
    return std::string(bytes.begin(), bytes.end());
}

std::string_view view_of(const Bytes& bytes) {
    return std::string_view{reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

bool starts_with(const Bytes& bytes, std::string_view prefix) {
    return bytes.size() > prefix.size() &&
           std::equal(prefix.begin(), prefix.end(), bytes.begin()) && bytes[prefix.size()] == ' ';
}

std::size_t number_of(const Bytes& value, std::size_t fallback) {
    std::size_t out = 0;
    const std::string_view text = view_of(value);
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), out);
    return error == std::errc{} && end == text.data() + text.size() && out > 0 ? out : fallback;
}

// "a" or "an" before a category name.
std::string article(const Bytes& category) {
    const bool vowel = !category.empty() && std::string_view{"aeiou"}.find(static_cast<char>(category.front())) !=
                                                std::string_view::npos;
    return (vowel ? "an " : "a ") + text_of(category);
}

const Bytes auxiliary_verb = bytes_of("auxiliary verb");
const Bytes verb = bytes_of("verb");
const Bytes determiner = bytes_of("determiner");
const Bytes adjective = bytes_of("adjective");
const Bytes numeral = bytes_of("numeral");
const Bytes base = bytes_of("base");
const Bytes noun = bytes_of("noun");
const Bytes proper_noun = bytes_of("proper noun");
const Bytes pronoun = bytes_of("pronoun");
const Bytes conjunction = bytes_of("conjunction");
const Bytes subject = bytes_of("subject");
const Bytes predicate = bytes_of("predicate");
const Bytes singular = bytes_of("singular");
const Bytes plural = bytes_of("plural");
const Bytes third_person = bytes_of("third person");
const Bytes question = bytes_of("question");

// The role of an entity: the last type that is one.
Bytes role_of(const Entity& e) {
    for (auto it = e.types.rbegin(); it != e.types.rend(); ++it) {
        if (std::ranges::contains(Grammar::roles(), *it)) {
            return *it;
        }
    }
    return {};
}

}  // namespace

Tolerance::Tolerance(const BaseRules& rules, const Grammar* grammar)
    : rules_(&rules), grammar_(grammar) {
    for (const auto& [key, value] : rules.tolerance()) {
        if (key == bytes_of("words per deviation")) {
            per_ = number_of(value, per_);
        } else if (key == bytes_of("most deviations")) {
            most_ = number_of(value, most_);
        } else if (starts_with(key, "fill")) {
            fillers_.emplace_back(Bytes(key.begin() + 5, key.end()), value);
        } else if (starts_with(key, "singular")) {
            singular_plural_.emplace_back(Bytes(key.begin() + 9, key.end()), value);
        }
    }
}

std::size_t Tolerance::allowed(std::size_t words) const noexcept {
    if (words == 0) {
        return 0;
    }
    return std::min(most_, (words + per_ - 1) / per_);
}

std::optional<Bytes> Tolerance::filler(const Bytes& category) const {
    for (const auto& [c, word] : fillers_) {
        if (c == category) {
            return word;
        }
    }
    return std::nullopt;
}

std::optional<Bytes> Tolerance::agreeing(const Bytes& folded, bool is_plural) const {
    for (const auto& [one, many] : singular_plural_) {
        if (folded == one && is_plural) {
            return many;
        }
        if (folded == many && !is_plural) {
            return one;
        }
    }
    return std::nullopt;
}

std::optional<Tolerance::Disagreement> Tolerance::disagreement(const Description& d) const {
    const std::vector<Entity>& entities = d.entities.entities;
    const AtomOperations ops;
    // The predicate: the first auxiliary verb or verb with that role; the
    // subject's head: the first noun, proper noun or pronoun with the subject
    // role. A subject joined by a conjunction, a pronoun that is not third
    // person, or a word without a number cannot be judged.
    std::size_t auxiliary = entities.size();
    std::size_t head = entities.size();
    for (std::size_t i = 0; i < entities.size(); ++i) {
        const Bytes role = role_of(entities[i]);
        if (auxiliary == entities.size() &&
            (entities[i].category == auxiliary_verb || entities[i].category == verb) && role == predicate) {
            auxiliary = i;
        }
        if (role == subject) {
            if (entities[i].category == conjunction) {
                return std::nullopt;
            }
            if (head == entities.size() &&
                (entities[i].category == noun || entities[i].category == proper_noun ||
                 entities[i].category == pronoun)) {
                head = i;
            }
        }
    }
    if (auxiliary == entities.size() || head == entities.size()) {
        return std::nullopt;
    }
    const Entity& h = entities[head];
    if (h.category == pronoun && !std::ranges::contains(h.types, third_person)) {
        return std::nullopt;
    }
    const bool head_plural = std::ranges::contains(h.types, plural);
    const bool head_singular = std::ranges::contains(h.types, singular);
    if (head_plural == head_singular) {
        return std::nullopt;
    }
    const Entity& a = entities[auxiliary];
    bool aux_plural = std::ranges::contains(a.types, plural);
    bool aux_singular = std::ranges::contains(a.types, singular);
    if (a.category == verb) {
        // A verb in the third person is singular ("has"); in its base form,
        // plural ("have"), as far as the table of forms goes.
        aux_singular = std::ranges::contains(a.types, third_person);
        aux_plural = std::ranges::contains(a.types, base);
    }
    if (aux_plural == aux_singular || head_plural == aux_plural) {
        return std::nullopt;
    }
    const std::optional<Bytes> form = agreeing(ops.fold(a.word), head_plural);
    if (!form) {
        return std::nullopt;
    }
    return Disagreement{auxiliary, head, head_plural, *form};
}

std::vector<Tolerance::MissingDeterminer> Tolerance::missing_determiners(const Description& d,
                                                                         Memory* memory) const {
    std::vector<MissingDeterminer> out;
    if (memory == nullptr) {
        return out;
    }
    const std::vector<Entity>& entities = d.entities.entities;
    const AtomOperations ops;
    for (std::size_t i = 0; i < entities.size(); ++i) {
        if (entities[i].category != noun) {
            continue;
        }
        // The start of the noun phrase: back over adjectives and numerals.
        std::size_t start = i;
        while (start > 0 && (entities[start - 1].category == adjective || entities[start - 1].category == numeral)) {
            --start;
        }
        if (start > 0 && (entities[start - 1].category == determiner || entities[start - 1].category == noun)) {
            continue;  // "the old dog"; "AI model" has its determiner before "AI"
        }
        // What memory knows: the uses of the word with a determiner right
        // before it, against those with nothing or another category before.
        std::map<Bytes, std::size_t> determiners;
        std::size_t with = 0;
        std::size_t without = 0;
        for (const WordUse& use : memory->uses(ops.fold(entities[i].word))) {
            if (use.category != noun) {
                continue;
            }
            // A withdrawn conception teaches nothing (Q27), nor does one that
            // was read as something else (Q29): "Sky is blue." read as "The
            // sky is blue." is no use of "sky" without a determiner.
            const std::optional<StoredAtom> atom = memory->find_id(use.atom);
            if (atom && (atom->status == Status::Withdrawn || !atom->reading.empty())) {
                continue;
            }
            if (use.before_category == determiner) {
                ++with;
                ++determiners[ops.fold(use.before)];
            } else if (use.before_category != adjective && use.before_category != numeral) {
                ++without;
            }
        }
        if (with == 0 || without > 0) {
            continue;
        }
        const auto most = std::ranges::max_element(determiners, [](const auto& a, const auto& b) {
            return a.second < b.second;
        });
        out.push_back(MissingDeterminer{start, i, most->first, with});
    }
    return out;
}

Reading Tolerance::read(const Description& said, const Assimilation& assimilation,
                        Memory* memory) const {
    const AtomOperations ops;
    Reading out;
    out.meant = said;
    out.pattern = said.pattern;
    const std::vector<Entity>& entities = said.entities.entities;
    const std::size_t n = entities.size();
    out.allowed = allowed(n);
    if (grammar_ == nullptr || n == 0) {
        return out;
    }
    std::vector<Bytes> categories;
    for (const Entity& e : entities) {
        categories.push_back(e.category);
    }
    if (std::ranges::any_of(categories, [](const Bytes& c) { return c.empty(); })) {
        return out;  // A word without a category: the grammar cannot judge.
    }
    const bool is_question = said.category.bytes == question;

    // The ending of the sentence as said, kept in the reading: ".", "?", "!".
    const std::string_view as_said = ops.text(said.atom);
    std::size_t end = as_said.size();
    while (end > 0 && std::ranges::contains(rules_->punctuation(), Bytes{static_cast<std::uint8_t>(as_said[end - 1])})) {
        --end;
    }
    const std::string ending{as_said.substr(end)};
    const bool capital = !as_said.empty() && as_said.front() >= 'A' && as_said.front() <= 'Z';
    const auto describe = [&](const std::vector<Bytes>& words) {
        std::string text;
        for (const Bytes& w : words) {
            text += text.empty() ? "" : " ";
            text += text_of(w);
        }
        // The capital of the first word stays with the first word.
        if (capital && !text.empty() && text.front() >= 'a' && text.front() <= 'z') {
            text.front() = static_cast<char>(text.front() - 'a' + 'A');
        }
        text += ending;
        return assimilation.describe(ops.from_text(text), memory);
    };
    const auto word_at = [&](std::size_t i) {
        return i < n ? std::format("\"{}\"", text_of(entities[i].word)) : std::string{"the end"};
    };

    // The reading is built as a list of words, with a mark on the ones the
    // reading put there: a filler that then has to agree costs nothing more.
    std::vector<Bytes> words;
    for (const Entity& e : entities) {
        words.push_back(e.word);
    }
    std::vector<bool> put(n, false);
    const auto insert = [&](std::size_t at, const Bytes& word) {
        words.insert(words.begin() + static_cast<std::ptrdiff_t>(at), word);
        put.insert(put.begin() + static_cast<std::ptrdiff_t>(at), true);
    };
    const auto erase = [&](std::size_t at) {
        words.erase(words.begin() + static_cast<std::ptrdiff_t>(at));
        put.erase(put.begin() + static_cast<std::ptrdiff_t>(at));
    };
    const auto same_as_said = [&] {
        if (words.size() != n) {
            return false;
        }
        for (std::size_t i = 0; i < n; ++i) {
            if (words[i] != entities[i].word) {
                return false;
            }
        }
        return true;
    };
    const auto current = [&] { return same_as_said() ? said : describe(words); };

    // 1. No pattern fits: the nearest one within the allowance, each slip named
    // and counted, a missing place filled when the rules say with what, an
    // extra word left out.
    if (said.pattern.empty()) {
        const Near near = grammar_->nearest(categories, is_question, out.allowed);
        if (!near.found) {
            const Fit fit = grammar_->fit(categories, is_question);
            std::string expected;
            for (const Bytes& e : fit.expected) {
                expected += expected.empty() ? "" : ", ";
                expected += e == bytes_of("end") ? "the end of the sentence" : text_of(e);
            }
            out.accepted = false;
            out.reason = std::format(
                "no pattern fits within {} deviation{} for {} word{}; the nearest, \"{}\", breaks at {}{} and expected {}",
                out.allowed, out.allowed == 1 ? "" : "s", n, n == 1 ? "" : "s", fit.pattern,
                fit.breaks_at < n ? "word " + std::to_string(fit.breaks_at + 1) + " " : "",
                word_at(fit.breaks_at), expected);
            return out;
        }
        out.pattern = near.pattern;
        std::vector<Slip> slips = near.slips;
        std::ranges::stable_sort(slips, [](const Slip& a, const Slip& b) { return a.at < b.at; });
        for (const Slip& slip : slips) {
            switch (slip.kind) {
            case Slip::Kind::Missing:
                out.deviations.push_back(std::format("a missing {} before {}", text_of(slip.category), word_at(slip.at)));
                out.counted += 1;
                break;
            case Slip::Kind::Extra:
                out.deviations.push_back(std::format("an extra word {}", word_at(slip.at)));
                out.counted += 1;
                break;
            case Slip::Kind::Wrong:
                out.deviations.push_back(std::format("{} ({}) where {} fits", word_at(slip.at),
                                                     text_of(categories[slip.at]), article(slip.category)));
                out.counted += 2;
                break;
            }
        }
        for (auto it = slips.rbegin(); it != slips.rend(); ++it) {
            if (it->kind == Slip::Kind::Extra) {
                erase(it->at);
            } else if (it->kind == Slip::Kind::Missing) {
                if (const std::optional<Bytes> fill = filler(it->category)) {
                    insert(it->at, *fill);
                }
            }
        }
    }

    // 2. A noun memory knows only with a determiner before it, and without
    // one here: the determiner comes back. Memory knows what was meant, so
    // this is named but costs nothing.
    {
        const Description so_far = current();
        const std::vector<MissingDeterminer> missing = missing_determiners(so_far, memory);
        for (const MissingDeterminer& m : missing) {
            const Entity& head = so_far.entities.entities[m.head];
            out.deviations.push_back(std::format("a missing determiner before \"{}\" (known as \"{} {}\" in {} use{})",
                                                 text_of(head.word), text_of(m.determiner),
                                                 text_of(ops.fold(head.word)), m.uses, m.uses == 1 ? "" : "s"));
        }
        for (auto it = missing.rbegin(); it != missing.rend(); ++it) {
            if (it->at == 0) {
                words[it->head] = ops.fold(words[it->head]);  // "Sky" becomes "the sky"
            }
            insert(it->at, it->determiner);
        }
    }

    // 3. Agreement of the subject and its predicate, on the reading so far.
    if (const std::optional<Disagreement> d = disagreement(current())) {
        if (!put[d->predicate]) {
            out.deviations.push_back(std::format("\"{}\" where \"{}\" fits (\"{}\" is {})",
                                                 text_of(words[d->predicate]), text_of(d->form),
                                                 text_of(words[d->head]), d->plural ? "plural" : "singular"));
            out.counted += 1;
        }
        words[d->predicate] = d->form;
    }

    out.accepted = out.counted <= out.allowed;
    if (!out.accepted) {
        out.reason = std::format("{} deviation{} counted for {} word{}, and {} {} allowed", out.counted,
                                 out.counted == 1 ? "" : "s", n, n == 1 ? "" : "s", out.allowed,
                                 out.allowed == 1 ? "is" : "are");
        return out;
    }
    if (!same_as_said()) {
        out.meant = describe(words);
        out.changed = true;
    }
    return out;
}

}  // namespace larry
