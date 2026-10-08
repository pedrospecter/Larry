#!/usr/bin/env bash
# The gate before a commit (PLAN.md, section 3, rule on tests): builds the four
# configurations and runs every test in each, one configuration at a time
# (the database tests share the local server's scratch schemas), and exits
# with a failure when anything fails. Nothing is committed when it fails.
#
#   scripts/check.sh            # all four configurations
#   scripts/check.sh build      # one of them
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
export LARRY_DB="${LARRY_DB:-dbname=larry}"
failed=0
configure() {
    case "$1" in
    build) cmake -S . -B build > /dev/null ;;
    build-asan) cmake -S . -B build-asan -DLARRY_SANITIZE=ON > /dev/null ;;
    build-libcxx) CXX=clang++-18 cmake -S . -B build-libcxx > /dev/null ;;
    build-nocloud) cmake -S . -B build-nocloud -DLARRY_CLOUD=OFF > /dev/null ;;
    *) echo "check: unknown configuration $1" >&2; return 1 ;;
    esac
}
dirs="${1:-build build-asan build-libcxx build-nocloud}"
for d in $dirs; do
    if [ "$d" = build-libcxx ] && ! command -v clang++-18 > /dev/null; then
        echo "check: $d skipped (no clang++-18)"
        continue
    fi
    if ! configure "$d"; then failed=1; continue; fi
    if ! cmake --build "$d" 2>&1 | grep -E "error|warning: " ; then :; else failed=1; fi
    if ! cmake --build "$d" > /dev/null 2>&1; then echo "check: $d does not build"; failed=1; continue; fi
    result="$(ctest --test-dir "$d" 2>&1 | grep -E 'tests passed|Failed|Not Run' | head -3)"
    echo "check: $d: $result"
    echo "$result" | grep -q "100% tests passed" || failed=1
done
if [ "$failed" -ne 0 ]; then
    echo "check: FAILED; do not commit"
    exit 1
fi
echo "check: all green"
