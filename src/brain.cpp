#include "larry/brain.hpp"

#include "larry/atom_operations.hpp"
#include "larry/grammar.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <format>
#include <map>
#include <optional>
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
        // A number word reads as its digits: "three" is "3".
        for (const auto& [number, digits] : rules_->number_words()) {
            if (number == word) {
                word = digits;
                break;
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

Verdict Brain::decide(const Description& claim) const {
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
    // K1: a conception that gives the same thing another exclusive attribute
    // makes the claim false, and its negation true. The cache first.
    for (const bool cloud : {false, true}) {
        for (const Core& form : forms) {
            std::map<std::int64_t, StoredAtom> seen;
            for (const Bytes& word : form.words) {
                for (StoredAtom& atom : candidates(Core{{word}, false}, cloud)) {
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
        reply.text = "I cannot do that yet. I can:";
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
        reply.text = "I can do that: " + what + ".";
        reply.because.push_back(std::format("command: \"{}\" is {}", cmd->pattern, cmd->operation));
        reply.command = std::move(cmd);
        return reply;
    }
    // M1: a calculation is done, not looked up, and nothing is stored.
    if (const std::optional<Calculation> calc = calculate(sentence)) {
        if (!calc->defined) {
            reply.text = "That is " + calc->result + ".";
        } else if (calc->comparison) {
            reply.text = store ? (calc->holds ? "Yes." : "No.") : (calc->holds ? "true" : "false");
        } else {
            reply.text = calc->result;
        }
        reply.because.push_back("rule: " + calc->rule());
        return reply;
    }
    const Reading reading = read(said);
    if (!reading.accepted) {
        reply.text = "I cannot read that";
        for (std::size_t i = 0; i < reading.deviations.size(); ++i) {
            reply.text += (i == 0 ? ": " : "; ") + reading.deviations[i];
        }
        reply.text += ".";
        reply.because.push_back("rule: " + reading.reason);
        return reply;
    }
    const Description& d = reading.changed ? reading.meant : said;
    for (const std::string& deviation : reading.deviations) {
        reply.because.push_back("deviation: " + deviation);
    }
    if (reading.changed) {
        reply.text = std::format("I read it as \"{}\". ", ops.text(d.atom));
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
        reply.text += "I cannot do that yet. I can:";
        for (const std::string& ability : abilities()) {
            reply.text += " " + ability + ",";
        }
        reply.text.back() = '.';
        reply.because.emplace_back("rule: an order that matches no command waits (S1)");
        return reply;
    }
    const std::string read_as = reading.changed ? std::string{ops.text(d.atom)} : std::string{};
    if (qualification == "assumption") {
        if (!store) {
            reply.text += "That is an assumption: I do not judge it.";
            reply.because.emplace_back("rule: an assumption is kept apart from the truths");
            return reply;
        }
        remember(said, Status::Proposed, source, read_as);
        reply.stored = true;
        reply.text += "Noted as an assumption, not as a truth.";
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
            reply.text += "Yes.";
            break;
        case Truth::False:
            reply.text += "No.";
            break;
        case Truth::Unknown:
            reply.text += "I don't know.";
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
            reply.text += " I know: " + text_of(verdict.nearest.front());
        }
        return reply;
    }
    // A claim to judge, when nothing is stored: true, false or I don't know,
    // with the conception, its standing, the rule and the nearest conceptions.
    const Verdict verdict = truth(d);
    if (!store) {
        switch (verdict.truth) {
        case Truth::True:
            reply.text += "true";
            break;
        case Truth::False:
            reply.text += "false";
            break;
        case Truth::Unknown:
            reply.text += "I don't know";
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
            reply.text += ". I know: " + text_of(verdict.nearest.front());
        }
        return reply;
    }
    // An affirmation: what does memory hold already? (C16, first step)
    const Stored stored = remember(said, Status::Proposed, source, read_as);
    reply.stored = stored == Stored::New;
    if (verdict.truth == Truth::True) {
        reply.text += stored == Stored::New ? "I know. " + text_of(verdict.because.front())
                                           : "I already know that.";
        reply.because.push_back(text_of(verdict.because.front()));
        return reply;
    }
    if (verdict.truth == Truth::False) {
        reply.text += "That conflicts with what I know: " + text_of(verdict.because.front()) +
                     " I keep both and note the conflict.";
        reply.because.push_back(text_of(verdict.because.front()));
        for (const std::string& rule : verdict.rules) {
            reply.because.push_back("rule: " + rule);
        }
        reply.because.emplace_back("rule: a conflict is recorded, not chosen silently (R2)");
        // N3: the conflict is a bond between the two conceptions, from the
        // rule or the comparison that found it.
        static const Bytes conflicts{'c', 'o', 'n', 'f', 'l', 'i', 'c', 't', 's', ' ', 'w', 'i', 't', 'h'};
        const std::string origin = verdict.rules.empty()
                                       ? "comparison: the same core with the opposite polarity (R1)"
                                       : "rule: " + verdict.rules.front();
        (void)bond(Bond{conflicts, BondEnd::atom(said.metadata),
                        BondEnd::atom(verdict.because.front().description.metadata), {origin}});
        reply.because.emplace_back("bond: conflicts with, recorded (N3)");
        return reply;
    }
    reply.text += "Noted.";
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
            reply.text += std::format(" I take \"{}\" as {}{}.", word,
                                      std::string_view{reinterpret_cast<const char*>(entity.category.data()),
                                                       entity.category.size()},
                                      d.notes[i].form.empty() ? std::string{} : ", by its form");
            if (d.notes[i].form.empty()) {
                reply.because.emplace_back("rule: an unknown word takes the category of known words in the same context, as a guess (A6)");
            } else {
                reply.because.emplace_back("rule: " + d.notes[i].form + " (A4)");
            }
        } else if (d.notes[i].source == Source::Unknown && !asked) {
            reply.text += std::format(" What is \"{}\"?", word);
            reply.because.emplace_back("rule: Larry asks about a word it does not know (A5)");
            if (!d.notes[i].near.empty()) {
                const Bytes& near = d.notes[i].near.front();
                reply.text += std::format(" Did you mean \"{}\"?",
                                          std::string_view{reinterpret_cast<const char*>(near.data()),
                                                           near.size()});
                reply.because.emplace_back("rule: the dictionary knows a word one slip away (A2b)");
            }
            asked = true;
        }
    }
    return reply;
}

}  // namespace larry
