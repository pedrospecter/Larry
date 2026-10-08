#include "larry/harness.hpp"

#include "larry/atom_operations.hpp"
#include "larry/database.hpp"
#include "larry/grammar.hpp"

#include <algorithm>
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

const Bytes affirmation = bytes_of("affirmation");
const Bytes noun = bytes_of("noun");
const Bytes proper_noun = bytes_of("proper noun");
const Bytes pronoun = bytes_of("pronoun");
const Bytes adjective = bytes_of("adjective");
const Bytes verb = bytes_of("verb");
const Bytes auxiliary_verb = bytes_of("auxiliary verb");
const Bytes preposition = bytes_of("preposition");
const Bytes subject = bytes_of("subject");
const Bytes predicate = bytes_of("predicate");
const Bytes attribute = bytes_of("attribute");
const Bytes object = bytes_of("object");
const Bytes complement = bytes_of("complement");

// The role of an entity: the last type that is one.
Bytes role_of(const Entity& e) {
    for (auto it = e.types.rbegin(); it != e.types.rend(); ++it) {
        if (std::ranges::contains(Grammar::roles(), *it)) {
            return *it;
        }
    }
    return {};
}

bool is_thing(const Bytes& category) {
    return category == noun || category == proper_noun || category == pronoun;
}

// The words in a list, joined: "vast, deep".
std::string joined(const std::vector<Seen>& seen) {
    std::string out;
    for (const Seen& s : seen) {
        out += out.empty() ? "" : ", ";
        out += text_of(s.word);
        if (s.status == Status::Proposed) {
            out += " (proposed)";
        }
        if (s.from_cloud) {
            out += " (cloud)";
        }
    }
    return out;
}

bool has_word(const std::vector<Seen>& seen, const Bytes& word) {
    return std::ranges::any_of(seen, [&](const Seen& s) { return s.word == word; });
}

}  // namespace

std::string_view name(Standing standing) noexcept {
    switch (standing) {
    case Standing::Known:
        return "known";
    case Standing::Plausible:
        return "plausible";
    case Standing::Unusual:
        return "unusual";
    }
    return "unusual";
}

std::string Judgement::text() const {
    const std::string head = text_of(relation.head);
    const std::string dependent = text_of(relation.dependent);
    const std::string kind = text_of(relation.kind);
    if (standing == Standing::Known) {
        return std::format("\"{}\" is known as {} {} ({}{}{})", dependent, kind, head,
                           validated > 0 ? std::format("{} validated", validated) : "",
                           validated > 0 && proposed > 0 ? ", " : "",
                           proposed > 0 ? std::format("{} proposed", proposed) : "");
    }
    std::string out = std::format("\"{}\" was never {} {}", dependent, kind, head);
    out += of_head.empty() ? std::format("; of {} I know nothing as {}", head, kind)
                           : std::format("; of {} I know as {}: {}", head, kind, joined(of_head));
    out += of_dependent.empty()
               ? std::format("; \"{}\" is {} nothing I know", dependent, kind)
               : std::format("; \"{}\" is {}: {}", dependent, kind, joined(of_dependent));
    return out;
}

std::vector<std::string> Report::unusual() const {
    std::vector<std::string> out;
    for (const Judgement& j : judgements) {
        if (j.standing == Standing::Unusual) {
            out.push_back(j.text());
        }
    }
    return out;
}

Harness::Harness(const BaseRules& rules) : rules_(&rules) {}

std::vector<Relation> Harness::relations(const Description& d) const {
    const AtomOperations ops;
    const std::vector<Entity>& entities = d.entities.entities;
    const std::size_t n = entities.size();
    std::vector<Bytes> roles;
    std::vector<Bytes> folded;
    for (const Entity& e : entities) {
        roles.push_back(role_of(e));
        folded.push_back(ops.fold(e.word));
    }
    std::vector<Relation> out;
    // A question word stands for what is asked ("Who went?"): no relation of its own.
    const auto add = [&](std::string_view kind, std::size_t head, std::size_t dependent) {
        if (std::ranges::contains(rules_->question_words(), folded[head]) ||
            std::ranges::contains(rules_->question_words(), folded[dependent])) {
            return;
        }
        out.push_back(Relation{bytes_of(kind), folded[head], folded[dependent], head, dependent});
    };
    // The heads: the first thing with the subject role, the first verb (else
    // auxiliary verb) with the predicate role, the first adjective or thing
    // with the attribute role, the first thing with the object role.
    std::size_t subject_head = n;
    std::size_t predicate_verb = n;
    std::size_t predicate_auxiliary = n;
    std::size_t attribute_head = n;
    std::size_t object_head = n;
    for (std::size_t i = 0; i < n; ++i) {
        const Bytes& category = entities[i].category;
        if (subject_head == n && roles[i] == subject && is_thing(category)) {
            subject_head = i;
        }
        if (predicate_verb == n && roles[i] == predicate && category == verb) {
            predicate_verb = i;
        }
        if (predicate_auxiliary == n && roles[i] == predicate && category == auxiliary_verb) {
            predicate_auxiliary = i;
        }
        if (attribute_head == n && roles[i] == attribute && (category == adjective || is_thing(category))) {
            attribute_head = i;
        }
        if (object_head == n && roles[i] == object && is_thing(category)) {
            object_head = i;
        }
    }
    // A negated sentence says what is not: "The sky is not green" makes
    // green no attribute of the sky, so it has no relation to judge.
    for (const Bytes& word : folded) {
        if (std::ranges::contains(rules_->negation_words(), word)) {
            return out;
        }
    }
    // A question that opens with a question word asks for the attribute:
    // "What is the sky?" has none to judge.
    const bool asks = n > 0 && std::ranges::contains(rules_->question_words(), folded.front());
    if (subject_head < n && attribute_head < n && !asks) {
        add("attribute of", subject_head, attribute_head);
    }
    if (subject_head < n && predicate_verb < n) {
        add("subject of", predicate_verb, subject_head);
    }
    if (predicate_verb < n && object_head < n) {
        add("object of", predicate_verb, object_head);
    }
    // An adjective before a noun of the same role modifies it; a thing after
    // a preposition, with the complement role, completes it.
    for (std::size_t i = 0; i + 1 < n; ++i) {
        if (entities[i].category == adjective && roles[i] != attribute) {
            for (std::size_t j = i + 1; j < n; ++j) {
                if (entities[j].category == adjective && roles[j] == roles[i]) {
                    continue;
                }
                if (entities[j].category == noun && roles[j] == roles[i]) {
                    add("modifier of", j, i);
                }
                break;
            }
        }
        if (entities[i].category == preposition) {
            for (std::size_t j = i + 1; j < n; ++j) {
                if (is_thing(entities[j].category) && roles[j] == complement) {
                    add("complement of", i, j);
                    break;
                }
                if (entities[j].category == verb || entities[j].category == auxiliary_verb ||
                    entities[j].category == preposition) {
                    break;
                }
            }
        }
    }
    return out;
}

Report Harness::judge(const Description& d, Memory* memory, Database* cloud) const {
    Report report;
    if (memory == nullptr) {
        return report;
    }
    for (const Relation& relation : relations(d)) {
        Judgement j;
        j.relation = relation;
        // The conceptions with the head, then with the dependent: affirmations
        // that were not withdrawn. The cloud when the cache has nothing.
        const auto conceptions = [&](const Bytes& word, bool& from_cloud) {
            std::vector<StoredAtom> found = memory->containing(word);
            from_cloud = false;
            if (found.empty() && cloud != nullptr) {
                found = cloud->containing(word);
                from_cloud = true;
            }
            std::vector<StoredAtom> out;
            for (StoredAtom& atom : found) {
                if (atom.description.category.bytes == affirmation && atom.status != Status::Withdrawn) {
                    out.push_back(std::move(atom));
                }
            }
            return out;
        };
        bool head_from_cloud = false;
        bool dependent_from_cloud = false;
        const std::vector<StoredAtom> with_head = conceptions(relation.head, head_from_cloud);
        std::vector<StoredAtom> with_dependent =
            relation.head == relation.dependent ? std::vector<StoredAtom>{}
                                                : conceptions(relation.dependent, dependent_from_cloud);
        // A conception with both words is counted once.
        std::erase_if(with_dependent, [&](const StoredAtom& atom) {
            return std::ranges::any_of(with_head, [&](const StoredAtom& h) {
                return h.id == atom.id && h.description.metadata.bytes == atom.description.metadata.bytes;
            });
        });
        j.from_cloud = head_from_cloud || dependent_from_cloud;
        const auto note = [&](const StoredAtom& atom, bool from_cloud) {
            for (const Relation& r : relations(atom.description)) {
                if (r.kind != relation.kind) {
                    continue;
                }
                if (r.head == relation.head && r.dependent == relation.dependent) {
                    if (atom.status == Status::Validated) {
                        ++j.validated;
                    } else {
                        ++j.proposed;
                    }
                }
                if (r.head == relation.head && !has_word(j.of_head, r.dependent)) {
                    j.of_head.push_back(Seen{r.dependent, atom.status, atom.id, from_cloud});
                }
                if (r.dependent == relation.dependent && !has_word(j.of_dependent, r.head)) {
                    j.of_dependent.push_back(Seen{r.head, atom.status, atom.id, from_cloud});
                }
            }
        };
        for (const StoredAtom& atom : with_head) {
            note(atom, head_from_cloud);
        }
        for (const StoredAtom& atom : with_dependent) {
            note(atom, dependent_from_cloud);
        }
        // Validated first in the lists, then the order they were stored in.
        const auto validated_first = [](const Seen& a, const Seen& b) {
            return (a.status == Status::Validated) > (b.status == Status::Validated);
        };
        std::ranges::stable_sort(j.of_head, validated_first);
        std::ranges::stable_sort(j.of_dependent, validated_first);
        if (j.validated + j.proposed > 0) {
            j.standing = Standing::Known;
        } else if (!j.of_dependent.empty()) {
            j.standing = Standing::Plausible;
        } else {
            j.standing = Standing::Unusual;
        }
        report.judgements.push_back(std::move(j));
    }
    return report;
}

}  // namespace larry
