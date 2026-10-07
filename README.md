# Larry

Larry is an AI model with no weights. It holds what it knows as explicit byte
entities (atoms, with their electrons) and thinks by looking them up and
comparing them. It runs on a CPU. `PLAN.md` is the working plan and the place
to start reading.

## Build and run

    scripts/setup.sh                       # Linux: GCC 14, CMake 3.28, libpq, a local PostgreSQL
    cmake -S . -B build && cmake --build build
    ctest --test-dir build
    ./build/larry help

The machine keeps the language and a cache of recent conceptions in
`memory/en.atoms`. The cloud, where the conceptions live for good, is the
PostgreSQL server `LARRY_DB` names, for example on a Raspberry Pi:

    export LARRY_DB="host=192.168.10.133 dbname=larry user=postgres"

On first use Larry rebuilds its cache from `lessons/en/` and pushes the
lessons to the cloud. Try:

    ./build/larry show "the grass is green"
    ./build/larry compare "the sky is blue" "the sea is blue"
    ./build/larry ask "Is the sky blue?"
    ./build/larry chat
    ./build/larry validate list
    ./build/larry words sky
