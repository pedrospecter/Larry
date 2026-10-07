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
    # Azure Database for PostgreSQL (SSL is required there)
    export LARRY_DB="host=<server>.postgres.database.azure.com port=5432 dbname=larry user=<user> password=<password> sslmode=require"

The tables are in `sql/schema.sql`; Larry applies it on first contact, and
`psql -d larry -f sql/schema.sql` does the same by hand. On first use Larry
rebuilds its cache from `lessons/en/` and pushes the lessons to the cloud.

## Who decides what is true

Everything Larry hears is a proposed conception. Only a validator makes it
true or withdraws it, and nobody is a validator until you add yourself:

    export LARRY_USER=pedro                 # who is talking (default: your login name)
    ./build/larry validators add pedro      # anyone adds the first; after that, only a validator
    ./build/larry validate                  # y validates, n withdraws, s skips, q stops

Larry refuses a decision from anyone else, and records who decided.

## Try

    ./build/larry show "the grass is green"
    ./build/larry compare "the sky is blue" "the sea is blue"
    ./build/larry ask "Is the sky blue?"
    ./build/larry say "The skyy is blue."      # Did you mean "sky"?
    ./build/larry words sky
