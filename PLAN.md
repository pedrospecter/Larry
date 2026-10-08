# Larry — plan

This file is the working plan for Larry. It is written for the cloud sessions that will build Larry one piece at a time, and for the user who directs them. A cloud session starts with nothing but this repository, so everything a session needs to know is here or in the code.

Last updated: 2026-10-08.

## How to use this file

1. Read sections 1 to 5 for context, then section 6 to find the next item.
2. Run the setup script (once F2 exists), build, and run the tests. If anything fails before you have changed anything, fix that first and say so.
3. Take the first unticked item in section 6 whose needs are met. If it needs an open question from section 10, ask the user at the start of the session. If there is no answer, take the next item that is not blocked.
4. Build the item with its tests. Each item is sized for one session. If it turns out larger, split it in this file before you start.
5. Before you finish: tick the item in section 6, add a line to the log in section 12 with the numbers you measured, add new questions to section 10, and correct anything in this file that the work showed to be wrong.

## 1. What Larry is

Larry is an AI model that holds what it knows as explicit byte entities and thinks by looking them up and comparing them. It has no weights: nothing in it is a number fitted by training. It runs on a CPU.

The user set three goals:

1. Build all of the language assimilation technique: how Larry takes in language.
2. Build all of the cognitive comparisons: how Larry relates one piece of knowledge to another.
3. Build everything else needed for intelligence: memory, reasoning, speaking, and directing itself.

## 2. Vocabulary

These names are the user's. Use them as they are and do not rename them.

| Term | Meaning | In the code |
|---|---|---|
| Atom | A sentence held as bits (its UTF-8 bytes). An atom is an assumed truth. | `Sentence` |
| Electron | One part of an atom's description. Every atom has one electron of each of the five kinds below. | `electron.hpp` |
| Category electron | What kind of sentence the atom is. Its value is a qualification: affirmation, question, order, assumption or expression. | `CategoryElectron`, `Qualification` |
| Type electron | The type of the atom. Not defined yet (Q1). | `TypeElectron` |
| Entities electron | The words of the sentence, in order. Each entity has its word, one category taken from the language's base rules, and a list of types. The types are not defined yet (Q2). | `EntitiesElectron`, `Entity` |
| Image electron | The atom's representation. Today it is just the sentence text (Q3). | `ImageElectron` |
| Metadata electron | The other electrons written out as bytes. It is the key under which the atom is stored and found. Different electrons always give different metadata, and atoms that share leading parts sort next to each other. | `MetadataElectron`, `AtomOperations::metadata` |
| Constellation | A language. | `Constellation`, `Language` |
| Base rules | The rules of a language, read from files in `base_rules/<locale>/`. Today they hold the 13 word categories. | `BaseRules` |
| Cognition | The process that compares atoms. | `Cognition` |
| Database | What the brain uses to get information: the cloud, the record of conceptions, PostgreSQL on the user's Raspberry Pi, reached through libpq. `LARRY_DB` names it. | `Database` |
| Cloud | The user's word for the database: where the conceptions live for good. The machine searches it when the cache has no answer. | `Database`, `Brain::truth`, `Brain::answers` |
| Cache | The user's word for what the machine keeps: the sentences, the words with category, type and context, and the recent conceptions. It answers first. | `Memory` |
| Status | What a conception stands at: proposed, validated (by the user or by a second source), withdrawn. | `Status` |
| Source | Where a conception came from: `lesson:<file>`, `user`, `read:<file>`, or the cloud. | `StoredAtom::sources` |
| Emotion | Part of the atom's type: neutral, joy, sadness, anger, fear, surprise, disgust, or sarcasm. The user asked for it to tell sarcasm apart. | `Assimilation::types` |
| Guess | A category an unknown word takes from the known words seen in the same context. Marked, and never evidence. | `Source::Guess` |
| Dictionary | The words of a language with their categories, always on the machine: the English one is the public-domain Moby Part-of-Speech list, 192,960 words. A fallback for a word no conception has taught, and the spell checker's list. | `Dictionary`, `dictionary/en/words.txt` |
| Validator | One of the people allowed to validate or withdraw a conception. The user's safeguard: Larry rejects a decision from anyone else, and records who decided. | `validators`, `Brain::decide` |
| Declaration | The user's word for an affirmation. Orders are also called commands. | `Qualification::Affirmation`, `Qualification::Order` |
| Exclusive attributes | Values a thing has one of at a time: colours, open and closed, hot and cold. A conception with one makes a claim with another false (K1). | `base_rules/en/exclusives.txt` |
| Grammar | The rules of where the words go in an English sentence: patterns of categories with the role each place has (K2). | `base_rules/en/grammar.txt` |
| Tolerance | How far a sentence may stray from the grammar and still be understood, for non-native English (K3). | `base_rules/en/tolerance.txt` |
| Harness | The user's word for the context check: whether an entity is used with the words it is known with, from the conceptions (K4). "The sea is wide" is unusual when the sea is known as deep and blue. | `Cognition::harness` |
| Neural network | What finds an atom by its metadata. Today: the metadata order and the word index in memory. The spreading lookup is N5 (Q4). | `Memory::find`, `find_prefix`, `uses` |
| Brain | The loop that takes input, uses memory and cognition, and replies (Q4, proposed answer in use). | `Brain` |
| Conception | An atom Larry holds: what it has been told, stored in memory with its electrons. The user's term. | `StoredAtom`, `Memory::store` |
| Memory | The cache: a local file, a log of atoms with status and sources as hex, with the word index built from it. | `Memory`, `memory/<locale>.atoms` |

This plan needs four names that the user has not chosen. They are proposals (Q12):

| Proposed term | Meaning |
|---|---|
| Bond | A typed link from one atom to another, or from one entity to another, that records how they relate: "conflicts with", "answers", "is a kind of". |
| Molecule | A group of atoms treated as one unit: a text, a conversation, a chain of reasoning. |
| Pattern | An atom in which one or more entities are left open: "the [noun] is blue". |
| Evidence count | A whole number saying how many stored atoms support something (Q7). |

## 3. Rules for every session

1. **Do not invent definitions.** Where this plan says "proposed", the user has not agreed yet. An item that depends on an open question in section 10 waits for the user's answer, unless the question is marked as not blocking.
2. **No weights.** No fitted floating-point parameters, no gradient training, no pretrained models or embeddings, no machine-learning libraries, and no call to a language model when Larry runs.
3. **Bytes, not text.** In the database, anything Larry reads is a `bytea` column, every table has an `id` key, and a language is named by its locale code (`en`). This is the user's instruction. The memory file follows it: every atom is its metadata and its bytes, written as hex.
4. **Data read on every run lives in files, not in the database.** It goes in `base_rules/<locale>/`, one item per line, each byte written as two hex digits. The database is for what Larry stores and looks up. This is the user's instruction: a database call on every run is too slow.
5. **CPU only.** C++23, the warning flags in `CMakeLists.txt`, the style in `.clang-format`. Add a dependency only when the item cannot be built without it.
6. **Every result can be traced.** Any description, comparison, answer or conclusion can list the atoms and rules it came from.
7. **Every item lands with tests.** The build and all tests pass before a commit.
8. **Claim only what was measured.** Write measured numbers in the log. A capability exists when its test passes.

## 4. Where the code stands (2026-10-07, end of the second prototype session)

`./build/larry` builds and runs on Linux (GCC 14) with libpq as its only dependency. The machine keeps the cache (`memory/<locale>.atoms`, or `LARRY_MEMORY`); the cloud is the PostgreSQL server `LARRY_DB` names, with `scripts/setup.sh` starting a local one as the stand-in for the user's Raspberry Pi. On first use Larry rebuilds its cache from the lessons and pushes them to the cloud.

| Part | State |
|---|---|
| Atom from text, atom as bits | Works (`AtomOperations`). |
| Metadata | Works, reads back into electrons, and gives prefixes for neighbours. It now holds the types, so caches and clouds filled before the types change need a rebuild. |
| Dictionary (A2b) | `dictionary/en/words.txt`, built by `scripts/dictionary.sh` from Moby, loads in 0.3 s. A word memory does not know takes its one dictionary category (`Source::Dictionary`, evidence when stored), or its several as candidates for the context to choose from (then a guess). A word nobody knows gets the dictionary words one slip away, the ones memory knows first, and Larry asks "Did you mean" instead of guessing. |
| Base rules | 19 files in `base_rules/en/`: the 13 categories, and the lists the splitter, qualification, brain and types need (punctuation, sentence ends, closers, joiners, number joiners, abbreviations, titles, question words, assumption words, expressions, negation words, contractions, endings, forms, pronouns, auxiliaries, emotions, sarcasm markers). Hex, with `#` comments and `key=value` pairs; `scripts/hex.sh` converts. |
| Sentences and entities (A1) | Works. Suites: 240 sentences, 55 texts, random bytes under the sanitizers. Known gaps: a name of several words at the start of a sentence is not joined, initials and web addresses split, an emoji is a word. |
| Qualification (A3) | Works with the proposed rules (Q5). Suite: 407 sentences. Guessed categories do not drive it. |
| Categories (A2, A6a) | From lessons and from memory. An unknown word takes the category of known words in the same context as a guess, marked and never evidence; Larry says so when it hears one and asks about a word it cannot guess (A5, first step). `larry words` lists the vocabulary with categories, types and contexts. |
| Types (A7a) | Filled, with Q1 and Q2 as proposed plus emotion: features from the word's form (number, tense, person, degree), the role by position (subject, predicate, object, attribute, complement, modifier, link), and the atom's roles in order with its emotion (sarcasm by marker phrases for now). |
| Image | Still a copy of the text (Q3). |
| Cache (F3, F5, N1, N2) | `Memory`: a log of atoms with status and sources; find by metadata, by prefix, by id; the word index with each use's category and context, the neighbours' categories too. |
| Cloud (N2) | `Database` through libpq (optional: without it Larry builds with the cache alone, `-DLARRY_CLOUD=OFF` does the same): conceptions with status and created time, sources, the word index with context. The brain answers from the cache first, then searches the cloud and caches what it finds; what Larry hears goes to both; `larry sync` pushes what the cloud lacks and pulls its most recent. Measured with the local server: an answer from the cloud 0.09 s, from the cache 0.01 s. |
| Validation (R2a, R2b) | A conception is proposed until a validator accepts it (`larry validate`, interactive or by id); a withdrawn one is no evidence. Only validators decide: `larry validators add <name>` (anyone adds the first, then only a validator), `LARRY_USER` names who is talking, and the decision records who made it, in the cache and the cloud. A second source never validates by itself: the user asked for this safeguard. |
| Lessons and rebuild (F7) | 96 sentences in lesson 1. Two rebuilds give the same cache file byte for byte. |
| Comparisons of form (C1 to C5) | Work; `larry compare`. C5 sees the types now. Not yet stored as bonds (N3). Suites: 104 pairs. |
| Truth of a concept (R1a) | `larry ask`: true, false or unknown, with the conception and whether it came from the cloud and is still proposed. |
| Hearing requests (G3a) | `larry say`, `larry chat`: statements stored with a novelty check, conflict report and validation by a second source; questions answered; orders refused; assumptions kept apart; expressions returned; "why?" explains. |
| Tests | 8 test executables, `ctest`, all passing with and without `LARRY_SANITIZE=ON`. The database and brain tests use a scratch schema of the local server and skip their cloud checks without one. |

## 5. The design

```
text in → assimilation → atoms with electrons → network (memory)
                                                    ↓
text out ← generation ← reasoning ← cognition (comparisons)
```

Larry works by repeating one step. It takes what is in hand (a new sentence, a question, a goal), finds the stored atoms nearest to it, lines them up entity by entity, carries over what matches, and records what differs.

- **Assimilation** uses the step to describe a new sentence from sentences Larry already knows.
- **Cognition** is the lining up itself: each comparison is one way of reading the result.
- **Reasoning** chains the step: the result of one comparison becomes what is in hand for the next.
- **Generation** runs the step backwards, from a meaning to a sentence.
- **The network** is what makes "find the nearest stored atoms" fast.

Nothing in the step is a trained number. This table shows what does the work that weights do in other models:

| A weight-based model | Larry |
|---|---|
| What goes with what is spread across the weights. | It is explicit: the order of the metadata keys, the word index, and the bonds. |
| How strongly: the size of a weight. | How specific the match is (how many parts two atoms share) and, if the user allows it, evidence counts (Q7). |
| Learning is gradient descent over many passes. | Learning is storing an atom, forming a pattern from atoms that differ in one place, and recording a bond. One example is enough to learn from. |
| It generalizes by interpolating between examples. | It generalizes through patterns with open entities and by analogy to the nearest stored atom. |
| It corrects a mistake by retraining. | It withdraws or revises the atom. Bonds lead to everything that was concluded from it. |
| It cannot say where an answer came from. | Every answer lists its atoms (rule 6). |

## 6. Build order

Tick an item when its "done when" holds. Items are described in section 7. A question number after an item means the item waits for that answer.

**Stage 1 — Foundation**
- [x] F1 Session instructions
- [x] F2 Environment script
- [x] F3 Connection from the environment (as the memory file, `LARRY_MEMORY`; the database is parked)
- [x] F4 Tests
- [x] F5 Read path and duplicates (in `Memory`)
- [x] F6 Command line

**Stage 2 — First sentences**
- [x] A1 Sentences and entities
- [x] N1 Word index (in `Memory`)
- [x] A2 Taught categories
- [x] F7 Lessons and rebuild (Q9, proposed answer used)
- [x] C1–C5 Comparisons of form
- [x] A3 Qualification (Q5, proposed answer used)

Milestone 1: after one lesson, `larry show "the grass is green"` fills in every category without being told, and `larry compare "the sky is blue" "the sea is blue"` reports the same structure with one difference, sky and sea. **Reached on 2026-10-07.**

**Stage 2b — The first prototype** (the user asked for these early; each is a first step of a later item)
- [x] R1a Truth of a concept: `larry ask` (first step of R1, see section 7)
- [x] G3a Hearing requests: `larry say`, `larry chat` (first step of G3, with first steps of C16, R2, A5 and G4)

**Stage 2c — The second prototype session** (the user's direction: the language on the machine, the conceptions in the cloud)
- [x] N2a The database is the cloud, through libpq (N2 the other way round: see section 7)
- [x] N2b The cache answers first, the cloud second; `larry sync`
- [x] R2a Proposed, validated, withdrawn; `larry validate` (first step of R2)
- [x] A7a Types with emotion (first step of A7, Q1 and Q2 as proposed, emotion added by the user)
- [x] A6a Vocabulary: guesses from context, `larry words` (first step of A6)
- [x] A2b The dictionary on the machine: fallback categories and spelling (new item, see section 7)
- [x] R2b Validation by the user only: validators, `LARRY_USER`, who decided (the user's safeguard)

**Stage 2d — Cognition, the user's five asks (2026-10-08)**
- [ ] K1 True, false or I don't know: a claim is false when a conception gives the thing another exclusive attribute ("the sky is green" is false because the sky is blue). First step of C9.
- [ ] K2 The grammar of English as data: patterns of categories with roles, why the words sit where they sit; `larry grammar`. First step of A8.
- [ ] K3 Tolerance for non-native English: the nearest pattern within a threshold, the sentence read as meant, the deviation named.
- [ ] K4 The context harness: is each entity used with the words it is known with, from the conceptions; what is known of it instead. First step of A10 and C16.
- [ ] K5 Qualification as a cognitive process: by the rules and by the nearest validated conceptions, with the reason; `larry qualify`.

Milestone 2d: `larry ask "the sky is green"` answers false and says why; `larry grammar "sky is blue"` names the missing determiner and reads the sentence as "the sky is blue"; `larry harness "the sea is wide"` says the sea is known as blue and deep; `larry qualify` gives its reason.

**Stage 3 — Memory**
- [ ] F8 Benchmarks
- [ ] N2 Local copy of the atoms (Q11): the cache and the cloud exist (N2a, N2b); the targets below are not measured yet
- [ ] N3 Bonds
- [ ] N4 Molecules
- [ ] N5 Spreading lookup (Q4)
- [ ] N6 Working memory

**Stage 4 — Learning words**
- [ ] A4 Word forms
- [ ] A5 Guided assimilation
- [ ] A6 Free assimilation (Q7)
- [ ] A7 Entity types and atom type (Q1, Q2)
- [ ] A8 Groups and roles (Q2)

Milestone 2: Larry describes sentences it has never seen, and its accuracy on a public test set is on record.

**Stage 5 — Meaning**
- [ ] A9 Image (Q3)
- [ ] A10 Word meanings and relations
- [ ] A11 Reference across sentences
- [ ] C6, C7, C12, C17 Comparisons of meaning
- [ ] C16 Novelty

**Stage 6 — Truth and answers**
- [ ] C8, C9, C14, C15 Comparisons of truth
- [ ] C10, C11 Comparisons of use
- [ ] R1 Answering from memory (Q13)
- [ ] R2 Truth keeping (Q14)
- [ ] R3 State and time

Milestone 3: Larry answers questions about what it was told, says "I don't know" when it has nothing, and reports a conflict when it is told two things that cannot both be true.

**Stage 7 — Reasoning**
- [ ] R4 Chaining
- [ ] R5 Rules from examples (Q7)
- [ ] R6 Best explanation
- [ ] R7 Numbers, sets and space
- [ ] R8 Analogy as a step, with C13
- [ ] R9 Rules as atoms

Milestone 4: the bAbI scores for tasks 1 to 18 are on record. Tasks 19 and 20 come with planning (S2).

**Stage 8 — Speaking**
- [ ] G1 From image to sentence
- [ ] G2 Answers as sentences
- [ ] G3 Conversation (Q13)
- [ ] G4 Explaining
- [ ] G5 Asking
- [ ] G6 Longer output

Milestone 5: `larry chat` holds a conversation and answers "why?" after any reply.

**Stage 9 — Self-direction and breadth**
- [ ] S1 Goals from orders
- [ ] S2 Planning
- [ ] S3 Attention
- [ ] S4 Knowing what it knows
- [ ] S5 Reading on its own
- [ ] S6 New categories
- [ ] S7 Idle thinking
- [ ] A12 Second constellation (Q15)
- [ ] G7 Translation

## 7. The items

### Track F — Foundation

**F1 · Session instructions.** Add `CLAUDE.md` at the repository root: a few lines telling a session to read `PLAN.md` first and follow section 3. Cloud sessions load `CLAUDE.md` automatically and do not load this file unless told.
Done when: a new session given only "continue the plan" picks the right next item.

**F2 · Environment script.** `scripts/setup.sh` prepares a clean Linux machine: a compiler with C++23 `<print>` and `std::ranges::contains` (GCC 14 or newer, or Clang 18 or newer) and CMake 3.28 or newer. With `--postgres` it also installs libpq, libpqxx 8.0.2 (built from source from the Debian source tarball when the distribution's package is older) and a local PostgreSQL server with a database `larry` and `sql/schema.sql` applied, for the parked database class. CMake picks GCC 14 on Linux when no compiler was chosen, and fails early with a clear message when `<print>` is missing.
Done when: on a fresh cloud session the script, the configure step, the build and `./build/larry` all succeed with no manual step, and the macOS build still succeeds. Done on Linux; the macOS build is not verified by a session.

**F3 · Connection from the environment.** Memory is the file `LARRY_MEMORY` names, or `memory/<locale>.atoms` in the repository. The parked database takes its connection string from `LARRY_DB` (default `dbname=larry`). No address is written in the source.
Done when: the same binary uses another memory file with only the variable changed. Done.

**F4 · Tests.** A `ctest` target with one test file per class, covering what exists: metadata (different electrons give different bytes, including words that contain a zero byte; atoms that share leading parts sort together), the hex file reader (odd length, non-hex characters, empty lines), and `categorize` (wrong count, unknown category). The tests also run with `LARRY_SANITIZE=ON`.
Done when: `ctest` passes in both builds on Linux and macOS.

**F5 · Read path and duplicates.** `Memory::find(metadata)` and `Memory::find_prefix(prefix)`. Decided: the same sentence stored again changes nothing (`Stored::Same`); a different sentence with the same metadata, the same words in another form of capitals, spacing or punctuation (Q6), keeps the first atom (`Stored::SameForm`).
Done when: a store-then-find round trip and both duplicate cases are tested. Done, in `Memory`; the parked `Database` has the same operations.

**F6 · Command line.** Subcommands that later items extend: `larry show <text>` describes a sentence without storing it, `larry tell <text> [category ...]` describes and stores it (taught when the categories follow), `larry read <file>` tells every sentence in a file. Also there now: `teach`, `rebuild`, `compare`, `ask`, `say`, `chat`, `count`. Later: `bench`, `sync`. `larry help` lists them.
Done when: `larry show "the sky is blue"` prints what the demo prints today. Done.

**F7 · Lessons and rebuild.** A cloud session starts with an empty memory, so what Larry has been taught must live in the repository. Lessons are files in `lessons/<locale>/`: plain UTF-8 (Q9), a sentence line, then the category of each entity separated by commas, blank lines between lessons, `#` comments. `larry rebuild` empties memory and assimilates every lesson file in name order, and Larry does this by itself when the memory file does not exist yet. A test checks every lesson in the repository: one category per entity, all in the base rules, no word taught with two categories. This also makes a design change cheap to try: change a rule, rebuild, and compare the results.
Done when: two rebuilds give the same `atoms` contents, byte for byte. Done: the memory files are identical.

**F8 · Benchmarks.** `larry bench` measures sentences assimilated per second, lookups per second, memory per atom, and time to answer, with ten thousand and with one million atoms. Generated atoms are fine for this. A session that changes storage or lookup writes the new numbers in the log.
Done when: the benchmark finishes in a few minutes and prints one line per measure.

### Track A — Language assimilation (goal 1)

Assimilation turns text into atoms with every electron filled in, and uses each stored atom to describe the next sentence better. It has three levels. Larry starts at level 0 and should end at level 2.

| Level | Name | Larry is given | Larry does |
|---|---|---|---|
| 0 | Taught | The sentence and its description. | Stores it. |
| 1 | Guided | The sentence. | Describes what it can from memory and asks about the rest. |
| 2 | Free | The sentence. | Describes all of it and marks what it guessed. |

**A1 · Sentences and entities.** Split text into sentences and each sentence into entities: words, numerals, contractions ("don't"), hyphenated words, abbreviations, and names of more than one word. The splitting rules are data in `base_rules/en/`. It must handle any UTF-8 input.
Done when: a suite of at least 200 sentences with their expected entities passes, and random bytes as input never crash the sanitizer build. Q6 is open but does not block: use the proposed answer. Done (240 sentences, 55 texts). Known gaps, kept in the suites as they are: a name of several words joins only after the first word of a sentence and only when each word is capitalized in ASCII ("New York is big" stays two entities; joining at the start needs memory, A6/A8); initials ("J. K. Rowling") and web addresses split at the full stops; a single newline is white space and a blank line ends a sentence.

**A2 · Taught categories (level 0).** A lesson gives a sentence and the category of each word. `larry tell` stores the atom with those categories. Larry's knowledge of a word is then the set of stored atoms that contain it, so no separate dictionary is needed.
Done when: after a lesson, `larry show` on a new sentence made only of taught words with one category each fills in every category. Needs A1, N1. Done. Each entity is marked taught, memory, open (with its candidates) or unknown.

**A2b · The dictionary.** The words of the language with their categories, always on the machine, as the user asked: the public-domain Moby Part-of-Speech list, converted by `scripts/dictionary.sh` into `dictionary/en/words.txt` (one word, a tab, Larry's categories; a capitalized noun in Moby is a proper noun; the auxiliary verbs come from the base rules; phrases are left out). Memory comes first; the dictionary speaks only for a word no conception has taught: one category is the word's and counts as evidence once stored, several are candidates for the context to choose from (A6a). For a word in neither, the dictionary gives the words one typing slip away (swap, drop, change, add), the ones memory knows first, and Larry asks "Did you mean" rather than guess. Q8 allows a public resource made by people as a source of knowledge; this is one.
Done when: the dictionary loads in under a second and the tests for categories and near words pass. Done: 192,960 words, 3.6 MB, 0.3 s. Not yet: numerals as words ("three" is a noun in Moby), particles, a second language.

**A3 · Qualification.** Fill in `Cognition::qualify` from rules in `base_rules/en/`. Proposed starting rules: a sentence that ends with "?" or opens with a question word or an auxiliary verb before its subject is a question; one that opens with a verb in its base form and has no subject is an order; one that opens with or hangs on a word such as "if", "suppose", "maybe" or "perhaps" is an assumption; one made only of interjections or a greeting is an expression; anything else is an affirmation.
Done when: a suite of at least 300 sentences labelled by hand passes. Needs A2, Q5. Done with the proposed rules (407 sentences) and two refinements the suite asked for: an auxiliary verb followed by a verb opens an order ("Do not stop"), and a question word used as a noun does not make a question. Known gaps: "I wonder if it rains" is an assumption by the rule; "What a day!" is a question by the rule.

**A4 · Word forms.** Relate the forms of one word: sky and skies, is and was, blue and bluer. Larry finds regular endings by lining up taught words that share a beginning and differ at the end. Irregular forms are taught as pairs. Each relation is stored as a "form of" bond between entities.
Done when: for a held-out list of regular forms Larry finds the base form, and an unknown word with a known ending gets a proposed category. Needs N3.

**A5 · Guided assimilation (level 1).** When Larry meets a word or a structure it cannot describe from memory, it asks one specific question ("what category is 'azure' in 'the sky is azure'?") and stores the answer as taught.
Done when: reading a lesson in which one word in ten is unknown produces exactly one question per unknown word, and reading it again after the answers produces none.

**A6 · Free assimilation (level 2).** Larry proposes the category of an unknown word itself. From context: it finds stored atoms with the same categories around the same place, and the unknown word takes the category that fills that place in them. From form: A4. A word with several categories ("run") takes the one from the most specific matching context. A guess is stored as a guess, never as taught.
Done when: accuracy on the Universal Dependencies English test set is recorded as a curve against the number of taught sentences. Set a target with the user after the first measurement. Needs A4, N5, Q7.
First step done (A6a): the context is the words before and after, with their categories, kept in the word index. An unknown word takes the single winning category among what followed the word before it and what preceded the word after it; it is stored as a guess (type `guessed`, source `Guess`) and never counts as evidence, so memory does not learn from its own guesses. Not yet: form (A4), the most specific context, the measurement.
Notes: Larry's 13 categories line up almost one for one with the 17 Universal Dependencies word tags. Preposition is ADP, conjunction covers CCONJ and SCONJ, numeral is NUM, auxiliary verb is AUX, proper noun is PROPN, and PUNCT, SYM and X have no Larry category. Memory-based taggers, which work the way this item describes, have reported around 96% on English newspaper text (Daelemans and others, 1996).

**A7 · Entity types and atom type.** Fill the two empty electrons. Agreed (Q1, Q2): an entity's types are its grammatical features (number, tense, person, degree) and its role in the sentence (subject, object, and so on). The atom's type is its structure, the ordered roles of its entities, and its emotion (the user's addition, to tell sarcasm apart).
Done when: a suite labelled by hand passes. Needs Q1, Q2.
First step done (A7a): features from the word's form and category (regular endings, exceptions and irregular forms, pronouns, auxiliary verbs, all base rules); the role by position around the first verb (subject before it, predicate, then object, attribute after a copula, complement after a preposition, modifier for an adverb, link for a conjunction, none for an interjection); the emotion from marker phrases (sarcasm) or the first emotion word, else neutral. Tested by cases, not yet by a labelled suite. Known gaps: roles in questions and in sentences with two verbs are rough; sarcasm needs meaning and context (A9, A11).

**A8 · Groups and roles.** Work out which entities belong together ("the blue sky") and which entity depends on which (the subject of the verb). Larry learns this as patterns from taught atoms (C4) and applies the most specific pattern that matches.
Done when: the share of words attached to the right word on the Universal Dependencies English test set is recorded. Set a target after the first measurement. Needs A7, C4.

**A9 · Image.** Make the image the form in which two sentences that say the same thing are equal. Proposed: words in their base form, roles in a fixed order, references replaced by what they refer to, and negation and time kept as marks.
Done when: in a suite of sentence pairs, every pair that means the same has equal images and every pair that does not has different images. Needs A4, A8, Q3.

**A10 · Word meanings and relations.** Separate the meanings of one word (the bank of a river, the bank that holds money) and learn relations between words: is a kind of, is part of, is the opposite of, means the same as. Larry learns them from defining sentences, which are ordinary atoms ("a sparrow is a bird"), and stores them as bonds between entities.
Done when: after reading a set of defining sentences, the bonds from sparrow reach animal in two steps. Needs N3, A9.

**A11 · Reference across sentences.** Link "it", "she" or "the animal" to the earlier entity it refers to within a molecule.
Done when: a suite passes, and bAbI tasks 11 and 13 are measured. Needs N4, A8.

**A12 · Second constellation.** Add a second language using only new files in `base_rules/<locale>/` and `lessons/<locale>/`. This shows that the technique does not depend on English.
Done when: the suites for A1 to A6 pass for the new locale and the change to `src/` contains nothing specific to either language. Needs Q15.

### Track N — The network (goal 3)

Proposed definition (Q4): the neural network is the stored atoms, the connections between them (the order of the metadata keys, the word index, and the bonds), and the lookup that walks from the atoms in hand to their neighbours.

**N1 · Word index.** For each word, the atoms that contain it, the position, the category it has there, and its context: the words before and after it. It answers "which atoms mention sky?" and "which categories has 'run' been seen with?". The metadata key alone cannot answer these, because it only finds atoms by their leading parts. The index is built from the atoms when the memory file is read, so it is always the same for the same atoms.
Done when: both questions are answered correctly for a set of stored atoms, and building the index twice from the same atoms gives the same result. Done, in `Memory`.

**N2 · Local copy of the atoms.** Describing or answering one sentence will take thousands of lookups. A network round trip to the database for each would take seconds per sentence. The user's direction: the machine keeps the language (sentences, words with category, type and context) and a cache of recent conceptions; the cloud keeps the conceptions for good; a question is answered from the cache at once, and the cloud is searched when the cache has nothing. Done as N2a and N2b: `Memory` is the cache (a log file, read whole at start-up), `Database` the cloud, the brain looks in the cache first and caches what the cloud gives, `larry sync` pushes and pulls. Still to do: the memory-mapped file and binary search, and the targets below.
Done when (proposed targets): with one million atoms, a prefix lookup takes under 10 microseconds and start-up takes under one second. Needs F8, Q11.

**N3 · Bonds.** Store typed links between atoms and between entities, with lookup in both directions. Each bond records its kind, its two ends, and where it came from (taught, or the comparison that produced it).
Done when: bonds survive a rebuild and both directions are tested.

**N4 · Molecules.** Group atoms into a text or a conversation, keeping their order, who said each one, and when. This gives Larry a memory of events ("what did I tell you yesterday?") and a source for every atom.
Done when: `larry read` stores a file as one molecule, and the atoms come back in order with their source.

**N5 · Spreading lookup.** From a set of atoms or entities, collect their neighbours through shared key parts, the word index and bonds, nearest first, up to a limit of steps and results. Every later item uses this. Nearest means most shared parts, then fewest bonds away, then highest evidence count if Q7 allows.
Done when: the result is the same on every run, and with one million atoms three steps take under 10 milliseconds (a proposed target). Needs N1, N2, N3, Q4.

**N6 · Working memory.** The small, bounded set of atoms now in play: the conversation so far, the question, and results on the way to an answer. Lookup starts from here.
Done when: A11 and R1 read from it instead of taking atoms as arguments.

Two later options if the benchmarks call for them: a fixed-width bit signature for each atom, so that candidates can be filtered with AND and a bit count before the full comparison; and lookup across several CPU cores.

### Track C — Cognitive comparisons (goal 2)

Cognition is the process that compares atoms. Every comparison takes two atoms (C13 takes four, C16 takes one atom and the whole memory), gives its result as bytes, can say which entity matched which, and can be stored as a bond. Each has a suite of pairs in `tests/comparisons/`.

| # | Comparison | The question it answers | Example |
|---|---|---|---|
| C1 | Identity | Are the bits the same? | "the sky is blue" twice |
| C2 | Same form | Are they the same apart from capitals, spacing and punctuation? | "The sky is blue." and "the sky is blue" |
| C3 | Alignment | Which entity in one corresponds to which in the other? Every other comparison starts here. | sky lines up with sea in "the sky is blue" and "the sea is blue" |
| C4 | Difference | Where exactly do they differ? Two atoms that differ in one place give a pattern, and this is how Larry generalizes. | sky and sea give "the [noun] is blue" |
| C5 | Same structure | Do they have the same categories and types in the same order, with different words? | "the sky is blue" and "the grass is green" |
| C6 | Same meaning | Are the images equal? | "the sky is blue" and "blue is the colour of the sky" |
| C7 | More general | Does one say about a wider group what the other says about a narrower one? | "birds fly" and "sparrows fly" |
| C8 | Follows from | If the first is true, must the second be? | "Tom has a red car" and "Tom has a car" |
| C9 | Conflict | Can they not both be true? | "the door is open" and "the door is closed" |
| C10 | Answers | Does the affirmation fill the gap in the question? | "what colour is the sky?" and "the sky is blue" |
| C11 | Satisfies | Does the state fulfil the order? | "close the door" and "the door is closed" |
| C12 | About the same thing | Do they concern the same things? | "the sky is blue" and "clouds cross the sky" |
| C13 | Analogy | Is the relation in the first pair the same as in the second pair? | bird and nest, bee and hive |
| C14 | Quantity and order | Which is more, or which comes first? | "Tom has three apples" and "Ana has five apples" |
| C15 | Cause and condition | Does one give the reason or the condition for the other? | "if it rains the street is wet" and "it rains" |
| C16 | Novelty | Against all of memory: is this atom already known, a narrower case of something known, new, or in conflict? Every assimilated atom passes through this. | |
| C17 | Same referent | Do two entities name the same thing? | "the sky" and "it" |

Build them in four groups:

- **Form, C1 to C5.** Needs A1 and A2 only.
- **Meaning, C6, C7, C12, C17, then C16.** Needs A9, A10, A11 and N5.
- **Truth, C8, C9, C14, C15.** Needs the meaning group. Also measured on FraCaS, a public set of 346 inference problems built by hand, each answered yes, no or unknown.
- **Use, C10, C11, C13.** Needs the truth group. C13 is built with R8.

Done when, for each comparison: its suite passes, it reports which entities matched, and its result can be stored and read back as a bond. C1 to C5 done except the bond, which waits for N3; their results are bytes (`Comparison::bytes`) ready to be stored.

### Track K — Cognition, as the user framed it (2026-10-08)

The user's five asks after the first prototype, each a first step of a later item. They keep every rule of section 3: no weights, data in files, every answer traceable.

**K1 · True, false or I don't know.** `larry ask` already says true or false when a conception has the same core with the same or the opposite polarity, and I don't know otherwise. Now a claim is also false when a conception gives the same thing another value of an exclusive attribute: "the sky is green" is false because "the sky is blue" is held and blue and green are both colours, of which a thing has one at a time. The exclusive groups are data in `base_rules/en/exclusives.txt` (colours, open and closed, hot and cold, and so on; two different numerals in the same place are exclusive too). The negation of a false claim is true. The answer names the conception and the rule. Withdrawn conceptions count for nothing; proposed ones are marked.
Done when: a suite of claims against a fixed memory passes (true, false, I don't know), and `larry ask "the sky is green"` prints false, the conception and the rule.

**K2 · The grammar of English.** Why the words sit where they sit: patterns of categories with the role of each place, as data in `base_rules/en/grammar.txt`, one pattern per line, with optional and repeated parts ("determiner? adjective* noun auxiliary_verb adjective"). A matcher says which pattern a sentence fits and gives the roles, which replace the position heuristic of A7a when a pattern fits; when none fits it says where the sentence breaks and what was expected there. Validated conceptions add their category sequences as patterns of their own (grammar from examples, the start of A8). `larry grammar <text>` shows it.
Done when: every lesson sentence fits a pattern, a suite of well-formed and ill-formed sentences passes, and the roles agree with A7a's on the lessons.

**K3 · Tolerance for non-native English.** When no pattern fits, the nearest pattern is found by lining up the categories (C3), and the sentence is accepted when the deviation is within the threshold in `base_rules/en/tolerance.txt` (proposed: one deviation per four words, at most two). The deviation is named (a missing determiner, an extra word, "are" where "is" fits), the sentence is read as meant for truth and answers, and stored as said with the reading noted. Spelling slips already go through the dictionary (A2b).
Done when: a suite of non-native sentences is read as meant within the threshold, and sentences beyond it are refused with the reason.

**K4 · The context harness.** For each entity of a sentence, whether it is used with the words it is known with: known (this neighbour was seen, in a conception), plausible (a neighbour of the same category was seen), unusual (never seen). For an unusual attribute of a thing, what is known of the thing instead ("of the sea I know: blue, deep") and what the attribute is known of ("wide is said of: road, river"). Validated conceptions are the standard; proposed ones count less. `say`, `ask` and `show` report what is unusual; `larry harness <text>` shows it all. This is the user's point about "the sea is wide": Larry cannot know that "vast" fits better until a conception says so, but it can say that "wide" was never said of the sea.
Done when: a suite against a fixed memory passes, and `larry harness "the sea is wide"` reports the known attributes of the sea.

**K5 · Qualification as a cognitive process.** A3 qualifies by rules. Now the nearest validated conceptions with the same structure (C5) give their qualification too, and the answer names its reason: the rule that fired, or the conceptions it resembles. When the rules and the examples disagree, Larry says both. The user's words, declaration for affirmation and command for order, are accepted in the output. `larry qualify <text>` shows it.
Done when: the A3 suite still passes, a suite of sentences qualified by example passes, and every answer names its reason.

### Track R — Reasoning (goal 3)

**R1 · Answering from memory.** Take a question atom, find the affirmations that answer it (C10) and reply with the best one. When nothing answers, the reply is "I don't know". Larry never produces an answer it cannot trace to atoms.
Done when: a suite of questions about a lesson passes, including questions with no answer, and bAbI tasks 4 and 5 are measured. Needs C10, N5, Q13.
First step done (R1a, `Brain::truth`, `Brain::answers`, `larry ask`): a sentence is reduced to its core (words in lower case, contractions expanded, negation words and do-support removed, a polarity); a concept is true when an affirmation has the same core with the same polarity, false with the opposite polarity, unknown otherwise, and only affirmations count. A yes/no question is read as the statements it asks about (the auxiliary verb moved after each possible subject; a negative question asks about the positive). A question that opens with a question word is answered by the conceptions whose core has its known words around the gap ("What is the sky?" asks for "the sky is [?]", also answered by "[?] is the sky"). Unknown answers list the nearest conceptions by shared content words. Not yet: word forms (A4), meaning (A9), the C10 comparison itself.

**R2 · Truth keeping.** Atoms are assumed truths, so some will turn out false. Each atom gets a status (assumed, concluded, in conflict, withdrawn) and its support: its source, or the atoms it was concluded from. When C9 finds a conflict, Larry records it and does not choose silently. When an atom is withdrawn, so is everything concluded only from it.
Done when: a suite passes, and bAbI tasks 9 and 10 are measured. Needs C9, N3, Q14.
First step done (R2a, R2b): the status is proposed, validated or withdrawn, with the sources that gave the conception and who decided. Only a validator validates or withdraws, with `larry validate`; `larry validators add` names them, anyone the first, then only a validator; `LARRY_USER` says who is talking. A second source never validates by itself, as the user asked: it only adds to the conception's sources. A withdrawn conception is no evidence. A conflict (R1a's negation) is reported and both atoms are kept. Not yet: concluded atoms and their support, withdrawal of what was concluded.

**R3 · State and time.** The world changes. "Mary went to the kitchen. Mary went to the garden." is a change, and the two sentences do not conflict. A later atom about the same thing replaces the earlier one as the present state, and both stay in memory in order.
Done when: bAbI tasks 1, 6, 12 and 14 are measured. Needs N4, R2.

**R4 · Chaining.** Combine several atoms through bonds and conditions to reach a new atom, forwards when an atom is stored and backwards from a question. Every concluded atom records the atoms it came from. Depth and time are bounded.
Done when: a suite passes, and bAbI tasks 2, 3 and 15 are measured. Needs C8, C15, R3.

**R5 · Rules from examples.** From several atoms that fit one pattern, propose a general atom and store it as an assumption: sparrows fly, robins fly, both are birds, so "birds fly". A counter-example ("penguins do not fly") narrows or withdraws it.
Done when: a suite with held-out cases passes, and bAbI task 16 is measured. Needs C4, C7, R2, Q7.

**R6 · Best explanation.** Given an observation and stored conditions, propose the assumption that would explain it: "the street is wet" and "if it rains the street is wet" give the assumption "it rained".
Done when: a suite passes, and each proposal is stored as an assumption, never as a truth.

**R7 · Numbers, sets and space.** Count, add, compare, list, and relate positions and sizes.
Done when: bAbI tasks 7, 8, 17 and 18 are measured. Needs C14.

**R8 · Analogy as a step.** When no rule applies, find the nearest stored atom with the same structure (C5, C13), carry over what it concluded, and mark the result as a guess. This is what keeps Larry useful on input it has no rule for.
Done when: on a suite of cases that no stored rule covers, the measured share of correct guesses is recorded.

**R9 · Rules as atoms.** Reasoning rules are stored as atoms (assumptions and conditions), so Larry can be taught how to reason in plain sentences and can inspect and revise its own rules.
Done when: a rule taught in one sentence changes Larry's answers with no change to the code.

### Track G — Generation and dialogue (goal 3)

**G1 · From image to sentence.** The reverse of assimilation: from an image and a qualification, produce a sentence in a constellation, using patterns from stored atoms.
Done when: for the A9 suite, assimilating a sentence, generating from its image and assimilating the result gives the same image. Needs A9, C4.

**G2 · Answers as sentences.** Turn the atom R1 found into a reply that fits the question: "blue", "the sky is blue", or "yes".

**G3 · Conversation.** `larry chat` keeps a molecule for the conversation and treats each qualification in its own way. An affirmation goes through novelty (C16) and is stored, or Larry objects if it conflicts. A question is answered. An order is carried out or refused. An assumption is reasoned about without being stored as a truth. An expression is answered in kind.
Done when: scripted conversations pass. Needs N6, R1, R2, Q13.
First step done (G3a, `Brain::hear`, `larry say`, `larry chat`): an affirmation is stored after a first novelty check (the same conception: "I already know that"; a conflicting one is kept and reported, as R2 asks; a new one is stored and Larry asks about the first word it does not know, as A5 asks); a question is answered through R1a; an order is refused ("I cannot do that yet"); an assumption is stored as one; an expression is answered in kind. `chat` keeps no molecule yet (N4) and answers "why?" with what the last reply came from (first step of G4).

**G4 · Explaining.** After any reply, "why?" lists the atoms and rules used, as sentences.

**G5 · Asking.** Larry asks when it finds a gap or a conflict (A5, R2).

**G6 · Longer output.** Summarize a molecule, describe a thing from everything known about it, and give steps in order.

**G7 · Translation.** Generate in one constellation from an image assimilated in another. Needs A12.

### Track S — Self-direction (goal 3)

This track is research. The methods are proposals, and several will need to change once they are tried.

**S1 · Goals from orders.** An order becomes a goal: the state that would satisfy it (C11).

**S2 · Planning.** Actions are atoms that say what must hold before and what holds after. Larry searches, within a bound, for a sequence of actions that reaches the goal.
Done when: bAbI tasks 19 and 20 are measured and a suite of small planning problems passes.

**S3 · Attention.** A queue of what to think about, with a CPU budget for each turn. Open questions, conflicts, atoms waiting for novelty and unfinished goals compete for it.

**S4 · Knowing what it knows.** For any subject Larry reports what it knows, what was taught and what it guessed, where it holds conflicts, and what it cannot answer.

**S5 · Reading on its own.** Given a text, Larry reads at level 2, keeps what passes novelty, and lists its questions. Teaching sentence by sentence cannot reach the amount of knowledge Larry needs, so the project depends on this item more than any other.
Done when: after reading a set of texts unaided, Larry answers questions about them, measured on MCTest (short children's stories with multiple-choice questions).

**S6 · New categories.** Larry notices words that behave alike and fit no category well, and proposes a new category to the user.

**S7 · Idle thinking.** With no input, Larry spends CPU time running novelty, chaining and rules from examples over its memory. It finds conflicts, draws conclusions and proposes general atoms, and reports them.

## 8. How we know it is working

| Rung | Larry can | Measured by | Stage |
|---|---|---|---|
| 1 | Describe a sentence it was taught | Its own suites | 2 |
| 2 | Say how two sentences relate in form | Suites for C1 to C5 | 2 |
| 3 | Describe sentences it has never seen | Universal Dependencies English: category accuracy against the number of taught sentences | 4 |
| 4 | Say whether two sentences mean the same, follow, or conflict | Suites, FraCaS | 5, 6 |
| 5 | Answer questions about what it was told, and say when it does not know | Suites, bAbI tasks 1, 4, 5, 6, 9, 10 | 6 |
| 6 | Combine facts, count, and follow time and place | The remaining bAbI tasks up to 18 | 7 |
| 7 | Form a rule from examples and drop it on a counter-example | bAbI task 16, held-out suites | 7 |
| 8 | Hold a conversation and explain every reply | Scripted conversations, with the trace of every reply checked | 8 |
| 9 | Read new text unaided and answer questions about it | MCTest | 9 |
| 10 | Do all of this in a second language with no change to the code | The second locale's suites | 9 |
| 11 | Stretch: resolve references that need knowledge of the world | Winograd Schema Challenge, 273 problems | after 9 |

Three cautions about this ladder:

- **The rungs are evidence that Larry is getting more capable. They do not prove intelligence.** No known method, with or without weights, is certain to produce general intelligence. This plan cannot promise goal 3. It can make sure the project always knows which rung it stands on.
- **Rungs 1 to 6 use methods that have worked before in systems without weights. Rungs 7 to 11 are research.** Expect some of the proposed methods to fail there. The measures are in the plan to show a failure early.
- **bAbI is a small artificial language.** Passing it shows that the mechanisms work. Only rung 9 shows whether they hold up on real text.

Check the licence of any public data before adding it to the repository. Prefer a script that downloads it over committing it.

## 9. Known hard problems

Earlier systems built without weights ran into the same few walls. The plan meets each of them at a known place.

| Problem | Why it is hard | What the plan does | What will show failure |
|---|---|---|---|
| Ambiguity | Most common words have several categories and meanings, and most sentences have several possible structures. | Keep every reading, choose by the most specific stored context, and ask when two readings tie (A5, A6, A10). | Accuracy at rung 3 stays low as lessons are added. |
| Teaching does not scale | A person cannot type in everything an intelligence needs to know. | Level 2 assimilation and reading on its own (A6, S5). | The curve at rung 3 goes flat, or rung 9 fails. If so, stop and redesign. Do not add rules by hand to cover the gap. |
| Brittleness | Rules fail on wording nobody planned for. | The image makes different wordings equal (A9), and analogy to the nearest atom covers what the rules do not (R8). | Paraphrase suites fail. Rung 4 scores sit far below rung 2. |
| Search grows too fast | Chaining over many atoms multiplies the possibilities. | Lookup through indexes only, a bound on steps, the attention budget (N5, S3). | Benchmark times grow faster than the number of atoms. |
| Judging degree without weights | "Usually" and "probably" need some measure of how much. | Specificity of the match, and evidence counts if the user allows them (Q7). | Larry cannot choose between two readings that both match. |
| Meaning from text alone | Words defined only by other words may never connect to the world. | Start with relations between words (A10) and numbers, time and space (R7). Input other than text waits for the user's definition of the image (Q3). | Rung 11 fails while the earlier rungs pass. |

## 10. Questions for the user

"Blocks" lists the items that wait for the answer. A question that blocks nothing can be changed later at the cost of a rebuild (F7).

| # | Question | Proposed answer | Blocks |
|---|---|---|---|
| Q1 | What is the type of an atom (the type electron)? | Answered: its structure, the ordered roles of its entities, and its emotion. | A7 (done as A7a) |
| Q2 | What goes in an entity's list of types? | Answered: its grammatical features (number, tense, person, degree) and its role in the sentence. | A7 (done as A7a), A8 |
| Q3 | What is the image? | The form in which two sentences that say the same thing are equal (see A9). | A9 and every item that depends on meaning |
| Q4 | What are the neural network and the brain, as parts of the program? | The network is the stored atoms, their connections and the lookup that walks them. The brain is the loop that takes input, uses the network and cognition, and replies. The proposed answer is in use: `Brain` exists. | N5 |
| Q5 | What are the rules that tell the five qualifications apart? | The starting rules in A3, in use with two refinements (see A3). | A3 (done with the proposal) |
| Q6 | Is a punctuation mark an entity? The 13 categories have none for it. | No. It stays in the atom's bits, and qualification reads it. | Nothing |
| Q7 | May Larry use evidence counts? A count is a whole number, worked out from the stored atoms and always possible to work out again, so it is a tally and not a trained weight. It is still a number that sways choices. | Yes. | A6, R5 |
| Q8 | Where do lessons come from? They can be written by the user, taken from public resources made by people (Universal Dependencies, WordNet, Simple English Wikipedia), or written by Claude sessions. With the third, Larry's knowledge would come indirectly from a weight-based model. | The first two for lessons. Sessions write test suites only. | A2 beyond the first lessons |
| Q9 | Are lesson files plain UTF-8 text, or hex like the base rules? | Plain UTF-8 for sentences Larry reads, since they are input like typed text. Hex for Larry's own rule files. | F7 |
| Q10 | Which database does a cloud session use? The user's is on a private network. | A local PostgreSQL in each session, filled by `larry rebuild`. The user's database is filled the same way. | Nothing |
| Q11 | May Larry keep a local copy of the atoms in a file mapped into memory, with the database as the place of record? | Yes (see N2). | N2 |
| Q12 | Are the names bond, molecule, pattern and evidence count acceptable? | Yes. | Nothing |
| Q13 | The `atoms` table is described as assumed truths. Are questions and orders stored there too? | Every sentence becomes an atom and is kept in its molecule. Only affirmations are treated as truths. | R1, G3 |
| Q14 | When two atoms conflict, which one stands? | From the same source, the later one stands and the earlier is withdrawn. From different sources, both are kept and marked, and Larry asks. | R2 |
| Q15 | Which language is the second constellation? | None proposed. | A12 |
| Q16 | Where do conceptions live for good? | Answered: in the cloud, the Raspberry Pi's PostgreSQL. The machine keeps the language and a cache of recent conceptions, answers from the cache at once and searches the cloud for deeper thinking. | N2 (done as N2a, N2b) |
| Q17 | When is a conception true? | Answered: it is proposed until the user validates it or a second different source gives it; a conflict is reported and both are kept. | R2 (done as R2a) |
| Q18 | Is the copula ("is" in "the sky is blue") an auxiliary verb or a verb? The first lesson teaches it as an auxiliary verb, which makes "Is the sky blue" a question by the A3 rule. | Auxiliary verb. | A2 lessons, A3 |
| Q19 | May a lesson sentence have a word Larry cannot categorize? Today every lesson gives every category (level 0). | No: lessons are level 0. Level 1 and 2 come with A5 and A6. | F7 |
| Q20 | Which emotions does the atom's type hold? Today: neutral, joy, sadness, anger, fear, surprise, disgust, sarcasm, from word lists and marker phrases. | This list, until meaning (A9) gives a better way to find them. | A7 |
| Q21 | When Larry guesses a word's category, should it also ask the user, or only say what it took the word as? Today it says so and goes on. | Say so; the user corrects by teaching the sentence with its categories. | A5, A6 |
| Q22 | How many recent conceptions does the cache keep, and when does it forget? Today it keeps everything it has met; `larry sync` pulls the 100 most recent. | Keep everything until the benchmarks (F8) say otherwise. | N2 |
| Q23 | Should a dictionary category count as evidence when the sentence is stored (it does now), or only a validator's word? | Evidence: the list was made by people, like a lesson (Q8). | A2b |
| Q24 | Should the sessions that build Larry ever be validators? Today they are not, unless the user adds them. | No. | R2b |
| Q25 | Which attributes are exclusive? `exclusives.txt` proposes colours, open and closed, hot and cold, wet and dry, alive and dead, on and off, full and empty, sizes, days, months, seasons, and different numerals in the same place. | That list, grown as cases come up. | K1 |
| Q26 | How much deviation does tolerance allow? | One deviation per four words, at most two; a spelling slip counts as one. | K3 |
| Q27 | Does the harness judge by validated conceptions only, or by proposed ones too? | Validated ones are the standard; proposed ones are mentioned as such. | K4 |

## 11. Earlier work worth reading

- Memory-based language processing (Daelemans and van den Bosch; the TiMBL and MBT systems): describing new sentences from stored examples. For A6.
- ADIOS (Solan and others, 2005): finding patterns in raw text without a teacher. For C4, R5 and S5.
- Structure mapping (Gentner; the Structure-Mapping Engine): analogy as lining up relations. For C13 and R8.
- Natural logic (MacCartney and Manning): deciding "follows from" and "conflicts" directly on sentences. For C7 to C9.
- Truth maintenance systems (Doyle; de Kleer): withdrawing a belief and everything concluded from it. For R2.
- NARS (Pei Wang): a reasoning system without weights that runs on evidence counts. For Q7 and track R.
- Cyc: forty years of teaching facts by hand. Read it for the limits of level 0.
- Sparse distributed memory (Kanerva) and hyperdimensional computing: similarity from long bit vectors with no training, fast on a CPU. A fallback if lining up by structure proves too brittle.
- Soar and ACT-R: whole architectures with working memory, goals and attention. For track S.

## 12. Log

| Date | Items | Measured | Notes |
|---|---|---|---|
| 2026-10-07 | Plan written | `./build/larry` builds and runs on macOS. No tests exist. | Q1 to Q15 are open. |
| 2026-10-07 | F1 to F7, A1, A2, A3, N1, C1 to C5, R1a, G3a | Linux, GCC 14: build and 7 test executables pass, also with `LARRY_SANITIZE=ON`. Suites: 240 sentences with entities, 55 texts with sentences, 407 qualified sentences, 103 comparison pairs. Lesson 1: 96 sentences, 379 word uses, memory file 20,344 bytes (212 bytes per atom). `larry rebuild` 0.017 s; `larry ask "Is the sky blue?"` 0.005 s (wall time, one run each, 4 CPUs). Milestone 1 reached. | The user parked the PostgreSQL database and asked for a prototype: memory is a local file. Q4, Q5, Q9 used their proposed answers. Q16 to Q19 added. macOS build not verified this session. |
| 2026-10-07 | R2b, A2b | Linux, GCC 14 and clang 18 with libc++: 9 test executables pass, also with the sanitizers and without libpq. Dictionary: 192,960 words, 3,612,564 bytes, loads in 278 ms (GCC) and 1,182 ms under the sanitizers; `scripts/dictionary.sh` builds it in 2.6 s. | The user: conceptions on Azure next, the dictionary always on the machine, validation by the user alone. The two-source rule is gone. Q23, Q24 added. |
| 2026-10-07 | N2a, N2b, R2a, A7a, A6a | Linux, GCC 14, libpq: build and 8 test executables pass, also with `LARRY_SANITIZE=ON`. With the local server as the cloud: an answer from the cloud into an empty cache 0.088 s, from the cache 0.013 s, rebuild of 96 lessons into cache and cloud 0.174 s (wall time, one run each). Cache file 51,950 bytes for 96 atoms with types (541 bytes per atom). Vocabulary from lesson 1: 159 words. 19 rule files. | The user's direction: language on the machine, conceptions in the cloud, cache first. Q1, Q2, Q16, Q17 answered; emotion added to the type; Q20 to Q22 added. libpqxx replaced by libpq. The Pi itself was not reached from the cloud session. |
