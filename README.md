# Larry

Larry is an AI model with no weights. It holds what it knows as explicit byte
entities (atoms, with their electrons) and thinks by looking them up and
comparing them. It runs on a CPU. `PLAN.md` is the working plan and the place
to start reading.

## Build and run

    scripts/setup.sh                       # Linux: GCC 14 and CMake 3.28
    cmake -S . -B build && cmake --build build
    ctest --test-dir build
    ./build/larry help

On first use Larry rebuilds its memory from `lessons/en/`. Try:

    ./build/larry show "the grass is green"
    ./build/larry compare "the sky is blue" "the sea is blue"
    ./build/larry ask "Is the sky blue?"
    ./build/larry chat
