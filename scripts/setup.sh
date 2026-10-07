#!/usr/bin/env bash
# Prepares a clean Linux machine to build and run Larry (PLAN.md, item F2).
#
# It installs a compiler with C++23 <print> (GCC 14 or newer), CMake 3.28 or
# newer, libpq (the client library Larry reaches its database through), and
# a local PostgreSQL server with a database "larry": the stand-in for the
# user's Raspberry Pi, for the tests and for a cloud session. Run it again at
# any time: every step checks what is already there.
#
#   scripts/setup.sh
#   cmake -S . -B build && cmake --build build && ctest --test-dir build
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
say() { printf 'setup: %s\n' "$*"; }
die() { printf 'setup: %s\n' "$*" >&2; exit 1; }

case "$(uname -s)" in
Linux) ;;
Darwin)
    # macOS: Homebrew installs the tools; Apple clang has C++23 <print>.
    command -v brew >/dev/null || die "Homebrew is needed: https://brew.sh"
    say "installing with Homebrew: cmake, libpq, postgresql@16"
    brew install cmake libpq postgresql@16 >/dev/null
    say "cmake: $(cmake --version | head -1 | awk '{print $3}')"
    say "libpq: $(brew --prefix libpq)"
    if ! pg_isready -q 2>/dev/null; then
        say "starting PostgreSQL"
        brew services start postgresql@16 >/dev/null || true
        for _ in $(seq 1 30); do
            pg_isready -q 2>/dev/null && break
            sleep 1
        done
    fi
    if pg_isready -q 2>/dev/null; then
        psql="$(brew --prefix postgresql@16)/bin/psql"
        createdb="$(brew --prefix postgresql@16)/bin/createdb"
        if ! "$psql" -d postgres -tAc "select 1 from pg_database where datname = 'larry'" | grep -q 1; then
            say "creating database larry"
            "$createdb" larry
        fi
        "$psql" -q -d larry -v ON_ERROR_STOP=1 -f "$ROOT/sql/schema.sql"
        say "database: larry (set LARRY_DB=\"dbname=larry\" to use it; for the Pi or Azure, its host and user)"
    else
        say "PostgreSQL did not start; set LARRY_DB to reach a server, or run without a cloud"
    fi
    if [ ! -f "$ROOT/dictionary/en/words.txt" ]; then
        say "building the dictionary"
        "$ROOT/scripts/dictionary.sh"
    fi
    say "ready. Next:"
    say "  cmake -S $ROOT -B $ROOT/build && cmake --build $ROOT/build"
    say "  $ROOT/build/larry chat"
    exit 0
    ;;
*) die "unsupported system: $(uname -s)" ;;
esac

SUDO=""
if [ "$(id -u)" -ne 0 ]; then
    SUDO="sudo"
fi
as_postgres() {
    if [ "$(id -u)" -eq 0 ]; then
        runuser -u postgres -- "$@"
    else
        sudo -u postgres "$@"
    fi
}

# 1. Packages ---------------------------------------------------------------
command -v apt-get >/dev/null || die "this script needs apt-get (Debian or Ubuntu)"
export DEBIAN_FRONTEND=noninteractive
say "installing packages"
$SUDO apt-get update -qq
packages="ca-certificates curl cmake ninja-build make libpq-dev postgresql postgresql-client"
if apt-cache show g++-14 >/dev/null 2>&1; then
    packages="$packages gcc-14 g++-14"
else
    packages="$packages build-essential"
fi
# shellcheck disable=SC2086
$SUDO apt-get install -y -qq --no-install-recommends $packages

# 2. Compiler ---------------------------------------------------------------
CXX_BIN=""
for candidate in g++-15 g++-14 g++; do
    if command -v "$candidate" >/dev/null; then
        major="$("$candidate" -dumpfullversion -dumpversion 2>/dev/null | cut -d. -f1)"
        if [ "${major:-0}" -ge 14 ]; then
            CXX_BIN="$candidate"
            break
        fi
    fi
done
[ -n "$CXX_BIN" ] || die "no GCC 14 or newer found; Larry needs C++23 <print>"
say "compiler: $CXX_BIN ($("$CXX_BIN" --version | head -1))"
CC_BIN="${CXX_BIN/g++/gcc}"

# 3. CMake ------------------------------------------------------------------
cmake_version="$(cmake --version | head -1 | awk '{print $3}')"
cmake_major="$(echo "$cmake_version" | cut -d. -f1)"
cmake_minor="$(echo "$cmake_version" | cut -d. -f2)"
if [ "$cmake_major" -lt 3 ] || { [ "$cmake_major" -eq 3 ] && [ "$cmake_minor" -lt 28 ]; }; then
    die "CMake $cmake_version is too old; Larry needs 3.28 or newer (https://apt.kitware.com)"
fi
say "cmake: $cmake_version"

# 4. PostgreSQL -------------------------------------------------------------
# The local server stands in for the user's Raspberry Pi. When it cannot
# start, Larry still runs: memory is on the machine, and LARRY_DB can point
# at another server.
if ! pg_isready -q 2>/dev/null; then
    say "starting PostgreSQL"
    if ! $SUDO service postgresql start >/dev/null 2>&1; then
        version="$(pg_lsclusters -h 2>/dev/null | awk 'NR==1 {print $1}')"
        [ -n "$version" ] && $SUDO pg_ctlcluster "$version" main start || true
    fi
    for _ in $(seq 1 30); do
        pg_isready -q 2>/dev/null && break
        sleep 1
    done
fi
if pg_isready -q 2>/dev/null; then
    me="$(id -un)"
    if ! as_postgres psql -tAc "select 1 from pg_roles where rolname = '$me'" | grep -q 1; then
        say "creating database role $me"
        as_postgres createuser -s "$me"
    fi
    if ! psql -d postgres -tAc "select 1 from pg_database where datname = 'larry'" | grep -q 1; then
        say "creating database larry"
        createdb larry
    fi
    psql -q -d larry -v ON_ERROR_STOP=1 -f "$ROOT/sql/schema.sql"
    say "database: larry (set LARRY_DB=\"dbname=larry\" to use it; for the Pi, its host and user)"
else
    say "PostgreSQL did not start; set LARRY_DB to reach a server, or run without a cloud"
fi

# 5. Dictionary -------------------------------------------------------------
if [ ! -f "$ROOT/dictionary/en/words.txt" ]; then
    say "building the dictionary"
    "$ROOT/scripts/dictionary.sh"
fi

# 6. Done -------------------------------------------------------------------
say "ready. Next:"
say "  cmake -S $ROOT -B $ROOT/build"
say "  cmake --build $ROOT/build"
say "  ctest --test-dir $ROOT/build"
say "  $ROOT/build/larry show \"the sky is blue\""
