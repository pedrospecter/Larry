#include "larry/brain.hpp"

#include "larry/atom_operations.hpp"
#include "larry/grammar.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <chrono>
#include <ctime>
#include <format>
#include <limits>
#include <map>
#include <optional>
#include <print>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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

Brain::Brain(const BaseRules& rules, Memory& memory, Database* cloud, const Dictionary* dictionary,
             Grammar* grammar)
    : rules_(&rules),
      grammar_(grammar),
      assimilation_(rules, dictionary, grammar),
      tolerance_(rules, grammar),
      harness_(rules),
      arithmetic_(rules),
      calendar_(rules),
      algebra_(rules),
      cognition_(),
      memory_(&memory),
      cloud_(cloud) {
    if (grammar_ != nullptr) {
        for (const StoredAtom& atom : memory_->with_status(Status::Validated)) {
            learn_grammar(atom);
        }
    }
}

bool Brain::learn_grammar(const StoredAtom& atom) {
    if (grammar_ == nullptr || atom.status != Status::Validated || !atom.reading.empty()) {
        return false;
    }
    std::vector<Bytes> categories;
    std::vector<Bytes> roles;
    for (const Entity& e : atom.description.entities.entities) {
        // The role is the last type that is one; after it may come "guessed".
        const auto role = std::ranges::find_if(e.types.rbegin(), e.types.rend(), [](const Bytes& t) {
            return std::ranges::contains(Grammar::roles(), t);
        });
        if (e.category.empty() || role == e.types.rend()) {
            return false;
        }
        categories.push_back(e.category);
        roles.push_back(*role);
    }
    const AtomOperations ops;
    static const Bytes question = bytes_of("question");
    return grammar_->learn("example: " + std::string{ops.text(atom.description.atom)}, categories,
                           roles, atom.description.category.bytes == question);
}

void Brain::cache(const StoredAtom& atom) const {
    const Description& d = atom.description;
    const bool fresh = memory_->find(d.metadata) == std::nullopt;
    std::string_view first_source = atom.sources.empty() ? std::string_view{"cloud"} : atom.sources.front();
    memory_->store(d.atom, d.metadata, atom.status, first_source);
    for (std::size_t i = 1; i < atom.sources.size(); ++i) {
        memory_->store(d.atom, d.metadata, atom.status, atom.sources[i]);
    }
    if (fresh) {
        memory_->set_status(d.metadata, atom.status, atom.decided_by);
        if (!atom.reading.empty()) {
            memory_->set_reading(d.metadata, atom.reading);
        }
    }
}

Stored Brain::remember(const Description& d, Status status, std::string_view source,
                       std::string_view reading) {
    const AtomOperations ops;
    const Stored stored = memory_->store(d.atom, d.metadata, status, source);
    const bool complete = stored != Stored::New && ops.complete(d.metadata);
    if (complete) {
        memory_->redescribe(d.metadata);
    }
    if (stored == Stored::New && !reading.empty()) {
        memory_->set_reading(d.metadata, reading);
    }
    if (cloud_ != nullptr) {
        const Stored in_cloud = cloud_->store(d.atom, d.metadata, status, source);
        if (in_cloud != Stored::New && ops.complete(d.metadata)) {
            if (const std::optional<StoredAtom> held = cloud_->find(d.metadata);
                held && held->description.metadata.bytes != d.metadata.bytes) {
                cloud_->redescribe(held->id, d.metadata);
            }
        }
        if (in_cloud == Stored::New && !reading.empty()) {
            if (const std::optional<StoredAtom> held = cloud_->find(d.metadata)) {
                cloud_->set_reading(held->id, reading);
            }
        }
    }
    // N6: what was just remembered is in play.
    if (const std::optional<StoredAtom> held = memory_->find(d.metadata)) {
        bring_into_play(*held);
    }
    // A10: a defining sentence bonds its two things ("is a kind of", "is part of", ...).
    if (stored == Stored::New) {
        if (const std::optional<Relation> relation = relation_of(d)) {
            (void)bond(Bond{relation->kind, BondEnd{BondEnd::Kind::Entity, relation->from},
                            BondEnd{BondEnd::Kind::Entity, relation->to},
                            {"conception: " + std::string{ops.text(d.atom)}}});
        }
    }
    // A4: each word that is a form of a known word is bonded to it, "form of".
    if (stored == Stored::New) {
        static const Bytes form_of{'f', 'o', 'r', 'm', ' ', 'o', 'f'};
        for (const Entity& entity : d.entities.entities) {
            if (const std::optional<Form> form = assimilation_.form_of(entity.word, memory_)) {
                (void)bond(Bond{form_of, BondEnd::entity(std::string_view{reinterpret_cast<const char*>(form->word.data()), form->word.size()}),
                                BondEnd::entity(std::string_view{reinterpret_cast<const char*>(form->base.data()), form->base.size()}),
                                {"rule: " + form->rule}});
            }
        }
    }
    // N4: the conception joins the molecule being heard, said again or not.
    if (!molecule_.empty()) {
        const Bytes identity = ops.identity(d.metadata);
        const std::string when = now();
        memory_->join(molecule_, identity, source, when);
        if (cloud_ != nullptr) {
            cloud_->join(molecule_, identity, source, when);
        }
    }
    return stored;
}

std::vector<StoredAtom> Brain::newest_first() const {
    std::vector<StoredAtom> out;
    std::vector<Bytes> seen;
    const auto consider = [&](const StoredAtom& atom) {
        if (std::ranges::contains(seen, atom.description.metadata.bytes)) {
            return;
        }
        seen.push_back(atom.description.metadata.bytes);
        out.push_back(atom);
    };
    if (!molecule_.empty()) {
        if (const std::optional<Molecule> m = memory_->molecule(molecule_)) {
            for (auto it = m->members.rbegin(); it != m->members.rend(); ++it) {
                if (const std::optional<StoredAtom> atom = memory_->find_identity(it->identity)) {
                    consider(*atom);
                }
            }
        }
    }
    for (const StoredAtom& atom : working_) {
        if (const std::optional<StoredAtom> fresh = memory_->find(atom.description.metadata)) {
            consider(*fresh);
        }
    }
    const std::vector<StoredAtom> all = memory_->all();
    for (auto it = all.rbegin(); it != all.rend(); ++it) {
        consider(*it);
    }
    return out;
}

Core Brain::thinking_core(const StoredAtom& atom) const {
    if (atom.reading.empty()) {
        return core(atom.description);
    }
    const AtomOperations ops;
    return core(assimilation_.describe(ops.from_text(atom.reading), memory_));
}

std::optional<Brain::Relation> Brain::relation_of(const Description& d) const {
    if (d.category.bytes != affirmation) {
        return std::nullopt;
    }
    const Core form = core(d);
    if (form.negated) {
        return std::nullopt;  // "a sparrow is not a bird" defines nothing
    }
    return relation_in(form, d);
}

std::optional<Brain::Relation> Brain::relation_in(const Core& form, const Description& d) const {
    const AtomOperations ops;
    static const Bytes noun = bytes_of("noun");
    static const Bytes proper_noun = bytes_of("proper noun");
    static const Bytes adjective = bytes_of("adjective");
    static const Bytes verb = bytes_of("verb");
    static const Bytes determiner = bytes_of("determiner");
    static const std::vector<Bytes> articles = {bytes_of("a"), bytes_of("an"), bytes_of("the")};
    if (form.words.size() < 3) {
        return std::nullopt;
    }
    // The category of a core word, from the description (the core keeps the order of the words it kept).
    const auto category_of = [&](const Bytes& word) -> Bytes {
        for (const Entity& e : d.entities.entities) {
            if (ops.fold(e.word) == word) {
                return e.category;
            }
        }
        return {};
    };
    const auto head_of = [&](std::vector<Bytes> side, bool plural_phrase) -> std::optional<Bytes> {
        while (!side.empty() && (std::ranges::contains(articles, side.front()) || category_of(side.front()) == determiner)) {
            side.erase(side.begin());
        }
        if (side.empty()) {
            return std::nullopt;
        }
        const Bytes& head = side.back();
        const Bytes category = category_of(head);
        if (category == adjective || category == verb || category == determiner) {
            return std::nullopt;
        }
        // The singular form (A4): "sparrows" is "sparrow".
        if (const std::optional<Form> singular = assimilation_.form_of(head, memory_);
            singular && singular->category == noun && singular->base != head) {
            return singular->base;
        }
        if (plural_phrase && category.empty() && !std::ranges::contains(articles, head)) {
            // "Robins are birds": the plural undone by the ending, the base unknown.
            const std::vector<Form> by_ending = assimilation_.forms().candidates(head);
            if (!by_ending.empty() && by_ending.front().category == noun && by_ending.front().feature == bytes_of("plural")) {
                return by_ending.front().base;
            }
        }
        return head;
    };
    for (const auto& [phrase, kind] : rules_->relations()) {
        const std::vector<Bytes> parts = split_words(phrase);
        if (parts.empty() || form.words.size() < parts.size() + 2) {
            continue;
        }
        for (std::size_t p = 1; p + parts.size() < form.words.size(); ++p) {
            if (!std::equal(parts.begin(), parts.end(), form.words.begin() + static_cast<std::ptrdiff_t>(p))) {
                continue;
            }
            const std::vector<Bytes> left(form.words.begin(), form.words.begin() + static_cast<std::ptrdiff_t>(p));
            const std::vector<Bytes> right(form.words.begin() + static_cast<std::ptrdiff_t>(p + parts.size()), form.words.end());
            // "are" alone needs a noun on the right: "sparrows are birds", not "sparrows are small".
            const bool bare = parts.size() == 1;
            if (bare) {
                const Bytes category = category_of(right.back());
                const bool noun_like = category == noun || category == proper_noun ||
                                       (category.empty() && !assimilation_.forms().candidates(right.back()).empty() &&
                                        assimilation_.forms().candidates(right.back()).front().category == noun);
                if (!noun_like || category_of(left.back()) == adjective) {
                    continue;
                }
            }
            const bool plural_phrase = parts.front() == bytes_of("are");
            const std::optional<Bytes> from = head_of(left, plural_phrase);
            const std::optional<Bytes> to = head_of(right, plural_phrase);
            if (!from || !to || *from == *to) {
                continue;
            }
            return Relation{kind, *from, *to, std::string(phrase.begin(), phrase.end())};
        }
    }
    return std::nullopt;
}

std::vector<Bytes> Brain::chain(const Bytes& from, const Bytes& to, int steps) const {
    static const Bytes kind_of = bytes_of("is a kind of");
    std::vector<std::vector<Bytes>> paths = {{from}};
    std::vector<Bytes> seen = {from};
    for (int step = 0; step < steps && !paths.empty(); ++step) {
        std::vector<std::vector<Bytes>> next;
        for (const std::vector<Bytes>& path : paths) {
            for (const Bond& bond : memory_->bonds_from(BondEnd{BondEnd::Kind::Entity, path.back()})) {
                if (bond.kind != kind_of || bond.to.kind != BondEnd::Kind::Entity || std::ranges::contains(seen, bond.to.bytes)) {
                    continue;
                }
                std::vector<Bytes> longer = path;
                longer.push_back(bond.to.bytes);
                if (bond.to.bytes == to) {
                    return longer;
                }
                seen.push_back(bond.to.bytes);
                next.push_back(std::move(longer));
            }
        }
        paths = std::move(next);
    }
    return {};
}

std::string Brain::say(std::string_view name) const {
    for (const auto& [key, text] : rules_->replies()) {
        if (std::string_view{reinterpret_cast<const char*>(key.data()), key.size()} == name) {
            return std::string(text.begin(), text.end());
        }
    }
    return "[" + std::string{name} + "]";
}

std::string Brain::Plan::text() const {
    if (steps.empty()) {
        return no_way;
    }
    std::string out = heading;
    for (std::size_t i = 0; i < steps.size(); ++i) {
        out += std::format(" {}. {}", i + 1, steps[i]);
    }
    return out;
}

Brain::Plan Brain::plan(std::string_view goal) const {
    const AtomOperations ops;
    Plan out;
    out.goal = std::string{goal};
    out.no_way = std::vformat(say("no way"), std::make_format_args(out.goal));
    out.heading = std::vformat(say("to"), std::make_format_args(out.goal));
    const auto lower = [](std::string text) {
        for (char& c : text) {
            c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
        }
        while (!text.empty() && (text.back() == '.' || text.back() == ' ' || text.back() == '!')) {
            text.pop_back();
        }
        while (!text.empty() && text.front() == ' ') {
            text.erase(text.begin());
        }
        return text;
    };
    // The actions: "To <goal>, <action>." as conceptions, newest first.
    struct Action {
        std::string goal;
        std::string action;
        StoredAtom atom;
    };
    std::vector<Action> actions;
    for (const StoredAtom& atom : newest_first()) {
        if (atom.description.category.bytes != affirmation || atom.status == Status::Withdrawn) {
            continue;
        }
        const std::string text{ops.text(atom.description.atom)};
        const std::size_t comma = text.find(',');
        if (comma == std::string::npos || !(text.starts_with("To ") || text.starts_with("to "))) {
            continue;
        }
        actions.push_back({lower(text.substr(3, comma - 3)), lower(text.substr(comma + 1)), atom});
    }
    // Backwards from the goal: what to do, then what to do before that.
    std::vector<std::string> chain;  // the goal first, the first thing to do last
    std::string wanted = lower(std::string{goal});
    for (int depth = 0; depth < 6; ++depth) {
        const auto action = std::ranges::find(actions, wanted, &Action::goal);
        if (action == actions.end()) {
            break;
        }
        if (std::ranges::contains(chain, action->action) || action->action == wanted) {
            break;  // a circle
        }
        if (chain.empty()) {
            chain.push_back(wanted);
        }
        chain.push_back(action->action);
        out.because.push_back(action->atom);
        wanted = action->action;
    }
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        out.steps.push_back(*it);
    }
    return out;
}

std::string Brain::Guess::reason() const {
    const std::string a(word.begin(), word.end());
    const std::string b(other.begin(), other.end());
    const AtomOperations ops;
    if (shared == bytes_of("form")) {
        return std::format("analogy: \"{}\" is a form of \"{}\", and {} (R8)", a, b, ops.text(like.description.atom));
    }
    return std::format("analogy: {} and {} are both kinds of {}, and {} (R8)", a, b,
                       std::string(shared.begin(), shared.end()), ops.text(like.description.atom));
}

std::optional<Brain::Guess> Brain::analogy(const Description& question) const {
    static const Bytes kind_of = bytes_of("is a kind of");
    static const Bytes form_of = bytes_of("form of");
    std::vector<Core> forms;
    if (question.category.bytes == bytes_of("question")) {
        forms = statements(question);
    }
    if (forms.empty()) {
        forms.push_back(core(question));
    }
    // What a word is a kind of, one step; and the base it is a form of.
    const auto kinds_of = [&](const Bytes& word) {
        std::vector<Bytes> out;
        for (const Bond& bond : memory_->bonds_from(BondEnd{BondEnd::Kind::Entity, word})) {
            if (bond.kind == kind_of && bond.to.kind == BondEnd::Kind::Entity) {
                out.push_back(bond.to.bytes);
            }
        }
        return out;
    };
    // The bases a word may be a form of: itself, the "form of" bonds, the
    // known base (A4) and every base its endings allow ("likes": like, lik).
    const auto bases_of = [&](const Bytes& word) {
        std::vector<Bytes> out = {word};
        for (const Bond& bond : memory_->bonds_from(BondEnd{BondEnd::Kind::Entity, word})) {
            if (bond.kind == form_of && bond.to.kind == BondEnd::Kind::Entity) {
                out.push_back(bond.to.bytes);
            }
        }
        if (const std::optional<Form> form = assimilation_.form_of(word, memory_)) {
            out.push_back(form->base);
        }
        for (const Form& form : assimilation_.forms().candidates(word)) {
            out.push_back(form.base);
        }
        return out;
    };
    // The kinds of a word, or of a base of it ("robins": the kinds of "robin").
    const auto kinds_of_either = [&](const Bytes& word) {
        for (const Bytes& base : bases_of(word)) {
            std::vector<Bytes> kinds = kinds_of(base);
            if (!kinds.empty()) {
                return kinds;
            }
        }
        return std::vector<Bytes>{};
    };
    // Two words are one word when a base of one is a base of the other.
    const auto same_word = [&](const Bytes& a, const Bytes& b) {
        if (a == b) {
            return true;
        }
        const std::vector<Bytes> of_a = bases_of(a);
        for (const Bytes& base : bases_of(b)) {
            if (std::ranges::contains(of_a, base)) {
                return true;
            }
        }
        return false;
    };
    // The one base a word is read by, for the key of the search.
    const auto base_of = [&](const Bytes& word) {
        const std::vector<Bytes> bases = bases_of(word);
        return bases.size() > 1 ? bases[1] : bases.front();
    };
    for (const Core& form : forms) {
        if (form.words.size() < 2) {
            continue;
        }
        std::optional<Guess> found;
        for (std::size_t i = 0; i < form.words.size() && !found; ++i) {
            const Bytes& word = form.words[i];
            const std::vector<Bytes> my_kinds = kinds_of_either(word);
            if (my_kinds.empty()) {
                continue;
            }
            // The conceptions with the same words but this one, forms of one word
            // counted the same: found through the rarest of the other words that
            // memory holds at all.
            Bytes key;
            std::int64_t fewest = std::numeric_limits<std::int64_t>::max();
            for (std::size_t j = 0; j < form.words.size(); ++j) {
                if (j == i) {
                    continue;
                }
                for (const Bytes& spelling : {form.words[j], base_of(form.words[j])}) {
                    const std::int64_t uses = memory_->count_uses(spelling);
                    if (uses > 0 && uses < fewest) {
                        fewest = uses;
                        key = spelling;
                    }
                }
            }
            if (key.empty()) {
                continue;
            }
            for (const StoredAtom& atom : candidates(Core{{key}, false}, false)) {
                const Core stored = thinking_core(atom);
                if (stored.words.size() != form.words.size()) {
                    continue;
                }
                bool same = true;
                for (std::size_t j = 0; j < form.words.size() && same; ++j) {
                    same = j == i || same_word(form.words[j], stored.words[j]);
                }
                if (!same || same_word(stored.words[i], word)) {
                    continue;
                }
                const Bytes& other = stored.words[i];
                Bytes shared;
                for (const Bytes& kind : kinds_of_either(other)) {
                    if (std::ranges::contains(my_kinds, kind)) {
                        shared = kind;
                        break;
                    }
                }
                if (shared.empty()) {
                    continue;
                }
                found = Guess{stored.negated == form.negated, atom, word, other, shared};
            }
        }
        if (found) {
            return found;
        }
    }
    return std::nullopt;
}

std::vector<Brain::Proposal> Brain::propose() {
    const AtomOperations ops;
    static const Bytes kind_of = bytes_of("is a kind of");
    static const Bytes noun = bytes_of("noun");
    static const std::vector<Bytes> articles = {bytes_of("a"), bytes_of("an"), bytes_of("the")};
    std::vector<Proposal> out;
    // The kinds: for each thing, the things that are a kind of it.
    std::map<Bytes, std::vector<Bytes>> members;
    for (const Bond& bond : memory_->bonds()) {
        if (bond.kind == kind_of && bond.from.kind == BondEnd::Kind::Entity && bond.to.kind == BondEnd::Kind::Entity) {
            members[bond.to.bytes].push_back(bond.from.bytes);
        }
    }
    // What each thing is said to do or be: the affirmations whose subject is
    // the thing (singular or plural, with or without an article), by the rest
    // of their core; and the negated ones, as counter-examples.
    struct Saying {
        std::vector<Bytes> rest;
        bool negated;
        StoredAtom atom;
    };
    const auto sayings_of = [&](const Bytes& thing) {
        std::vector<Saying> list;
        std::vector<Bytes> spellings = {thing};
        Bytes plural = thing;
        plural.push_back('s');
        spellings.push_back(plural);
        for (const auto& [form, value] : rules_->irregular()) {
            std::string v(value.begin(), value.end());
            const std::size_t colon = v.find(':');
            if (colon != std::string::npos && Bytes(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(colon)) == thing &&
                v.ends_with(":plural")) {
                spellings.push_back(form);
            }
        }
        for (const Bytes& spelling : spellings) {
            for (StoredAtom& atom : memory_->containing(spelling)) {
                if (atom.description.category.bytes != affirmation || atom.status == Status::Withdrawn ||
                    relation_of(atom.description)) {
                    continue;  // a defining sentence is no example of what the thing does
                }
                Core c = thinking_core(atom);
                while (!c.words.empty() && std::ranges::contains(articles, c.words.front())) {
                    c.words.erase(c.words.begin());
                }
                if (c.words.size() < 2 || c.words.front() != spelling) {
                    continue;
                }
                list.push_back({std::vector<Bytes>(c.words.begin() + 1, c.words.end()), c.negated, std::move(atom)});
            }
        }
        return list;
    };
    const auto plural_of = [&](const Bytes& thing) {
        for (const auto& [form, value] : rules_->irregular()) {
            std::string v(value.begin(), value.end());
            const std::size_t colon = v.find(':');
            if (colon != std::string::npos && Bytes(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(colon)) == thing &&
                v.ends_with(":plural")) {
                return form;
            }
        }
        Bytes plural = thing;
        const std::string t(thing.begin(), thing.end());
        if (t.ends_with("y") && t.size() > 1 && std::string("aeiou").find(t[t.size() - 2]) == std::string::npos) {
            plural.pop_back();
            plural.push_back('i');
            plural.push_back('e');
            plural.push_back('s');
        } else if (t.ends_with("s") || t.ends_with("x") || t.ends_with("ch") || t.ends_with("sh")) {
            plural.push_back('e');
            plural.push_back('s');
        } else {
            plural.push_back('s');
        }
        return plural;
    };
    for (const auto& [kind, things] : members) {
        if (things.size() < 2) {
            continue;
        }
        // The rests said of at least two kinds, and the counter-examples.
        std::map<std::vector<Bytes>, std::vector<StoredAtom>> said;
        std::map<std::vector<Bytes>, StoredAtom> denied;
        for (const Bytes& thing : things) {
            for (Saying& saying : sayings_of(thing)) {
                if (saying.negated) {
                    denied.emplace(saying.rest, std::move(saying.atom));
                } else {
                    said[saying.rest].push_back(std::move(saying.atom));
                }
            }
        }
        for (auto& [rest, atoms] : said) {
            if (atoms.size() < 2) {
                continue;
            }
            Proposal proposal;
            const Bytes plural = plural_of(kind);
            std::string sentence(plural.begin(), plural.end());
            for (const Bytes& w : rest) {
                sentence += " " + std::string(w.begin(), w.end());
            }
            sentence[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(sentence[0])));
            sentence += ".";
            proposal.sentence = sentence;
            for (const StoredAtom& atom : atoms) {
                proposal.examples.emplace_back(ops.text(atom.description.atom));
            }
            Description d = assimilation_.describe(ops.from_text(sentence), memory_);
            d.category.bytes = bytes_of("assumption");
            d.metadata = ops.metadata(d.category, d.type, d.entities);
            const std::optional<StoredAtom> held = memory_->find(d.metadata);
            if (const auto counter = denied.find(rest); counter != denied.end()) {
                proposal.counter = std::string{ops.text(counter->second.description.atom)};
                if (held && held->status != Status::Withdrawn) {
                    (void)set_status(d.metadata, Status::Withdrawn, "rule: a counter-example withdraws the general atom (R5)");
                    proposal.withdrawn = true;
                }
                out.push_back(std::move(proposal));
                continue;
            }
            if (!held) {
                std::string source = "rule: R5 from";
                for (const std::string& example : proposal.examples) {
                    source += " \"" + example + "\"";
                }
                proposal.stored = remember(d, Status::Proposed, source) == Stored::New;
            }
            out.push_back(std::move(proposal));
        }
    }
    return out;
}

Brain::Attention Brain::attention() const {
    Attention out;
    static const Bytes conflicts{'c', 'o', 'n', 'f', 'l', 'i', 'c', 't', 's', ' ', 'w', 'i', 't', 'h'};
    static const Bytes assumption = bytes_of("assumption");
    // The words it could not describe: the guessed or unknown entities of the recent conceptions, once each.
    std::vector<Bytes> asked;
    for (const StoredAtom& atom : memory_->recent(50)) {
        const Description d = assimilation_.describe(atom.description.atom, memory_);
        for (Question& q : questions(d)) {
            if (!std::ranges::contains(asked, q.word)) {
                asked.push_back(q.word);
                out.questions.push_back(std::move(q));
            }
        }
    }
    // The conflicts nobody settled: both ends still proposed.
    for (const Bond& bond : memory_->bonds()) {
        if (bond.kind != conflicts) {
            continue;
        }
        const std::optional<StoredAtom> from = conception_at(bond.from);
        const std::optional<StoredAtom> to = conception_at(bond.to);
        if (from && to && from->status == Status::Proposed && to->status == Status::Proposed) {
            out.conflicts.push_back(bond);
        }
    }
    // The proposals waiting: assumptions from rules, still proposed.
    for (const StoredAtom& atom : memory_->with_status(Status::Proposed)) {
        if (atom.description.category.bytes == assumption &&
            std::ranges::any_of(atom.sources, [](const std::string& s) { return s.starts_with("rule: R5"); })) {
            out.proposals.push_back(atom);
        }
    }
    return out;
}

std::string Brain::Attention::text() const {
    return std::format("{} words to ask about, {} conflicts to settle, {} proposals waiting", questions.size(),
                       conflicts.size(), proposals.size());
}

Brain::Thought Brain::think(double seconds) {
    using Clock = std::chrono::steady_clock;
    const Clock::time_point start = Clock::now();
    const auto elapsed = [&] { return std::chrono::duration<double>(Clock::now() - start).count(); };
    Thought out;
    // Novelty over memory (C16, K1): the conflicts among the affirmations it
    // holds, newest first, bonded when found.
    const std::vector<StoredAtom> all = memory_->all();
    for (auto it = all.rbegin(); it != all.rend() && !out.out_of_time; ++it) {
        if (it->description.category.bytes != affirmation || it->status == Status::Withdrawn) {
            continue;
        }
        if (elapsed() > seconds) {
            out.out_of_time = true;
            break;
        }
        const Verdict verdict = decide(it->description, &it->description.metadata.bytes);
        if (verdict.truth != Truth::False || verdict.because.empty()) {
            continue;
        }
        static const Bytes conflicts{'c', 'o', 'n', 'f', 'l', 'i', 'c', 't', 's', ' ', 'w', 'i', 't', 'h'};
        const BondEnd mine = BondEnd::atom(it->description.metadata);
        const BondEnd other = BondEnd::atom(verdict.because.front().description.metadata);
        const bool bonded = std::ranges::any_of(memory_->bonds_of(mine), [&](const Bond& bond) {
            return bond.kind == conflicts && (bond.to == other || bond.from == other);
        });
        if (bonded) {
            continue;
        }
        const Bond bond{conflicts, mine, other,
                        {verdict.rules.empty() ? "comparison: the same core with the opposite polarity (R1), found thinking (S7)"
                                               : "rule: " + verdict.rules.front() + ", found thinking (S7)"}};
        if (this->bond(bond)) {
            out.conflicts_found.push_back(bond);
        }
    }
    if (!out.out_of_time) {
        out.proposals = propose();
    }
    out.waiting = attention();
    out.seconds = elapsed();
    return out;
}

std::string Brain::Thought::text() const {
    std::string out = std::format("thought for {:.2f} s{}: {} conflicts found", seconds, out_of_time ? " (out of time)" : "",
                                  conflicts_found.size());
    std::size_t proposed = 0;
    std::size_t stopped = 0;
    for (const Proposal& p : proposals) {
        (p.counter.empty() ? proposed : stopped) += 1;
    }
    out += std::format(", {} general atoms proposed, {} stopped by a counter-example; {}", proposed, stopped, waiting.text());
    return out;
}

std::optional<Description> Brain::refer(const Description& d) const {
    const AtomOperations ops;
    static const Bytes pronoun = bytes_of("pronoun");
    static const Bytes noun = bytes_of("noun");
    static const Bytes proper_noun = bytes_of("proper noun");
    static const Bytes determiner = bytes_of("determiner");
    static const Bytes adjective = bytes_of("adjective");
    static const Bytes subject_role = bytes_of("subject");
    static const Bytes object_role = bytes_of("object");
    static const Bytes link_role = bytes_of("link");
    static const Bytes plural = bytes_of("plural");
    static const std::vector<std::string> persons = {"he", "she", "him"};
    static const std::vector<std::string> things = {"it"};
    static const std::vector<std::string> plurals = {"they", "them"};
    const std::vector<Entity>& entities = d.entities.entities;
    // What a pronoun may stand for, from a conception: its subject phrase and
    // its object phrases, as written, with their kind and number.
    struct Phrase {
        std::string text;
        bool proper = false;
        bool plural = false;
        bool subject = false;
    };
    const auto phrases_of = [&](const Description& c) {
        std::vector<Phrase> out;
        const auto role_of = [&](const Entity& e) -> Bytes {
            // The role is the type after the features; "guessed" may follow it.
            for (auto it = e.types.rbegin(); it != e.types.rend(); ++it) {
                if (*it == subject_role || *it == object_role || *it == link_role) {
                    return *it;
                }
                if (*it != bytes_of("guessed")) {
                    return {};
                }
            }
            return {};
        };
        for (const Bytes& wanted : {subject_role, object_role}) {
            Phrase phrase;
            bool in_phrase = false;
            std::size_t words = 0;
            bool capital_first = false;  // the phrase's first word, capitalized and of no known category
            const auto flush = [&] {
                if (in_phrase && !phrase.text.empty()) {
                    phrase.subject = wanted == subject_role;
                    // A name nobody taught: one capitalized word of no category.
                    if (words == 1 && capital_first) {
                        phrase.proper = true;
                    }
                    out.push_back(phrase);
                }
                phrase = Phrase{};
                in_phrase = false;
                words = 0;
                capital_first = false;
            };
            for (std::size_t i = 0; i < c.entities.entities.size(); ++i) {
                const Entity& e = c.entities.entities[i];
                const Bytes role = role_of(e);
                const bool joiner = std::ranges::contains(rules_->conjunctions(), ops.fold(e.word));
                const bool joins = (role == link_role || joiner) && in_phrase && i + 1 < c.entities.entities.size() &&
                                   role_of(c.entities.entities[i + 1]) == wanted;
                if (role == wanted || joins) {
                    if (e.category == pronoun && !in_phrase) {
                        continue;  // a pronoun stands for nothing itself
                    }
                    const bool capital = !e.word.empty() && e.word[0] >= 'A' && e.word[0] <= 'Z';
                    if (!in_phrase) {
                        capital_first = capital && e.category.empty();
                    }
                    in_phrase = true;
                    ++words;
                    phrase.text += (phrase.text.empty() ? "" : " ") + std::string(e.word.begin(), e.word.end());
                    if (e.category == proper_noun || (e.category.empty() && capital && i > 0 && !joins)) {
                        phrase.proper = true;
                    }
                    if (std::ranges::contains(e.types, plural) || joins) {
                        phrase.plural = true;
                    }
                } else {
                    flush();
                }
            }
            flush();
        }
        return out;
    };
    std::string text{ops.text(d.atom)};
    bool changed = false;
    std::size_t from = 0;  // where in the text the next pronoun is looked for
    std::vector<StoredAtom> recent;  // fetched when the first pronoun needs it
    bool fetched = false;
    for (std::size_t i = 0; i < entities.size(); ++i) {
        const Entity& e = entities[i];
        if (e.category != pronoun) {
            continue;
        }
        const Bytes folded = ops.fold(e.word);
        const std::string word(folded.begin(), folded.end());
        const bool person = std::ranges::contains(persons, word);
        const bool thing = std::ranges::contains(things, word);
        const bool many = std::ranges::contains(plurals, word);
        // "her" refers when nothing it could own follows.
        const bool her = word == "her" && (i + 1 == entities.size() ||
                                           (entities[i + 1].category != noun && entities[i + 1].category != adjective &&
                                            entities[i + 1].category != determiner));
        if (!person && !thing && !many && !her) {
            continue;
        }
        if (!fetched) {
            recent = newest_first();
            fetched = true;
        }
        // The names in this sentence: a pronoun does not stand for one of them.
        std::vector<std::string> own;
        for (const Entity& other : entities) {
            if (other.category == proper_noun ||
                (other.category.empty() && !other.word.empty() && other.word[0] >= 'A' && other.word[0] <= 'Z' &&
                 &other != &entities.front())) {
                own.emplace_back(other.word.begin(), other.word.end());
            } else if (other.category.empty() && &other == &entities.front() && !other.word.empty() &&
                       other.word[0] >= 'A' && other.word[0] <= 'Z' && entities.size() > 1 &&
                       entities[1].category != noun) {
                own.emplace_back(other.word.begin(), other.word.end());
            }
        }
        std::optional<Phrase> referent;
        for (const StoredAtom& atom : recent) {
            if (atom.description.category.bytes != affirmation || atom.status == Status::Withdrawn) {
                continue;
            }
            const Description resolved =
                atom.reading.empty() ? atom.description : assimilation_.describe(ops.from_text(atom.reading), memory_);
            for (const Phrase& phrase : phrases_of(resolved)) {
                const bool fits = many ? phrase.plural
                                  : thing ? (!phrase.proper && !phrase.plural)
                                          : (phrase.proper && !phrase.plural);
                if (fits && !std::ranges::contains(own, phrase.text)) {
                    referent = phrase;
                    break;
                }
            }
            if (referent) {
                break;
            }
        }
        if (!referent) {
            continue;
        }
        // Replace the pronoun, as a whole word, where it stands in the text.
        const std::string written(e.word.begin(), e.word.end());
        std::size_t at = text.find(written, from);
        while (at != std::string::npos) {
            const bool starts = at == 0 || !std::isalnum(static_cast<unsigned char>(text[at - 1]));
            const bool ends = at + written.size() >= text.size() || !std::isalnum(static_cast<unsigned char>(text[at + written.size()]));
            if (starts && ends) {
                break;
            }
            at = text.find(written, at + 1);
        }
        if (at == std::string::npos) {
            continue;
        }
        std::string replacement = referent->text;
        if (at == 0) {
            replacement[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(replacement[0])));
        } else if (!referent->proper) {
            replacement[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(replacement[0])));
        }
        text.replace(at, written.size(), replacement);
        from = at + replacement.size();
        changed = true;
    }
    if (!changed) {
        return std::nullopt;
    }
    return assimilation_.describe(ops.from_text(text), memory_);
}

std::optional<Reply> Brain::state_of(const Description& question) const {
    const AtomOperations ops;
    const std::vector<Bytes> words = expanded_words(question);
    static const Bytes where = bytes_of("where");
    static const std::vector<Bytes> copulas = {bytes_of("is"), bytes_of("are"), bytes_of("was"), bytes_of("were")};
    static const Bytes in_word = bytes_of("in");
    if (words.size() < 3) {
        return std::nullopt;
    }
    // The shape: "where is X", or "is X in Y".
    std::vector<Bytes> subject;
    std::vector<Bytes> place;
    bool yes_no = false;
    if (words[0] == where && std::ranges::contains(copulas, words[1])) {
        subject.assign(words.begin() + 2, words.end());
    } else if (std::ranges::contains(copulas, words[0])) {
        const auto at = std::ranges::find(words.begin() + 1, words.end(), in_word);
        if (at == words.end() || at == words.begin() + 1 || at + 1 == words.end()) {
            return std::nullopt;
        }
        subject.assign(words.begin() + 1, at);
        place.assign(at + 1, words.end());
        yes_no = true;
    } else {
        return std::nullopt;
    }
    // The latest conception that sets a state of the subject.
    for (const StoredAtom& atom : newest_first()) {
        if (atom.description.category.bytes != affirmation || atom.status == Status::Withdrawn) {
            continue;
        }
        const Core stored = thinking_core(atom);
        if (stored.negated) {
            continue;
        }
        for (const auto& [phrase, state] : rules_->states()) {
            const std::vector<Bytes> parts = split_words(phrase);
            if (parts.empty() || stored.words.size() < parts.size() + 2) {
                continue;
            }
            for (std::size_t p = 1; p + parts.size() < stored.words.size(); ++p) {
                if (!std::equal(parts.begin(), parts.end(), stored.words.begin() + static_cast<std::ptrdiff_t>(p))) {
                    continue;
                }
                const std::vector<Bytes> who(stored.words.begin(), stored.words.begin() + static_cast<std::ptrdiff_t>(p));
                // The subject asked for, or one of the joined subjects ("Mary and John").
                static const Bytes and_word = bytes_of("and");
                bool same = who == subject;
                if (!same && std::ranges::contains(who, and_word)) {
                    std::vector<Bytes> part;
                    for (std::size_t k = 0; k <= who.size() && !same; ++k) {
                        if (k == who.size() || who[k] == and_word) {
                            same = part == subject;
                            part.clear();
                        } else {
                            part.push_back(who[k]);
                        }
                    }
                }
                if (!same) {
                    continue;
                }
                const std::vector<Bytes> object(stored.words.begin() + static_cast<std::ptrdiff_t>(p + parts.size()), stored.words.end());
                const std::string state_text(state.begin(), state.end());
                if (!state_text.starts_with("is in")) {
                    continue;  // a state that is no place: not what "where" asks
                }
                Reply reply;
                std::string sentence;
                for (const Bytes& w : subject) {  // the subject asked for, not the joined one
                    sentence += (sentence.empty() ? "" : " ") + std::string(w.begin(), w.end());
                }
                sentence += " " + state_text;
                for (const Bytes& w : object) {
                    sentence += " " + std::string(w.begin(), w.end());
                }
                if (!sentence.empty()) {
                    sentence[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(sentence[0])));
                }
                sentence += ".";
                if (yes_no) {
                    reply.text = object == place ? say("yes") : say("no");
                    reply.because.push_back(sentence + " (the latest state)");
                } else {
                    reply.text = sentence;
                }
                reply.because.push_back(std::string{ops.text(atom.description.atom)});
                reply.because.push_back("rule: \"" + std::string(phrase.begin(), phrase.end()) + "\" leaves the state \"" +
                                        state_text + "\"; the latest conception stands (R3)");
                return reply;
            }
        }
    }
    return std::nullopt;
}

Brain::Knowledge Brain::knowledge(std::string_view subject) const {
    const AtomOperations ops;
    Knowledge out;
    out.word = ops.fold(std::span{reinterpret_cast<const std::uint8_t*>(subject.data()), subject.size()});
    for (StoredAtom& atom : memory_->containing(out.word)) {
        (atom.status == Status::Validated ? out.validated
         : atom.status == Status::Withdrawn ? out.withdrawn
                                            : out.proposed)
            .push_back(std::move(atom));
    }
    out.categories = memory_->categories_of(out.word);
    for (const WordUse& use : memory_->uses(out.word)) {
        (use.category.empty() ? out.unsure_uses : out.sure_uses) += 1;
    }
    static const Bytes conflicts{'c', 'o', 'n', 'f', 'l', 'i', 'c', 't', 's', ' ', 'w', 'i', 't', 'h'};
    const auto has_word = [&](const StoredAtom& atom) {
        return std::ranges::any_of(atom.description.entities.entities,
                                   [&](const Entity& e) { return ops.fold(e.word) == out.word; });
    };
    for (const Bond& bond : memory_->bonds()) {
        if (bond.kind != conflicts) {
            continue;
        }
        const std::optional<StoredAtom> from = conception_at(bond.from);
        const std::optional<StoredAtom> to = conception_at(bond.to);
        if ((from && has_word(*from)) || (to && has_word(*to))) {
            out.conflicts.push_back(bond);
        }
    }
    for (const Bond& bond : memory_->bonds_of(BondEnd{BondEnd::Kind::Entity, out.word})) {
        if (bond.kind != conflicts) {
            out.bonds.push_back(bond);
        }
    }
    // What it cannot answer: the plain questions about the subject.
    const std::string name{subject};
    if (answers(assimilation_.describe(ops.from_text("What is " + name + "?"), memory_)).empty() &&
        answers(assimilation_.describe(ops.from_text("What is the " + name + "?"), memory_)).empty()) {
        out.cannot.push_back("what " + name + " is");
    }
    if (!state_of(assimilation_.describe(ops.from_text("Where is " + name + "?"), memory_)) &&
        !state_of(assimilation_.describe(ops.from_text("Where is the " + name + "?"), memory_))) {
        out.cannot.push_back("where " + name + " is");
    }
    return out;
}

std::string Brain::Knowledge::text() const {
    const std::string name(word.begin(), word.end());
    std::string out = std::format("\"{}\": {} conceptions ({} validated, {} proposed, {} withdrawn)", name,
                                  validated.size() + proposed.size() + withdrawn.size(), validated.size(),
                                  proposed.size(), withdrawn.size());
    if (!categories.empty()) {
        out += "; known as";
        for (const CategoryCount& c : categories) {
            out += std::format(" {} ({})", std::string(c.category.begin(), c.category.end()), c.count);
        }
    }
    out += std::format("; {} uses with a category, {} guessed or unknown", sure_uses, unsure_uses);
    out += std::format("; {} conflicts, {} other bonds", conflicts.size(), bonds.size());
    if (!cannot.empty()) {
        out += "; I cannot say";
        for (std::size_t i = 0; i < cannot.size(); ++i) {
            out += (i == 0 ? " " : " or ") + cannot[i];
        }
    }
    return out;
}

std::vector<Brain::Question> Brain::questions(const Description& d) const {
    const AtomOperations ops;
    std::vector<Question> out;
    const std::string sentence{ops.text(d.atom)};
    for (std::size_t i = 0; i < d.notes.size() && i < d.entities.entities.size(); ++i) {
        const EntityNote& note = d.notes[i];
        if (note.source != Source::Unknown && note.source != Source::Open && note.source != Source::Guess) {
            continue;
        }
        const Entity& entity = d.entities.entities[i];
        const std::string word{reinterpret_cast<const char*>(entity.word.data()), entity.word.size()};
        Question q{ops.fold(entity.word), sentence, std::format("what category is \"{}\" in \"{}\"?", word, sentence), {}};
        if (note.source == Source::Guess) {
            q.guess = "I take it as " + std::string(entity.category.begin(), entity.category.end()) +
                      (note.form.empty() ? ", from the words around it" : ", because " + note.form);
        } else if (note.source == Source::Open) {
            q.guess = "one of:";
            for (const Bytes& c : note.candidates) {
                q.guess += " " + std::string(c.begin(), c.end());
            }
        } else if (!note.near.empty()) {
            q.guess = "did you mean \"" + std::string(note.near.front().begin(), note.near.front().end()) + "\"?";
        }
        out.push_back(std::move(q));
    }
    return out;
}

std::optional<Stored> Brain::teach(const Sentence& sentence, std::string_view word, std::string_view category,
                                   std::string_view source) {
    const AtomOperations ops;
    const Bytes wanted = ops.fold(std::span{reinterpret_cast<const std::uint8_t*>(word.data()), word.size()});
    const Bytes given(category.begin(), category.end());
    if (!std::ranges::contains(rules_->categories(), given)) {
        return std::nullopt;
    }
    Description d = assimilation_.describe(sentence, memory_);
    static const Bytes guessed{'g', 'u', 'e', 's', 's', 'e', 'd'};
    bool found = false;
    for (std::size_t i = 0; i < d.entities.entities.size(); ++i) {
        Entity& entity = d.entities.entities[i];
        if (ops.fold(entity.word) != wanted) {
            continue;
        }
        found = true;
        entity.category = given;
        std::erase(entity.types, guessed);
        if (i < d.notes.size()) {
            d.notes[i].source = Source::Taught;
            d.notes[i].candidates.clear();
            d.notes[i].near.clear();
            d.notes[i].form.clear();
        }
    }
    if (!found) {
        return std::nullopt;
    }
    assimilation_.redescribe(d);
    return remember(d, Status::Proposed, source);
}

std::vector<Neighbour> Brain::near(const Description& d, int steps, std::size_t limit) const {
    const AtomOperations ops;
    std::vector<BondEnd> from;
    if (memory_->find(d.metadata)) {
        from.push_back(BondEnd::atom(d.metadata));
    } else {
        // A sentence not held: its words are the start.
        for (const Entity& e : d.entities.entities) {
            from.push_back(BondEnd{BondEnd::Kind::Entity, ops.fold(e.word)});
        }
    }
    return memory_->spread(from, steps, limit);
}

std::vector<Neighbour> Brain::near(std::string_view word, int steps, std::size_t limit) const {
    return memory_->spread({BondEnd::entity(word)}, steps, limit);
}

std::string Brain::now() {
    const std::time_t t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &t);
#else
    gmtime_r(&t, &utc);
#endif
    char out[32];
    const std::size_t n = std::strftime(out, sizeof out, "%Y-%m-%dT%H:%M:%SZ", &utc);
    return std::string(out, n);
}

std::optional<Molecule> Brain::molecule_named(const Bytes& name) const {
    if (const std::optional<Molecule> held = memory_->molecule(name)) {
        return held;
    }
    if (cloud_ != nullptr) {
        tell("searching the cloud for the molecule");
        return cloud_->molecule(name);
    }
    return std::nullopt;
}

std::vector<std::string> Brain::validators() const {
    return cloud_ != nullptr ? cloud_->validators() : memory_->validators();
}

bool Brain::is_validator(std::string_view name) const {
    return !name.empty() && std::ranges::contains(validators(), std::string{name});
}

bool Brain::add_validator(std::string_view name, std::string_view by) {
    if (name.empty() || (!validators().empty() && !is_validator(by))) {
        return false;
    }
    memory_->add_validator(name);
    if (cloud_ != nullptr) {
        cloud_->add_validator(name);
    }
    return true;
}

bool Brain::decide(const MetadataElectron& metadata, Status status, std::string_view by) {
    if (!is_validator(by)) {
        throw std::runtime_error(std::format(
            "only a validator decides what is true, and \"{}\" is not one", by));
    }
    return set_status(metadata, status, by);
}

std::vector<StoredAtom> Brain::proposed() const {
    return cloud_ != nullptr ? cloud_->with_status(Status::Proposed)
                             : memory_->with_status(Status::Proposed);
}

std::optional<StoredAtom> Brain::conception(std::int64_t id) const {
    return cloud_ != nullptr ? cloud_->find_id(id) : memory_->find_id(id);
}

bool Brain::set_status(const MetadataElectron& metadata, Status status, std::string_view by) {
    bool any = memory_->set_status(metadata, status, by);
    if (cloud_ != nullptr) {
        if (const std::optional<StoredAtom> held = cloud_->find(metadata)) {
            cloud_->set_status(held->id, status, by);
            any = true;
        }
    }
    if (any && status == Status::Validated && grammar_ != nullptr) {
        if (const std::optional<StoredAtom> held = memory_->find(metadata)) {
            learn_grammar(*held);
        }
    }
    return any;
}

Brain::Synced Brain::sync(std::int64_t pull) {
    if (cloud_ == nullptr) {
        throw std::runtime_error("Brain::sync: there is no cloud; set LARRY_DB");
    }
    const AtomOperations ops;
    Synced out;
    std::int64_t& pushed = out.pushed;
    for (const StoredAtom& atom : memory_->all()) {
        const Description& d = atom.description;
        if (const std::optional<StoredAtom> held = cloud_->find(d.metadata)) {
            // The cloud has it: the cache's description wins when it is newer and complete.
            if (held->description.metadata.bytes != d.metadata.bytes && ops.complete(d.metadata) &&
                cloud_->redescribe(held->id, d.metadata)) {
                ++out.redescribed;
            }
            if (held->reading.empty() && !atom.reading.empty()) {
                cloud_->set_reading(held->id, atom.reading);
            }
            continue;
        }
        if (atom.sources.empty()) {
            cloud_->store(d.atom, d.metadata, atom.status, "");
        }
        for (const std::string& source : atom.sources) {
            cloud_->store(d.atom, d.metadata, atom.status, source);
        }
        if (!atom.decided_by.empty() || !atom.reading.empty()) {
            if (const std::optional<StoredAtom> held = cloud_->find(d.metadata)) {
                if (!atom.decided_by.empty()) {
                    cloud_->set_status(held->id, atom.status, atom.decided_by);
                }
                if (!atom.reading.empty()) {
                    cloud_->set_reading(held->id, atom.reading);
                }
            }
        }
        ++pushed;
    }
    for (const StoredAtom& atom : cloud_->recent(pull)) {
        if (memory_->find(atom.description.metadata)) {
            continue;
        }
        cache(atom);
        ++out.pulled;
    }
    out.refreshed = refresh();
    // N3: the bonds, both ways.
    for (const Bond& bond : memory_->bonds()) {
        if (cloud_->bond(bond)) {
            ++out.bonds_pushed;
        }
    }
    for (const Bond& bond : cloud_->bonds()) {
        if (memory_->bond(bond)) {
            ++out.bonds_pulled;
        }
    }
    // N4: the molecules, both ways: each side appends the members it lacks,
    // by position.
    for (const Bytes& name : memory_->molecules()) {
        const std::optional<Molecule> mine = memory_->molecule(name);
        const std::optional<Molecule> theirs = cloud_->molecule(name);
        const std::size_t held = theirs ? theirs->members.size() : 0;
        for (std::size_t i = held; mine && i < mine->members.size(); ++i) {
            const Member& m = mine->members[i];
            cloud_->join(name, m.identity, m.who, m.when);
            ++out.members_pushed;
        }
    }
    for (const Bytes& name : cloud_->molecules()) {
        const std::optional<Molecule> theirs = cloud_->molecule(name);
        const std::optional<Molecule> mine = memory_->molecule(name);
        const std::size_t held = mine ? mine->members.size() : 0;
        for (std::size_t i = held; theirs && i < theirs->members.size(); ++i) {
            const Member& m = theirs->members[i];
            memory_->join(name, m.identity, m.who, m.when);
            ++out.members_pulled;
        }
    }
    return out;
}

bool Brain::bond(const Bond& bond) {
    const bool fresh = memory_->bond(bond);
    if (cloud_ != nullptr) {
        (void)cloud_->bond(bond);
    }
    return fresh;
}

std::vector<Bond> Brain::bonds_of(const BondEnd& end) const {
    std::vector<Bond> out = memory_->bonds_of(end);
    if (out.empty() && cloud_ != nullptr) {
        tell("searching the cloud for bonds");
        out = cloud_->bonds_of(end);
        for (const Bond& bond : out) {
            (void)memory_->bond(bond);
        }
    }
    return out;
}

std::optional<StoredAtom> Brain::conception_at(const BondEnd& end) const {
    if (end.kind != BondEnd::Kind::Atom) {
        return std::nullopt;
    }
    if (const std::optional<StoredAtom> held = memory_->find_identity(end.bytes)) {
        return held;
    }
    if (cloud_ != nullptr) {
        return cloud_->find_identity(end.bytes);
    }
    return std::nullopt;
}

std::int64_t Brain::refresh() {
    if (cloud_ == nullptr) {
        return 0;
    }
    tell("asking the cloud for its standings");
    const AtomOperations ops;
    std::map<Bytes, Database::Standing> record;
    for (Database::Standing& s : cloud_->standings()) {
        Bytes identity = s.identity;
        record.emplace(std::move(identity), std::move(s));
    }
    std::int64_t changed = 0;
    for (const StoredAtom& atom : memory_->all()) {
        const auto held = record.find(ops.identity(atom.description.metadata));
        if (held == record.end()) {
            continue;
        }
        const Database::Standing& s = held->second;
        if (atom.status != s.status || atom.decided_by != s.decided_by) {
            memory_->set_status(atom.description.metadata, s.status, s.decided_by);
            ++changed;
        }
        if (!s.reading.empty() && atom.reading != s.reading) {
            memory_->set_reading(atom.description.metadata, s.reading);
            ++changed;
        }
    }
    return changed;
}

std::vector<Bytes> Brain::spellings(const Bytes& word) const {
    // A core word in digits was stored as "three" or as "3": both are looked up.
    std::vector<Bytes> out{word};
    for (const auto& [number, digits] : rules_->number_words()) {
        if (digits == word && !in(out, number)) {
            out.push_back(number);
        }
    }
    return out;
}

std::vector<StoredAtom> Brain::candidates(const Core& form, bool cloud) const {
    std::vector<StoredAtom> out;
    if (form.words.empty() || (cloud && cloud_ == nullptr)) {
        return out;
    }
    const auto uses_of = [&](const Bytes& word) {
        std::size_t uses = 0;
        for (const Bytes& spelling : spellings(word)) {
            uses += static_cast<std::size_t>(memory_->count_uses(spelling));
        }
        return uses;
    };
    const Bytes* rarest = &form.words.front();
    std::size_t fewest = uses_of(*rarest);
    for (const Bytes& word : form.words) {
        const std::size_t uses = uses_of(word);
        if (uses < fewest) {
            fewest = uses;
            rarest = &word;
        }
    }
    std::vector<Bytes> taken;  // metadata of what is in `out` already
    const auto take = [&](StoredAtom& atom) {
        if (atom.description.category.bytes != affirmation || atom.status == Status::Withdrawn ||
            std::ranges::contains(taken, atom.description.metadata.bytes)) {
            return;
        }
        taken.push_back(atom.description.metadata.bytes);
        out.push_back(std::move(atom));
    };
    const std::vector<Bytes> wanted = spellings(*rarest);
    if (!cloud) {
        // N6: the atoms in play first, when they hold the word; as the cache
        // holds them now, since the record rules (a standing may have changed).
        const AtomOperations ops;
        for (const StoredAtom& atom : working_) {
            const bool holds = std::ranges::any_of(atom.description.entities.entities, [&](const Entity& e) {
                return std::ranges::contains(wanted, ops.fold(e.word));
            });
            if (!holds) {
                continue;
            }
            if (std::optional<StoredAtom> fresh = memory_->find(atom.description.metadata)) {
                take(*fresh);
            }
        }
    }
    for (const Bytes& spelling : wanted) {
        if (cloud) {
            tell("searching the cloud");
        }
        std::vector<StoredAtom> found = cloud ? cloud_->containing(spelling) : memory_->containing(spelling);
        for (StoredAtom& atom : found) {
            take(atom);
        }
    }
    return out;
}

std::vector<Bytes> Brain::expanded_words(const Description& d) const {
    static const Bytes interjection = bytes_of("interjection");
    const AtomOperations ops;
    std::vector<Bytes> out;
    for (const Entity& entity : d.entities.entities) {
        if (entity.category == interjection) {
            continue;  // "Oh, the sky is blue" claims what "the sky is blue" claims
        }
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

Core Brain::core_of(std::vector<Bytes> words, const std::vector<Bytes>& determiners) const {
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
        // A number word reads as its digits: "three" is "3"; not a determiner
        // that is also a number word ("um", "uma" in Portuguese).
        if (!std::ranges::contains(determiners, word)) {
            for (const auto& [number, digits] : rules_->number_words()) {
                if (number == word) {
                    word = digits;
                    break;
                }
            }
        }
        out.words.push_back(std::move(word));
    }
    out.negated = negations % 2 == 1;
    return out;
}

namespace {

bool is_digits(const Bytes& word) {
    return !word.empty() && std::ranges::all_of(word, [](std::uint8_t c) { return c >= '0' && c <= '9'; });
}

}  // namespace

Bytes Brain::exclusive_group(const Bytes& a, const Bytes& b) const {
    if (a == b) {
        return {};
    }
    if (is_digits(a) && is_digits(b)) {
        return bytes_of("number");
    }
    for (const BaseRules::Exclusive& group : rules_->exclusives()) {
        if (in(group.words, a) && in(group.words, b)) {
            return group.name.empty() ? bytes_of("exclusive") : group.name;
        }
    }
    return {};
}

Core Brain::core(const Description& d) const {
    static const Bytes determiner = bytes_of("determiner");
    const AtomOperations ops;
    std::vector<Bytes> determiners;
    for (const Entity& e : d.entities.entities) {
        if (e.category == determiner) {
            determiners.push_back(ops.fold(e.word));
        }
    }
    return core_of(expanded_words(d), determiners);
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

Reading Brain::read(const Description& said) const {
    return tolerance_.read(said, assimilation_, memory_);
}

Report Brain::judge(const Description& d) const {
    return harness_.judge(d, memory_, cloud_);
}

std::string Qualifying::text() const {
    const AtomOperations ops;
    const auto named = [](Qualification q) {
        return std::format("{} ({})", name(q), user_name(q));
    };
    std::string out = std::format("{}, by the {}", named(by_rules), rule);
    if (examples.empty()) {
        return out + "; no conception has the same structure";
    }
    std::string list;
    for (std::size_t i = 0; i < examples.size() && i < 3; ++i) {
        list += i == 0 ? "" : ", ";
        list += ops.text(examples[i].description.atom);
    }
    if (examples.size() > 3) {
        list += std::format(" and {} more", examples.size() - 3);
    }
    const std::string which = std::format("{} {} conception{} of the same structure{}: {}", examples.size(),
                                          validated ? "validated" : "proposed", examples.size() == 1 ? "" : "s",
                                          from_cloud ? " (from the cloud)" : "", list);
    if (!by_examples) {
        return out + "; the " + which + " disagree among themselves";
    }
    if (*by_examples == by_rules) {
        return out + ", and by the " + which;
    }
    return out + "; but " + named(*by_examples) + " by the " + which;
}

Qualifying Brain::qualify(const Description& d) const {
    Qualifying out;
    const Qualified by_rules = cognition_.qualification(d.atom, d.entities, *rules_);
    out.by_rules = by_rules.qualification;
    out.rule = by_rules.rule;
    // The conceptions with a word of the sentence that have its structure (C5).
    const AtomOperations ops;
    std::vector<StoredAtom> validated;
    std::vector<StoredAtom> proposed;
    for (const bool cloud : {false, true}) {
        if (cloud && (cloud_ == nullptr || !validated.empty() || !proposed.empty())) {
            break;
        }
        std::vector<std::int64_t> seen;
        if (cloud) {
            tell("searching the cloud for sentences of the same structure");
        }
        for (const Entity& e : d.entities.entities) {
            const Bytes word = ops.fold(e.word);
            for (StoredAtom& atom : cloud ? cloud_->containing(word) : memory_->containing(word)) {
                if (std::ranges::contains(seen, atom.id) || atom.status == Status::Withdrawn) {
                    continue;
                }
                seen.push_back(atom.id);
                if (!cognition_.same_structure(d, atom.description).holds) {
                    continue;
                }
                (atom.status == Status::Validated ? validated : proposed).push_back(std::move(atom));
            }
        }
        out.from_cloud = cloud && (!validated.empty() || !proposed.empty());
    }
    out.validated = !validated.empty();
    out.examples = out.validated ? std::move(validated) : std::move(proposed);
    if (out.examples.empty()) {
        return out;
    }
    std::map<Bytes, std::size_t> votes;
    for (const StoredAtom& atom : out.examples) {
        ++votes[atom.description.category.bytes];
    }
    const auto most = std::ranges::max_element(votes, [](const auto& a, const auto& b) { return a.second < b.second; });
    const bool tie = std::ranges::count_if(votes, [&](const auto& v) { return v.second == most->second; }) > 1;
    if (!tie) {
        const std::string_view category{reinterpret_cast<const char*>(most->first.data()), most->first.size()};
        out.by_examples = qualification_named(category);
    }
    return out;
}

Verdict Brain::truth(const Sentence& claim) const {
    const Description said = assimilation_.describe(claim, memory_);
    const Reading reading = read(said);
    if (!reading.accepted) {
        Verdict verdict;
        verdict.refused = true;
        verdict.deviations = reading.deviations;
        verdict.deviations.push_back(reading.reason);
        return verdict;
    }
    const Description& meant = reading.changed ? reading.meant : said;
    Verdict verdict = truth(meant);
    verdict.deviations = reading.deviations;
    if (reading.changed) {
        const AtomOperations ops;
        verdict.reading = std::string{ops.text(reading.meant.atom)};
    }
    verdict.unusual = judge(meant).unusual();
    return verdict;
}

Verdict Brain::truth(const Description& claim) const {
    Verdict verdict = decide(claim);
    // N6: what decided it, and what came nearest, is in play now.
    for (const StoredAtom& atom : verdict.because) {
        bring_into_play(atom);
    }
    for (const StoredAtom& atom : verdict.nearest) {
        bring_into_play(atom);
    }
    return verdict;
}

void Brain::bring_into_play(const StoredAtom& atom) const {
    std::erase_if(working_, [&](const StoredAtom& held) {
        return held.description.metadata.bytes == atom.description.metadata.bytes;
    });
    working_.insert(working_.begin(), atom);
    if (working_.size() > working_limit) {
        working_.resize(working_limit);
    }
}

Verdict Brain::decide(const Description& claim, const Bytes* except) const {
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
                if (except != nullptr && atom.description.metadata.bytes == *except) {
                    continue;  // thinking: a conception is no evidence for itself
                }
                const Core stored = thinking_core(atom);
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
    // K1: a conception that gives the same thing another exclusive attribute
    // makes the claim false, and its negation true. The cache first.
    for (const bool cloud : {false, true}) {
        for (const Core& form : forms) {
            std::map<std::int64_t, StoredAtom> seen;
            for (const Bytes& word : form.words) {
                for (StoredAtom& atom : candidates(Core{{word}, false}, cloud)) {
                    if (except != nullptr && atom.description.metadata.bytes == *except) {
                        continue;
                    }
                    seen.try_emplace(atom.id, std::move(atom));
                }
            }
            for (auto& [id, atom] : seen) {
                const Core stored = core(atom.description);
                if (stored.negated || stored.words.size() != form.words.size()) {
                    continue;
                }
                std::size_t differing = form.words.size();
                std::size_t count = 0;
                for (std::size_t i = 0; i < form.words.size(); ++i) {
                    if (form.words[i] != stored.words[i]) {
                        differing = i;
                        ++count;
                    }
                }
                if (count != 1) {
                    continue;
                }
                const Bytes group = exclusive_group(form.words[differing], stored.words[differing]);
                if (group.empty()) {
                    continue;
                }
                const auto text_of = [](const Bytes& b) {
                    return std::string(b.begin(), b.end());
                };
                verdict.truth = form.negated ? Truth::True : Truth::False;
                verdict.from_cloud = cloud;
                verdict.rules.push_back(std::format(
                    "{} and {} are both of the kind {}, and a thing has one at a time",
                    text_of(form.words[differing]), text_of(stored.words[differing]),
                    text_of(group)));
                if (cloud) {
                    cache(atom);
                }
                verdict.because.push_back(std::move(atom));
                return verdict;
            }
        }
    }
    // R4 (first step): "a sparrow is an animal" follows from "a sparrow is a
    // bird" and "a bird is an animal": the chain of "is a kind of" bonds.
    for (const Core& form : forms) {
        if (const std::optional<Relation> relation = relation_in(form, claim); relation && relation->kind == bytes_of("is a kind of")) {
            const std::vector<Bytes> path = chain(relation->from, relation->to);
            if (path.size() >= 3) {
                std::string steps;
                for (std::size_t i = 0; i + 1 < path.size(); ++i) {
                    steps += (i == 0 ? "" : ", ") + std::string(path[i].begin(), path[i].end()) + " is a kind of " +
                             std::string(path[i + 1].begin(), path[i + 1].end());
                }
                verdict.truth = form.negated ? Truth::False : Truth::True;
                verdict.rules.push_back(steps + " (chained, R4)");
                const AtomOperations ops;
                for (std::size_t i = 0; i + 1 < path.size(); ++i) {
                    for (const Bond& bond : memory_->bonds_from(BondEnd{BondEnd::Kind::Entity, path[i]})) {
                        if (bond.to.bytes != path[i + 1]) {
                            continue;
                        }
                        for (const std::string& origin : bond.origins) {
                            if (origin.starts_with("conception: ")) {
                                const Description said = assimilation_.describe(ops.from_text(origin.substr(12)), memory_);
                                if (const std::optional<StoredAtom> held = memory_->find(said.metadata)) {
                                    verdict.because.push_back(*held);
                                }
                            }
                        }
                    }
                }
                return verdict;
            }
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
    std::vector<StoredAtom> out = search_answers(question);
    for (const StoredAtom& atom : out) {
        bring_into_play(atom);  // N6
    }
    return out;
}

std::vector<StoredAtom> Brain::search_answers(const Description& question) const {
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
            const Core stored = thinking_core(atom);
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

namespace {

// The words of a pattern or a sentence, folded, split at spaces.
std::vector<std::string> words_of(std::string_view text) {
    std::vector<std::string> out;
    std::string current;
    for (const char c : text) {
        if (c == ' ') {
            if (!current.empty()) {
                out.push_back(std::move(current));
                current.clear();
            }
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty()) {
        out.push_back(std::move(current));
    }
    return out;
}

// Matches the pattern's words from `p` against the sentence's from `w`; a
// '*' takes one or more words, as an argument.
bool match_words(const std::vector<std::string>& pattern, std::size_t p, const std::vector<std::string>& folded,
                 const std::vector<std::string>& written, std::size_t w, std::vector<std::string>& arguments) {
    if (p == pattern.size()) {
        return w == folded.size();
    }
    if (pattern[p] != "*") {
        return w < folded.size() && folded[w] == pattern[p] &&
               match_words(pattern, p + 1, folded, written, w + 1, arguments);
    }
    for (std::size_t take = 1; w + take <= folded.size(); ++take) {
        std::string argument;
        for (std::size_t i = w; i < w + take; ++i) {
            argument += (i == w ? "" : " ") + written[i];
        }
        arguments.push_back(std::move(argument));
        if (match_words(pattern, p + 1, folded, written, w + take, arguments)) {
            return true;
        }
        arguments.pop_back();
    }
    return false;
}

}  // namespace

std::optional<Command> Brain::command(const Description& d) const {
    const AtomOperations ops;
    // The words of the sentence as written, split at spaces, with the marks
    // around each stripped: a file name keeps its dot ("notes.txt").
    std::vector<std::string> folded;
    std::vector<std::string> written;
    for (std::string word : words_of(ops.text(d.atom))) {
        const auto is_mark = [&](char c) {
            return in(rules_->punctuation(), Bytes{static_cast<std::uint8_t>(c)}) ||
                   in(rules_->sentence_ends(), Bytes{static_cast<std::uint8_t>(c)}) ||
                   in(rules_->closers(), Bytes{static_cast<std::uint8_t>(c)}) || c == '"' || c == '\'';
        };
        while (!word.empty() && is_mark(word.front())) {
            word.erase(word.begin());
        }
        while (!word.empty() && is_mark(word.back())) {
            word.pop_back();
        }
        if (word.empty()) {
            continue;
        }
        const Bytes f = ops.fold(Bytes(word.begin(), word.end()));
        const std::string lower(f.begin(), f.end());
        if (folded.empty() && (lower == "please" || lower == "larry")) {
            continue;
        }
        folded.push_back(lower);
        written.push_back(std::move(word));
    }
    if (folded.empty()) {
        return std::nullopt;
    }
    // A polite request ("could you search for the sea") is the command its
    // words make; one that makes none stays a request Larry cannot do.
    const auto match = [&](const std::vector<std::string>& f, const std::vector<std::string>& w,
                           auto& self) -> std::optional<Command> {
        for (const auto& [pattern, operation] : rules_->commands()) {
            const std::string text(pattern.begin(), pattern.end());
            const std::string op(operation.begin(), operation.end());
            std::vector<std::string> arguments;
            if (!match_words(words_of(text), 0, f, w, 0, arguments)) {
                continue;
            }
            if (op != "request") {
                return Command{op, std::move(arguments), text};
            }
            const std::vector<std::string> inner_written = words_of(arguments.front());
            std::vector<std::string> inner_folded;
            for (const std::string& word : inner_written) {
                const Bytes fw = ops.fold(Bytes(word.begin(), word.end()));
                inner_folded.emplace_back(fw.begin(), fw.end());
            }
            if (inner_folded.empty() || inner_folded.front() == "please") {
                return Command{op, std::move(arguments), text};
            }
            if (std::optional<Command> inner = self(inner_folded, inner_written, self)) {
                inner->pattern = text + " / " + inner->pattern;
                return inner;
            }
            return Command{op, std::move(arguments), text};
        }
        return std::nullopt;
    };
    return match(folded, written, match);
}

Brain::Recognition Brain::recognize(const Sentence& sentence) const {
    Recognition out;
    out.description = assimilation_.describe(sentence, memory_);
    const Description& d = out.description;
    const Qualified q = cognition_.qualification(d.atom, d.entities, *rules_);
    out.qualification = q.qualification;
    out.kind_reason = q.rule;
    switch (q.qualification) {
    case Qualification::Affirmation:
        out.kind = "statement";
        break;
    case Qualification::Question:
        out.kind = "question";
        break;
    case Qualification::Order:
        out.kind = "request";
        break;
    case Qualification::Assumption:
        out.kind = "assumption";
        break;
    case Qualification::Expression:
        out.kind = "expression";
        break;
    }
    out.command = command(d);
    if (out.command && out.command->operation != "request") {
        out.kind = "request";
        out.kind_reason = std::format("the command \"{}\" ({})", out.command->pattern, out.command->operation);
    } else if (out.command) {
        out.kind = "request";
        out.kind_reason = std::format("a polite request (\"{}\") that is no command Larry knows", out.command->pattern);
    }
    out.calculation = calculate(sentence);
    const Assimilation::Emotion emotion = assimilation_.emotion(d);
    out.emotion = std::string(emotion.feeling.begin(), emotion.feeling.end());
    out.emotion_reason = emotion.marker.empty()
                             ? "no marker of sarcasm and no emotion word"
                             : (out.emotion == "sarcasm" ? "the marker \"" + emotion.marker + "\""
                                                        : "the word \"" + emotion.marker + "\"");
    return out;
}

std::vector<std::string> Brain::abilities() const {
    std::vector<std::string> out;
    std::vector<std::string> seen;
    for (const auto& [pattern, operation] : rules_->commands()) {
        const std::string op(operation.begin(), operation.end());
        if (op == "request" || std::ranges::contains(seen, op)) {
            continue;
        }
        seen.push_back(op);
        out.emplace_back(pattern.begin(), pattern.end());
    }
    return out;
}

std::optional<Calculation> Brain::calculate(const Sentence& sentence) const {
    const AtomOperations ops;
    if (std::optional<Calculation> date = calendar_.calculate(ops.text(sentence))) {
        return date;
    }
    if (std::optional<Calculation> unknown = algebra_.calculate(ops.text(sentence))) {
        return unknown;
    }
    return arithmetic_.calculate(ops.text(sentence));
}

Reply Brain::hear(const Sentence& sentence, std::string_view source) {
    return respond(sentence, source, true);
}

Reply Brain::answer(const Sentence& sentence) {
    return respond(sentence, "", false);
}

Reply Brain::respond(const Sentence& sentence, std::string_view source, bool store) {
    const AtomOperations ops;
    last_notice_.clear();
    // What was said is what gets stored; what was meant, by the reading
    // within the tolerance (K3), is what Larry thinks with.
    const Description said = assimilation_.describe(sentence, memory_);
    Reply reply;
    // W3: an order Larry knows how to do, by its words, before anything else.
    if (std::optional<Command> cmd = command(said); cmd && cmd->operation == "request") {
        reply.text = say("cannot do");
        for (const std::string& ability : abilities()) {
            if (ability.starts_with("can you") || ability.starts_with("please")) {
                continue;
            }
            reply.text += " " + ability + ",";
        }
        reply.text.back() = '.';
        reply.because.emplace_back("rule: a request that matches no command waits (S1)");
        return reply;
    } else if (cmd) {
        std::string what = cmd->operation;
        for (const std::string& argument : cmd->arguments) {
            what += " \"" + argument + "\"";
        }
        reply.text = std::vformat(say("can do"), std::make_format_args(what));
        reply.because.push_back(std::format("command: \"{}\" is {}", cmd->pattern, cmd->operation));
        reply.command = std::move(cmd);
        return reply;
    }
    // M1: a calculation is done, not looked up, and nothing is stored.
    if (const std::optional<Calculation> calc = calculate(sentence)) {
        if (!calc->defined) {
            reply.text = std::vformat(say("that is"), std::make_format_args(calc->result));
        } else if (calc->comparison) {
            reply.text = store ? (calc->holds ? say("yes") : say("no")) : (calc->holds ? say("true") : say("false"));
        } else {
            reply.text = calc->result;
        }
        reply.because.push_back("rule: " + calc->rule());
        return reply;
    }
    const Reading reading = read(said);
    if (!reading.accepted) {
        reply.text = say("cannot read");
        for (std::size_t i = 0; i < reading.deviations.size(); ++i) {
            reply.text += (i == 0 ? ": " : "; ") + reading.deviations[i];
        }
        reply.text += ".";
        reply.because.push_back("rule: " + reading.reason);
        return reply;
    }
    Description d = reading.changed ? reading.meant : said;
    for (const std::string& deviation : reading.deviations) {
        reply.because.push_back("deviation: " + deviation);
    }
    // A11: a pronoun stands for what was said before.
    bool referred = false;
    if (std::optional<Description> resolved = refer(d)) {
        d = std::move(*resolved);
        referred = true;
        reply.because.emplace_back("rule: a pronoun stands for the latest thing of its kind said before (A11)");
    }
    if (reading.changed || referred) {
        const std::string read_text{ops.text(d.atom)};
        reply.text = std::vformat(say("read as"), std::make_format_args(read_text));
        reply.because.push_back(std::format("read as: {}", ops.text(d.atom)));
    }
    // K4: what is unusual goes into the reasons; a stored affirmation says it too.
    const std::vector<std::string> unusual = judge(d).unusual();
    for (const std::string& line : unusual) {
        reply.because.push_back("unusual: " + line);
    }
    const std::string_view qualification{reinterpret_cast<const char*>(d.category.bytes.data()),
                                         d.category.bytes.size()};
    const auto text_of = [&](const StoredAtom& atom) {
        return std::string{ops.text(atom.description.atom)};
    };
    if (qualification == "expression") {
        reply.text += std::string{ops.text(sentence)};
        reply.because.emplace_back("rule: an expression is answered in kind");
        return reply;
    }
    if (qualification == "order") {
        reply.text += say("cannot do");
        for (const std::string& ability : abilities()) {
            reply.text += " " + ability + ",";
        }
        reply.text.back() = '.';
        reply.because.emplace_back("rule: an order that matches no command waits (S1)");
        return reply;
    }
    const std::string read_as = reading.changed || referred ? std::string{ops.text(d.atom)} : std::string{};
    if (qualification == "assumption") {
        if (!store) {
            reply.text += say("assumption not judged");
            reply.because.emplace_back("rule: an assumption is kept apart from the truths");
            return reply;
        }
        remember(said, Status::Proposed, source, read_as);
        reply.stored = true;
        reply.text += say("assumption noted");
        reply.because.emplace_back("rule: an assumption is kept apart from the truths");
        return reply;
    }
    if (qualification == "question") {
        if (std::optional<Reply> state = state_of(d)) {
            return *state;  // R3: the present state, from the latest conception that set it
        }
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
        if (verdict.truth == Truth::Unknown) {
            if (const std::optional<Guess> guess = analogy(d)) {
                reply.text += guess->yes ? say("probably yes") : say("probably no");
                reply.because.push_back(guess->reason());
                reply.because.emplace_back("rule: a guess by analogy is a guess, not a truth (R8)");
                return reply;
            }
        }
        switch (verdict.truth) {
        case Truth::True:
            reply.text += say("yes");
            break;
        case Truth::False:
            reply.text += say("no");
            break;
        case Truth::Unknown:
            reply.text += say("unknown");
            break;
        }
        for (const StoredAtom& atom : verdict.because) {
            reply.because.push_back((verdict.from_cloud ? "cloud: " : "") + text_of(atom));
        }
        for (const std::string& rule : verdict.rules) {
            reply.because.push_back("rule: " + rule);
        }
        for (const StoredAtom& atom : verdict.nearest) {
            reply.because.push_back("nearest: " + text_of(atom));
        }
        if (verdict.truth == Truth::Unknown && !verdict.nearest.empty()) {
            const std::string nearest_text = text_of(verdict.nearest.front());
            reply.text += " " + std::vformat(say("i know colon"), std::make_format_args(nearest_text));
        }
        return reply;
    }
    // A claim to judge, when nothing is stored: true, false or I don't know,
    // with the conception, its standing, the rule and the nearest conceptions.
    const Verdict verdict = truth(d);
    if (!store) {
        if (verdict.truth == Truth::Unknown) {
            if (const std::optional<Guess> guess = analogy(d)) {  // R8
                reply.text += guess->yes ? say("probably true") : say("probably false");
                reply.because.push_back(guess->reason());
                reply.because.emplace_back("rule: a guess by analogy is a guess, not a truth (R8)");
                return reply;
            }
        }
        switch (verdict.truth) {
        case Truth::True:
            reply.text += say("true");
            break;
        case Truth::False:
            reply.text += say("false");
            break;
        case Truth::Unknown:
            reply.text += say("unknown claim");
            break;
        }
        for (const StoredAtom& atom : verdict.because) {
            reply.because.push_back(
                (verdict.from_cloud ? "cloud: " : "") + text_of(atom) +
                (atom.status == Status::Proposed  ? " (proposed)"
                 : atom.status == Status::Withdrawn ? " (withdrawn)"
                 : atom.decided_by.empty()          ? ""
                                                    : std::format(" (validated by {})", atom.decided_by)));
        }
        for (const std::string& rule : verdict.rules) {
            reply.because.push_back("rule: " + rule);
        }
        for (const StoredAtom& atom : verdict.nearest) {
            reply.because.push_back("nearest: " + text_of(atom));
        }
        if (verdict.truth == Truth::Unknown && !verdict.nearest.empty()) {
            const std::string nearest_text = text_of(verdict.nearest.front());
            reply.text += ". " + std::vformat(say("i know colon"), std::make_format_args(nearest_text));
        }
        return reply;
    }
    // An affirmation: what does memory hold already? (C16, first step)
    const Stored stored = remember(said, Status::Proposed, source, read_as);
    reply.stored = stored == Stored::New;
    if (verdict.truth == Truth::True) {
        const std::string known_text = text_of(verdict.because.front());
        reply.text += stored == Stored::New ? std::vformat(say("i know"), std::make_format_args(known_text)) : say("already know");
        reply.because.push_back(text_of(verdict.because.front()));
        return reply;
    }
    if (verdict.truth == Truth::False) {
        const StoredAtom earlier = verdict.because.front();
        for (const std::string& rule : verdict.rules) {
            reply.because.push_back("rule: " + rule);
        }
        // N3: the conflict is a bond between the two conceptions, from the
        // rule or the comparison that found it.
        static const Bytes conflicts{'c', 'o', 'n', 'f', 'l', 'i', 'c', 't', 's', ' ', 'w', 'i', 't', 'h'};
        const std::string origin = verdict.rules.empty()
                                       ? "comparison: the same core with the opposite polarity (R1)"
                                       : "rule: " + verdict.rules.front();
        (void)bond(Bond{conflicts, BondEnd::atom(said.metadata), BondEnd::atom(earlier.description.metadata), {origin}});
        // Q14 (R2c): from the same source, the later stands and the earlier is
        // withdrawn, unless a validator decided the earlier: then only a
        // validator undoes it, and Larry asks (G5).
        const bool same_source = !earlier.sources.empty() &&
                                 std::ranges::all_of(earlier.sources, [&](const std::string& s) { return s == source; });
        if (same_source && earlier.status != Status::Validated) {
            (void)set_status(earlier.description.metadata, Status::Withdrawn,
                             "rule: the later from the same source stands (Q14)");
            const std::string earlier_text = text_of(earlier);
            reply.text += std::vformat(say("contradicts"), std::make_format_args(earlier_text));
            reply.because.push_back("withdrawn: " + text_of(earlier));
            reply.because.emplace_back("rule: from the same source, the later stands and the earlier is withdrawn (Q14, R2)");
            reply.because.emplace_back("bond: conflicts with, recorded (N3)");
            return reply;
        }
        const std::string earlier_text = text_of(earlier);
        const std::string said_text{ops.text(said.atom)};
        reply.text += std::vformat(say("conflicts"), std::make_format_args(earlier_text, said_text, earlier_text));
        reply.because.push_back(text_of(earlier));
        reply.because.emplace_back(same_source
                                       ? "rule: a validator decided the earlier; only a validator undoes it (R2b), so Larry asks (G5)"
                                       : "rule: from different sources, both are kept and marked, and Larry asks (Q14, R2, G5)");
        reply.because.emplace_back("bond: conflicts with, recorded (N3)");
        return reply;
    }
    reply.text += say("noted");
    reply.because.emplace_back("rule: an affirmation is stored as a conception");
    for (const std::string& line : unusual) {
        reply.text += " " + line + ".";
    }
    bool asked = false;
    for (std::size_t i = 0; i < d.notes.size(); ++i) {
        const Entity& entity = d.entities.entities[i];
        const std::string_view word{reinterpret_cast<const char*>(entity.word.data()),
                                    entity.word.size()};
        if (d.notes[i].source == Source::Guess) {
            const std::string taken_word{word};
            const std::string taken_category(entity.category.begin(), entity.category.end());
            const std::string by_form = d.notes[i].form.empty() ? std::string{} : say("by its form");
            reply.text += " " + std::vformat(say("i take"), std::make_format_args(taken_word, taken_category, by_form));
            if (d.notes[i].form.empty()) {
                reply.because.emplace_back("rule: an unknown word takes the category of known words in the same context, as a guess (A6)");
            } else {
                reply.because.emplace_back("rule: " + d.notes[i].form + " (A4)");
            }
        } else if (d.notes[i].source == Source::Unknown && !asked) {
            const std::string asked_word{word};
            reply.text += " " + std::vformat(say("what is"), std::make_format_args(asked_word));
            reply.because.emplace_back("rule: Larry asks about a word it does not know (A5)");
            if (!d.notes[i].near.empty()) {
                const Bytes& near = d.notes[i].near.front();
                const std::string near_word(near.begin(), near.end());
                reply.text += " " + std::vformat(say("did you mean"), std::make_format_args(near_word));
                reply.because.emplace_back("rule: the dictionary knows a word one slip away (A2b)");
            }
            asked = true;
        }
    }
    return reply;
}

}  // namespace larry
