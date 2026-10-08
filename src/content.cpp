#include "larry/content.hpp"

#include "larry/atom_operations.hpp"
#include "larry/brain.hpp"
#include "larry/cognition.hpp"
#include "larry/tolerance.hpp"

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

std::vector<Bytes> split(const Bytes& list, char separator) {
    std::vector<Bytes> out;
    Bytes current;
    for (const std::uint8_t b : list) {
        if (b == static_cast<std::uint8_t>(separator)) {
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

std::string lower(std::string_view text) {
    std::string out{text};
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return out;
}

const Bytes verb = bytes_of("verb");
const Bytes auxiliary_verb = bytes_of("auxiliary verb");
const Bytes pronoun = bytes_of("pronoun");

}  // namespace

std::string_view name(ContentClass c) noexcept {
    switch (c) {
    case ContentClass::Fact:
        return "fact";
    case ContentClass::Context:
        return "context";
    case ContentClass::Question:
        return "question";
    case ContentClass::Instruction:
        return "instruction";
    case ContentClass::Speech:
        return "speech";
    case ContentClass::Heading:
        return "heading";
    case ContentClass::Reference:
        return "reference";
    case ContentClass::Fragment:
        return "fragment";
    }
    return "fragment";
}

void ContentTally::add(ContentClass c) noexcept {
    switch (c) {
    case ContentClass::Fact: ++facts; break;
    case ContentClass::Context: ++context; break;
    case ContentClass::Question: ++questions; break;
    case ContentClass::Instruction: ++instructions; break;
    case ContentClass::Speech: ++speech; break;
    case ContentClass::Heading: ++headings; break;
    case ContentClass::Reference: ++references; break;
    case ContentClass::Fragment: ++fragments; break;
    }
}

Content::Content(const BaseRules& rules) : rules_(&rules) {
    for (const auto& [key, value] : rules.content()) {
        if (key == bytes_of("reference")) {
            reference_ = split(value, ';');
        } else if (key == bytes_of("speech")) {
            speech_ = split(value, ';');
        } else if (key == bytes_of("context")) {
            context_ = split(value, ';');
        } else if (key == bytes_of("heading")) {
            heading_ = split(value, ';');
        } else if (key == bytes_of("least words")) {
            least_ = static_cast<std::size_t>(std::max(1, std::atoi(text_of(value).c_str())));
        } else if (key == bytes_of("most words")) {
            most_ = static_cast<std::size_t>(std::max(1, std::atoi(text_of(value).c_str())));
        }
    }
}

std::vector<Piece> Content::classify(std::string_view text, const Brain& brain) const {
    std::vector<Piece> out;
    for (const Sentence& sentence : brain.assimilation().sentences(text)) {
        out.push_back(classify(sentence, brain));
    }
    return out;
}

Piece Content::classify(const Sentence& sentence, const Brain& brain) const {
    const AtomOperations ops;
    Piece piece;
    piece.sentence = sentence;
    piece.description = brain.assimilation().describe(sentence, &brain.memory());
    const Description& d = piece.description;
    const std::string text{ops.text(sentence)};
    const std::string folded_text = lower(text);
    const std::vector<Entity>& entities = d.entities.entities;
    const std::size_t n = entities.size();
    std::vector<Bytes> folded;
    for (const Entity& e : entities) {
        folded.push_back(ops.fold(e.word));
    }
    const auto decide = [&](ContentClass what, std::string reason) {
        piece.what = what;
        piece.reason = std::move(reason);
        return piece;
    };

    // 1. A reference: a citation mark, a web address, a reading list.
    for (const Bytes& marker : reference_) {
        const std::string m = text_of(marker);
        if (folded_text.find(m) != std::string::npos) {
            return decide(ContentClass::Reference, std::format("contains \"{}\"", m));
        }
    }
    for (std::size_t i = 0; i + 2 < text.size(); ++i) {
        if (text[i] == '[' && text[i + 1] >= '0' && text[i + 1] <= '9') {
            return decide(ContentClass::Reference, "has a citation mark like [1]");
        }
    }
    // 2. Mostly symbols or digits: a table, a formula, a list of numbers.
    std::size_t letters = 0;
    std::size_t others = 0;
    for (const char ch : text) {
        const auto c = static_cast<unsigned char>(ch);
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c >= 0x80) {
            ++letters;
        } else if (c != ' ') {
            ++others;
        }
    }
    if (letters < others) {
        return decide(ContentClass::Fragment, "mostly symbols or digits");
    }
    // 3. A heading: a line of "=", a heading word, or no end mark on a short line.
    const bool ends_with_mark = !text.empty() && std::ranges::contains(rules_->sentence_ends(),
                                                                        Bytes{static_cast<std::uint8_t>(text.back())});
    const bool closes = !text.empty() && std::ranges::contains(rules_->closers(), Bytes{static_cast<std::uint8_t>(text.back())});
    if (text.starts_with("=")) {
        return decide(ContentClass::Heading, "a line of = around a title");
    }
    if (n == 1 && std::ranges::contains(heading_, folded.front())) {
        return decide(ContentClass::Heading, std::format("\"{}\" by itself", text_of(folded.front())));
    }
    if (!ends_with_mark && !closes) {
        if (n <= 8) {
            return decide(ContentClass::Heading, "no end mark on a short line");
        }
        return decide(ContentClass::Fragment, "no end mark on a long line");
    }
    if (n == 0) {
        return decide(ContentClass::Fragment, "no word");
    }
    // 4. What kind of sentence it is, by the rules (A3, K5).
    const Qualified q = brain.cognition().qualification(d.atom, d.entities, *rules_);
    if (q.qualification == Qualification::Question) {
        return decide(ContentClass::Question, q.rule);
    }
    if (q.qualification == Qualification::Order) {
        return decide(ContentClass::Instruction, q.rule);
    }
    if (q.qualification == Qualification::Expression) {
        return decide(ContentClass::Speech, "an expression");
    }
    // 5. Speech: a quotation, an opinion, the first or second person.
    if (text.find('"') != std::string::npos || text.find("\xE2\x80\x9C") != std::string::npos) {
        return decide(ContentClass::Speech, "a quotation");
    }
    for (const Bytes& marker : speech_) {
        const std::string m = text_of(marker);
        if (folded_text.find(m) != std::string::npos) {
            return decide(ContentClass::Speech, std::format("says \"{}\"", m));
        }
    }
    for (std::size_t i = 0; i < n; ++i) {
        for (const auto& [word, features] : rules_->pronouns()) {
            if (word != folded[i]) {
                continue;
            }
            const std::string f = text_of(features);
            if (f.find("first person") != std::string::npos || f.find("second person") != std::string::npos) {
                return decide(ContentClass::Speech, std::format("\"{}\" is the {}", text_of(entities[i].word),
                                                                f.find("first") != std::string::npos ? "first person"
                                                                                                     : "second person"));
            }
        }
    }
    // 6. An assumption, or a sentence that hangs on what came before.
    if (q.qualification == Qualification::Assumption) {
        return decide(ContentClass::Context, q.rule);
    }
    if (std::ranges::contains(context_, folded.front())) {
        return decide(ContentClass::Context, std::format("opens with \"{}\"", text_of(entities.front().word)));
    }
    // 7. The length of a fact.
    if (n < least_) {
        return decide(ContentClass::Fragment, std::format("{} word{}: too short", n, n == 1 ? "" : "s"));
    }
    if (n > most_) {
        return decide(ContentClass::Fragment, std::format("{} words: too long", n));
    }
    // 8. A verb, when every word is placed; the grammar, within the tolerance.
    std::size_t unknown = 0;
    bool has_verb = false;
    for (std::size_t i = 0; i < n; ++i) {
        const Entity& e = entities[i];
        const bool to_learn = e.category.empty() ||
                              (i < d.notes.size() && (d.notes[i].source == Source::Unknown ||
                                                      d.notes[i].source == Source::Guess));
        if (to_learn) {
            ++unknown;
        }
        if (!e.category.empty() && (e.category == verb || e.category == auxiliary_verb)) {
            has_verb = true;
        }
    }
    if (unknown == 0 && !has_verb) {
        return decide(ContentClass::Fragment, "no verb");
    }
    if (unknown * 2 > n) {
        return decide(ContentClass::Fragment, std::format("{} of {} words unknown", unknown, n));
    }
    const Reading reading = brain.read(d);
    if (!reading.accepted) {
        return decide(ContentClass::Fragment, reading.reason);
    }
    std::string reason = reading.pattern.empty() ? "an affirmation" : "an affirmation the grammar reads as \"" + reading.pattern + "\"";
    if (reading.changed) {
        reason += std::format(", read as \"{}\"", ops.text(reading.meant.atom));
    }
    if (unknown > 0) {
        reason += std::format(", with {} word{} to learn", unknown, unknown == 1 ? "" : "s");
    }
    return decide(ContentClass::Fact, reason);
}

}  // namespace larry
