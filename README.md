# Larry

Larry is an AI model with no weights. It holds what it knows as explicit byte
entities (atoms, with their electrons) and thinks by looking them up and
comparing them. It runs on a CPU. `PLAN.md` is the working plan and the place
to start reading.

## Build and run

    scripts/setup.sh                       # Linux (apt) or macOS (Homebrew): compiler, CMake,
                                           # libpq, a local PostgreSQL, the dictionary
    cmake -S . -B build && cmake --build build
    ctest --test-dir build
    ./build/larry chat

Without libpq the build still works, with the cache alone.

## The machine and the cloud

The machine keeps the language: the sentences it has met, the words with
their categories, types and contexts, the English dictionary
(`dictionary/en/words.txt`, 192,960 words, always there), and a cache of
recent conceptions in `memory/en.atoms`. The cloud, where the conceptions
live for good, is the PostgreSQL server `LARRY_DB` names. A question is
answered from the cache at once; what the cache cannot answer, Larry
searches in the cloud and remembers.

    # A Raspberry Pi on the network
    export LARRY_DB="host=192.168.10.133 dbname=larry user=postgres"
    # Azure Database for PostgreSQL, in the form the portal shows (Larry adds sslmode=require)
    export LARRY_DB="Host=<server>.postgres.database.azure.com;Port=5432;Username=<user>;Password=<password>;Database={0}"

Instead of exporting, put the line in a `.env` file at the repository root
(see `.env.example`; git ignores `.env`): Larry reads it on every run. The
tables are in `sql/schema.sql`; Larry creates the database when the server
lacks it and applies the schema on first contact. On first use Larry rebuilds
its cache from `lessons/en/` and pushes the lessons to the cloud.

## Who decides what is true

Everything Larry hears is a proposed conception. Only a validator makes it
true or withdraws it, and nobody is a validator until you add yourself:

    export LARRY_USER=pedro                 # who is talking (default: your login name)
    ./build/larry validators add pedro      # anyone adds the first; after that, only a validator
    ./build/larry validate                  # y validates, n withdraws, s skips, q stops

Larry refuses a decision from anyone else, and records who decided.

## Cognition

Larry answers true, false or I don't know (`larry ask`): a claim is false
when a conception says the opposite, or gives the thing another exclusive
attribute ("the sky is green" against "the sky is blue"), and the answer
names the conception and the rule. The grammar of English is data
(`base_rules/en/grammar.txt`): patterns of categories with the role of each
place, which say why the words sit where they sit (`larry grammar`). A
sentence off the grammar is read as meant within a tolerance
(`base_rules/en/tolerance.txt`), each deviation named: "Sky are blue" is
read as "The sky is blue", stored as said. The context harness says whether
each word is used with the words it is known with (`larry harness`): "wide"
was never said of the sea, the sea is known as vast. And every sentence is
qualified, an affirmation (a declaration), a question, an order (a command),
an assumption or an expression, by the rules and by the conceptions of the
same structure, with the reasons (`larry qualify`).

## The web as a source

Larry can search Wikipedia, keep an article or any page as plain text on
the machine (`content/en/`), ask Wiktionary what a word is, and study
content: every sentence gets a class (a fact, context, a question, an
instruction, speech, a heading, a reference, a fragment), the facts
become proposed conceptions, the words to learn are gathered, and a lesson
draft is written in `lessons/en/drafts/` for you to correct and teach.
Nothing from the web is true until you validate it. The transport is the
`curl` command on the machine.

    ./build/larry search sky
    ./build/larry fetch Sky                  # content/en/sky.txt
    ./build/larry define vast                # adjective, noun, from Wiktionary
    ./build/larry classify content/en/sky.txt
    ./build/larry study Sky                  # or a file, or a url
    ./build/larry teach lessons/en/drafts/sky.txt   # after you corrected it

In `larry chat` and `larry say`, an order Larry knows how to do is done:
"search for the sea", "define vast", "tell me about the sea", "compare the
sky and the sea", "count the conceptions", "remember that the moon is
round", "forget that ..." (a validator only). The list is in
`base_rules/en/commands.txt`.

## What a sentence is

    ./build/larry recognize "Oh great, another meeting."   # statement, sarcasm, by the marker "oh great"
    ./build/larry recognize "Could you search for the sea?" # request: the command search "the sea"
    ./build/larry ask "What is two plus three?"            # 5, because: arithmetic: 2 + 3 = 5

## Ask Larry something

    ./build/larry chat                       # a line at a time: questions, statements, "why?", "bye"
    ./build/larry ask "Is the sky blue?"     # one question or claim: the answer, and why; nothing stored
    ./build/larry ask "How many minutes are there in 3 hours?"   # 180 minutes
    ./build/larry ask "Solve 2x + 3 = 11"    # x = 4, because: algebra: 2x + 3 = 11 gives x = 4
    ./build/larry ask "What day of the week was January 1, 2000?" # Saturday
    ./build/larry say "The sea is wide."     # one sentence: Larry replies and stores what it heard

## Try

    ./build/larry show "the grass is green"
    ./build/larry compare "the sky is blue" "the sea is blue"
    ./build/larry ask "Is the sky blue?"       # Yes.
    ./build/larry ask "What is the sky?"       # The sky is blue.
    ./build/larry ask "the sky is green"       # false, because the sky is blue
    ./build/larry say "The skyy is blue."      # Did you mean "sky"?
    ./build/larry say "Sky are blue."          # I read it as "The sky is blue."
    ./build/larry grammar "Sky the is blue."   # breaks at word 2; expected ...
    ./build/larry harness "The sea is wide."   # "wide" was never said of the sea
    ./build/larry qualify "Close the window."  # order (command), by rule 6 and by example
    ./build/larry words sky
    ./build/larry say "The sky is not blue."  # a conflict: both stay, bonded "conflicts with"
    ./build/larry bonds sky                   # the bonds at a word, or at a sentence in quotes
    ./build/larry molecules                   # each text read and each conversation, in order (N4)
    ./build/larry near sky                    # the neighbours in memory, nearest first (N5)
    ./build/larry bench 10000                 # F8: sentences per second, lookups, answers, start-up
