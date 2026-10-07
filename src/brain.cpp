#include "larry/brain.hpp"

#include "larry/atom_operations.hpp"

#include <algorithm>
#include <format>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace larry {

namespace {

Bytes bytes_of(std::string_view text) {
    return Bytes(text.begin(), text.end());
}

bool in(const std::vector<Bytes>& list, const Bytes& item) {
    return std::ranges::contains(list, item);
}

// The words of an expansion ("is not"), split at spaces.
std::vector<Bytes> split_words(const Bytes& text) {
    std::vector<Bytes> out;
    Bytes current;
    for (const std::uint8_t b : text) {
        if (b == ' ') {
            if (!current.empty()) {
                out.push_back(std::move(current));
                current.clear();
            }
        } else {
            current.push_back(b);
        }
    }
    if (!current.empty()) {
        out.push_back(std::move(current));
    }
    return out;
}

const Bytes affirmation = bytes_of("affirmation");
const Bytes auxiliary_verb = bytes_of("auxiliary verb");
const std::vector<Bytes> do_support = {bytes_of("do"), bytes_of("does"), bytes_of("did")};

}  // namespace

Brain::Brain(const BaseRules& rules, Memory& memory, Database* cloud)
    : rules_(&rules), assimilation_(rules), cognition_(), memory_(&memory), cloud_(cloud) {}

void Brain::cache(const StoredAtom& atom) const {
    const Description& d = atom.description;
    const bool fresh = memory_->find(d.metadata) == std::nullopt;
    std::string_view first_source = atom.sources.empty() ? std::string_view{"cloud"} : atom.sources.front();
    memory_->store(d.atom, d.metadata, atom.status, first_source);
    for (std::size_t i = 1; i < atom.sources.size(); ++i) {
        memory_->store(d.atom, d.metadata, atom.status, atom.sources[i]);
    }
    if (fresh) {
        memory_->set_status(d.metadata, atom.status);
    }
}

Stored Brain::remember(const Description& d, Status status, std::string_view source) {
    const Stored stored = memory_->store(d.atom, d.metadata, status, source);
    if (cloud_ != nullptr) {
        cloud_->store(d.atom, d.metadata, status, source);
    }
    // Two different sources validate a proposed conception.
    const std::optional<StoredAtom> held =
        cloud_ != nullptr ? cloud_->find(d.metadata) : memory_->find(d.metadata);
    if (held && held->status == Status::Proposed) {
        std::vector<std::string> sources = held->sources;
        if (const std::optional<StoredAtom> cached = memory_->find(d.metadata)) {
            for (const std::string& s : cached->sources) {
                if (!std::ranges::contains(sources, s)) {
                    sources.push_back(s);
                }
            }
        }
        if (sources.size() >= 2) {
            set_status(d.metadata, Status::Validated);
        }
    }
    return stored;
}

std::vector<StoredAtom> Brain::proposed() const {
    return cloud_ != nullptr ? cloud_->with_status(Status::Proposed)
                             : memory_->with_status(Status::Proposed);
}

std::optional<StoredAtom> Brain::conception(std::int64_t id) const {
    return cloud_ != nullptr ? cloud_->find_id(id) : memory_->find_id(id);
}

bool Brain::set_status(const MetadataElectron& metadata, Status status) {
    bool any = memory_->set_status(metadata, status);
    if (cloud_ != nullptr) {
        if (const std::optional<StoredAtom> held = cloud_->find(metadata)) {
            cloud_->set_status(held->id, status);
            any = true;
        }
    }
    return any;
}

std::pair<std::int64_t, std::int64_t> Brain::sync(std::int64_t pull) {
    if (cloud_ == nullptr) {
        throw std::runtime_error("Brain::sync: there is no cloud; set LARRY_DB");
    }
    std::int64_t pushed = 0;
    for (const StoredAtom& atom : memory_->all()) {
        const Description& d = atom.description;
        if (cloud_->find(d.metadata)) {
            continue;
        }
        if (atom.sources.empty()) {
            cloud_->store(d.atom, d.metadata, atom.status, "");
        }
        for (const std::string& source : atom.sources) {
            cloud_->store(d.atom, d.metadata, atom.status, source);
        }
        ++pushed;
    }
    std::int64_t pulled = 0;
    for (const StoredAtom& atom : cloud_->recent(pull)) {
        if (memory_->find(atom.description.metadata)) {
            continue;
        }
        cache(atom);
        ++pulled;
    }
    return {pushed, pulled};
}

std::vector<StoredAtom> Brain::candidates(const Core& form, bool cloud) const {
    std::vector<StoredAtom> out;
    if (form.words.empty() || (cloud && cloud_ == nullptr)) {
        return out;
    }
    const Bytes* rarest = &form.words.front();
    std::size_t fewest = memory_->uses(*rarest).size();
    for (const Bytes& word : form.words) {
        const std::size_t uses = memory_->uses(word).size();
        if (uses < fewest) {
            fewest = uses;
            rarest = &word;
        }
    }
    std::vector<StoredAtom> found = cloud ? cloud_->containing(*rarest) : memory_->containing(*rarest);
    for (StoredAtom& atom : found) {
        if (atom.description.category.bytes == affirmation && atom.status != Status::Withdrawn) {
            out.push_back(std::move(atom));
        }
    }
    return out;
}

std::vector<Bytes> Brain::expanded_words(const Description& d) const {
    const AtomOperations ops;
    std::vector<Bytes> out;
    for (const Entity& entity : d.entities.entities) {
        const Bytes word = ops.fold(entity.word);
        bool expanded = false;
        for (const auto& [contraction, expansion] : rules_->contractions()) {
            if (contraction == word) {
                for (Bytes& part : split_words(expansion)) {
                    out.push_back(std::move(part));
                }
                expanded = true;
                break;
            }
        }
        if (!expanded) {
            out.push_back(std::move(word));
        }
    }
    return out;
}

Core Brain::core_of(std::vector<Bytes> words) const {
    Core out;
    std::size_t negations = 0;
    for (Bytes& word : words) {
        if (in(rules_->negation_words(), word)) {
            ++negations;
            continue;
        }
        if (in(do_support, word)) {
            continue;
        }
        out.words.push_back(std::move(word));
    }
    out.negated = negations % 2 == 1;
    return out;
}

Core Brain::core(const Description& d) const {
    return core_of(expanded_words(d));
}

bool Brain::is_auxiliary(const Bytes& folded_word, const Bytes& category) const {
    if (category == auxiliary_verb) {
        return true;
    }
    if (!category.empty()) {
        return false;
    }
    // A contraction expands to words memory may know: "isn't" to "is".
    const std::vector<CategoryCount> known = memory_->categories_of(folded_word);
    return known.size() == 1 && known.front().category == auxiliary_verb;
}

std::vector<Core> Brain::statements(const Description& question) const {
    std::vector<Core> out;
    const std::vector<Bytes> words = expanded_words(question);
    if (words.size() < 2 || question.entities.entities.empty()) {
        return out;
    }
    const Entity& first = question.entities.entities.front();
    const bool opens_with_auxiliary =
        is_auxiliary(words.front(), first.category) ||
        (first.category.empty() && in(do_support, words.front()));
    if (!opens_with_auxiliary || in(rules_->question_words(), words.front())) {
        return out;
    }
    const Bytes auxiliary = words.front();
    std::vector<Bytes> rest(words.begin() + 1, words.end());
    // A negative question ("Isn't the sky blue?", "Is not the sky blue?")
    // asks about the positive: the negation right after the auxiliary goes.
    if (!rest.empty() && in(rules_->negation_words(), rest.front())) {
        rest.erase(rest.begin());
    }
    if (rest.empty()) {
        return out;
    }
    for (std::size_t k = 1; k < rest.size(); ++k) {
        std::vector<Bytes> statement(rest.begin(), rest.begin() + static_cast<std::ptrdiff_t>(k));
        statement.push_back(auxiliary);
        statement.insert(statement.end(), rest.begin() + static_cast<std::ptrdiff_t>(k), rest.end());
        out.push_back(core_of(std::move(statement)));
    }
    if (rest.size() == 1) {
        // "Is it?": the only reading has the subject alone.
        std::vector<Bytes> statement = rest;
        statement.push_back(auxiliary);
        out.push_back(core_of(std::move(statement)));
    }
    return out;
}

Verdict Brain::truth(const Sentence& claim) const {
    return truth(assimilation_.describe(claim, memory_));
}

Verdict Brain::truth(const Description& claim) const {
    Verdict verdict;
    std::vector<Core> forms;
    if (claim.category.bytes == bytes_of("question")) {
        forms = statements(claim);
    }
    if (forms.empty()) {
        forms.push_back(core(claim));
    }
    // The cache answers first; the cloud only when the cache cannot.
    for (const bool cloud : {false, true}) {
        bool any_false = false;
        StoredAtom false_because;
        for (const Core& form : forms) {
            for (StoredAtom& atom : candidates(form, cloud)) {
                const Core stored = core(atom.description);
                if (stored.words != form.words) {
                    continue;
                }
                if (stored.negated == form.negated) {
                    verdict.truth = Truth::True;
                    verdict.from_cloud = cloud;
                    if (cloud) {
                        cache(atom);
                    }
                    verdict.because.push_back(std::move(atom));
                    return verdict;
                }
                if (!any_false) {
                    any_false = true;
                    false_because = std::move(atom);
                }
            }
        }
        if (any_false) {
            verdict.truth = Truth::False;
            verdict.from_cloud = cloud;
            if (cloud) {
                cache(false_because);
            }
            verdict.because.push_back(std::move(false_because));
            return verdict;
        }
    }
    // Unknown: the affirmations that share the most content words with the
    // concept, most shared first, then the order they were stored in. A
    // content word is one memory does not know as a determiner, auxiliary
    // verb, preposition, conjunction or pronoun.
    static const std::vector<Bytes> function_categories = {
        bytes_of("determiner"), bytes_of("auxiliary verb"), bytes_of("preposition"),
        bytes_of("conjunction"), bytes_of("pronoun")};
    const auto is_content = [&](const Bytes& word) {
        const std::vector<CategoryCount> known = memory_->categories_of(word);
        return known.size() != 1 || !in(function_categories, known.front().category);
    };
    std::size_t content_words = 0;
    std::map<std::int64_t, std::pair<std::size_t, StoredAtom>> shared;
    for (const Core& form : forms) {
        std::vector<Bytes> seen;
        for (const Bytes& word : form.words) {
            if (in(seen, word) || !is_content(word)) {
                continue;
            }
            seen.push_back(word);
            if (&form == &forms.front()) {
                ++content_words;
            }
            for (StoredAtom& atom : memory_->containing(word)) {
                if (atom.description.category.bytes != affirmation ||
                    atom.status == Status::Withdrawn) {
                    continue;
                }
                auto [it, inserted] = shared.try_emplace(atom.id, 0, std::move(atom));
                ++it->second.first;
            }
        }
    }
    std::vector<std::pair<std::size_t, StoredAtom>> ranked;
    for (auto& [id, entry] : shared) {
        ranked.push_back(std::move(entry));
    }
    std::ranges::stable_sort(ranked, [](const auto& a, const auto& b) { return a.first > b.first; });
    // Nearest means sharing at least one content word and at least half of them.
    for (std::size_t i = 0; i < ranked.size() && verdict.nearest.size() < 3; ++i) {
        if (ranked[i].first >= 1 && ranked[i].first * 2 >= content_words) {
            verdict.nearest.push_back(std::move(ranked[i].second));
        }
    }
    return verdict;
}

std::vector<StoredAtom> Brain::answers(const Description& question) const {
    std::vector<StoredAtom> out;
    const std::vector<Bytes> words = expanded_words(question);
    if (words.size() < 2 || !in(rules_->question_words(), words.front())) {
        return out;
    }
    // The pattern: the known words, and whether the gap is at the end (the
    // question has an auxiliary verb, moved after the subject) or at the
    // front ("who went to the kitchen").
    std::vector<Bytes> known;
    bool gap_at_end = false;
    for (std::size_t j = 1; j < words.size(); ++j) {
        const Entity* entity = j < question.entities.entities.size() ? &question.entities.entities[j] : nullptr;
        const Bytes category = entity != nullptr ? entity->category : Bytes{};
        if (is_auxiliary(words[j], category) || in(do_support, words[j])) {
            known.assign(words.begin() + static_cast<std::ptrdiff_t>(j) + 1, words.end());
            known.push_back(words[j]);
            gap_at_end = true;
            break;
        }
    }
    if (!gap_at_end) {
        known.assign(words.begin() + 1, words.end());
    }
    const Core pattern = core_of(std::move(known));
    if (pattern.words.empty()) {
        return out;
    }
    for (const bool cloud : {false, true}) {
        for (StoredAtom& atom : candidates(pattern, cloud)) {
            const Core stored = core(atom.description);
            if (stored.negated || stored.words.size() <= pattern.words.size()) {
                continue;
            }
            bool fits = gap_at_end
                            ? std::equal(pattern.words.begin(), pattern.words.end(), stored.words.begin())
                            : std::equal(pattern.words.rbegin(), pattern.words.rend(), stored.words.rbegin());
            if (!fits && gap_at_end && pattern.words.size() >= 2) {
                // "X is [?]" is also answered by "[?] is X": the auxiliary and the
                // subject at the end of the conception.
                std::vector<Bytes> reversed(pattern.words.end() - 1, pattern.words.end());
                reversed.insert(reversed.end(), pattern.words.begin(), pattern.words.end() - 1);
                fits = std::equal(reversed.rbegin(), reversed.rend(), stored.words.rbegin());
            }
            if (fits) {
                if (cloud) {
                    cache(atom);
                }
                out.push_back(std::move(atom));
            }
        }
        if (!out.empty()) {
            break;
        }
    }
    return out;
}

Reply Brain::hear(const Sentence& sentence, std::string_view source) {
    const AtomOperations ops;
    const Description d = assimilation_.describe(sentence, memory_);
    const std::string_view qualification{reinterpret_cast<const char*>(d.category.bytes.data()),
                                         d.category.bytes.size()};
    const auto text_of = [&](const StoredAtom& atom) {
        return std::string{ops.text(atom.description.atom)};
    };
    Reply reply;
    if (qualification == "expression") {
        reply.text = std::string{ops.text(sentence)};
        reply.because.emplace_back("rule: an expression is answered in kind");
        return reply;
    }
    if (qualification == "order") {
        reply.text = "I cannot do that yet.";
        reply.because.emplace_back("rule: orders wait for S1");
        return reply;
    }
    if (qualification == "assumption") {
        remember(d, Status::Proposed, source);
        reply.stored = true;
        reply.text = "Noted as an assumption, not as a truth.";
        reply.because.emplace_back("rule: an assumption is kept apart from the truths");
        return reply;
    }
    if (qualification == "question") {
        const std::vector<StoredAtom> found = answers(d);
        if (!found.empty()) {
            for (std::size_t i = 0; i < found.size() && i < 3; ++i) {
                if (i > 0) {
                    reply.text += ' ';
                }
                reply.text += text_of(found[i]);
                reply.because.push_back(text_of(found[i]));
            }
            return reply;
        }
        const Verdict verdict = truth(d);
        switch (verdict.truth) {
        case Truth::True:
            reply.text = "Yes.";
            break;
        case Truth::False:
            reply.text = "No.";
            break;
        case Truth::Unknown:
            reply.text = "I don't know.";
            break;
        }
        for (const StoredAtom& atom : verdict.because) {
            reply.because.push_back((verdict.from_cloud ? "cloud: " : "") + text_of(atom));
        }
        for (const StoredAtom& atom : verdict.nearest) {
            reply.because.push_back("nearest: " + text_of(atom));
        }
        if (verdict.truth == Truth::Unknown && !verdict.nearest.empty()) {
            reply.text += " I know: " + text_of(verdict.nearest.front());
        }
        return reply;
    }
    // An affirmation: what does memory hold already? (C16, first step)
    const Verdict verdict = truth(d);
    const Status before = memory_->find(d.metadata).transform([](const StoredAtom& a) { return a.status; }).value_or(Status::Proposed);
    const Stored stored = remember(d, Status::Proposed, source);
    reply.stored = stored == Stored::New;
    const bool validated_now =
        before == Status::Proposed &&
        memory_->find(d.metadata).transform([](const StoredAtom& a) { return a.status; }).value_or(Status::Proposed) ==
            Status::Validated;
    if (verdict.truth == Truth::True) {
        reply.text = stored == Stored::New ? "I know. " + text_of(verdict.because.front())
                                          : "I already know that.";
        if (validated_now) {
            reply.text += " Now validated: a second source says so.";
            reply.because.emplace_back("rule: two different sources validate a conception (R2)");
        }
        reply.because.push_back(text_of(verdict.because.front()));
        return reply;
    }
    if (verdict.truth == Truth::False) {
        reply.text = "That conflicts with what I know: " + text_of(verdict.because.front()) +
                     " I keep both and note the conflict.";
        reply.because.push_back(text_of(verdict.because.front()));
        reply.because.emplace_back("rule: a conflict is recorded, not chosen silently (R2)");
        return reply;
    }
    reply.text = "Noted.";
    reply.because.emplace_back("rule: an affirmation is stored as a conception");
    bool asked = false;
    for (std::size_t i = 0; i < d.notes.size(); ++i) {
        const Entity& entity = d.entities.entities[i];
        const std::string_view word{reinterpret_cast<const char*>(entity.word.data()),
                                    entity.word.size()};
        if (d.notes[i].source == Source::Guess) {
            reply.text += std::format(" I take \"{}\" as {}.", word,
                                      std::string_view{reinterpret_cast<const char*>(entity.category.data()),
                                                       entity.category.size()});
            reply.because.emplace_back("rule: an unknown word takes the category of known words in the same context, as a guess (A6)");
        } else if (d.notes[i].source == Source::Unknown && !asked) {
            reply.text += std::format(" What is \"{}\"?", word);
            reply.because.emplace_back("rule: Larry asks about a word it does not know (A5)");
            asked = true;
        }
    }
    return reply;
}

}  // namespace larry
