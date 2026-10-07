#!/usr/bin/env bash
# Prepares a clean Linux machine to build and run Larry (PLAN.md, item F2).
#
# It installs a compiler with C++23 <print> (GCC 14 or newer) and CMake 3.28
# or newer. Larry's memory is a local file, so nothing else is needed. With
# --postgres it also installs libpq, libpqxx 8.0.x (built from source when
# the distribution's package is older) and a local PostgreSQL server with a
# database "larry", for the parked database class (-DLARRY_POSTGRES=ON).
# Run it again at any time: every step checks what is already there.
#
#   scripts/setup.sh [--postgres]
#   cmake -S . -B build && cmake --build build && ctest --test-dir build
set -euo pipefail

PQXX_VERSION="8.0.2"
PQXX_SHA256="8b3ab47ea5c5df63717c73238613599edab138dcb2e56d036df0e4f4f581e653"
PQXX_URL="https://deb.debian.org/debian/pool/main/libp/libpqxx/libpqxx_${PQXX_VERSION}.orig.tar.gz"
PQXX_GIT="https://github.com/jtv/libpqxx"

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
with_postgres=false
if [ "${1-}" = "--postgres" ]; then
    with_postgres=true
fi

say() { printf 'setup: %s\n' "$*"; }
die() { printf 'setup: %s\n' "$*" >&2; exit 1; }

case "$(uname -s)" in
Linux) ;;
Darwin)
    say "this script prepares Linux. On macOS use Homebrew:"
    say "  brew install cmake"
    say "  with --postgres: brew install libpq libpqxx postgresql@16"
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
packages="ca-certificates curl cmake ninja-build make"
if apt-cache show g++-14 >/dev/null 2>&1; then
    packages="$packages gcc-14 g++-14"
else
    packages="$packages build-essential"
fi
if $with_postgres; then
    packages="$packages libpq-dev postgresql postgresql-client"
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

if $with_postgres; then
    # 4. libpqxx ------------------------------------------------------------
    pqxx_installed_version() {
        local header
        for header in /usr/local/include/pqxx/version.hxx /usr/include/pqxx/version.hxx; do
            if [ -f "$header" ]; then
                sed -n 's/^#define PQXX_VERSION "\([^"]*\)".*/\1/p' "$header" | head -1
                return
            fi
        done
    }
    have="$(pqxx_installed_version || true)"
    if [ -n "$have" ] && [ "$(echo "$have" | cut -d. -f1)" -ge 8 ]; then
        say "libpqxx: $have is installed"
    else
        say "libpqxx: ${have:-none} installed, building $PQXX_VERSION from source"
        work="$(mktemp -d)"
        trap 'rm -rf "$work"' EXIT
        src=""
        if curl -fsSL -o "$work/libpqxx.tar.gz" "$PQXX_URL"; then
            echo "$PQXX_SHA256  $work/libpqxx.tar.gz" | sha256sum -c --quiet -
            mkdir "$work/src"
            tar -xzf "$work/libpqxx.tar.gz" -C "$work/src"
            src="$(find "$work/src" -mindepth 1 -maxdepth 1 -type d | head -1)"
        else
            say "download from deb.debian.org failed, cloning the git tag"
            git clone --quiet --depth 1 --branch "$PQXX_VERSION" "$PQXX_GIT" "$work/src"
            src="$work/src"
        fi
        [ -f "$src/CMakeLists.txt" ] || die "libpqxx source not found in $src"
        cmake -S "$src" -B "$work/build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_C_COMPILER="$CC_BIN" -DCMAKE_CXX_COMPILER="$CXX_BIN" \
            -DBUILD_SHARED_LIBS=OFF -DSKIP_BUILD_TEST=ON -DBUILD_DOC=OFF \
            -DCMAKE_INSTALL_PREFIX=/usr/local >"$work/configure.log" 2>&1 ||
            { cat "$work/configure.log"; die "libpqxx configure failed"; }
        cmake --build "$work/build" -j "$(nproc)" >"$work/build.log" 2>&1 ||
            { tail -50 "$work/build.log"; die "libpqxx build failed"; }
        $SUDO cmake --install "$work/build" >/dev/null
        say "libpqxx: installed $(pqxx_installed_version) in /usr/local"
    fi

    # 5. PostgreSQL ---------------------------------------------------------
    if ! pg_isready -q 2>/dev/null; then
        say "starting PostgreSQL"
        if ! $SUDO service postgresql start >/dev/null 2>&1; then
            version="$(pg_lsclusters -h | awk 'NR==1 {print $1}')"
            $SUDO pg_ctlcluster "$version" main start
        fi
        for _ in $(seq 1 30); do
            pg_isready -q 2>/dev/null && break
            sleep 1
        done
        pg_isready -q || die "PostgreSQL did not start"
    fi
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
    say "database: larry (connection \"dbname=larry\"; set LARRY_DB to use another)"
fi

# 6. Done -------------------------------------------------------------------
say "ready. Next:"
say "  cmake -S $ROOT -B $ROOT/build"
say "  cmake --build $ROOT/build"
say "  ctest --test-dir $ROOT/build"
say "  $ROOT/build/larry show \"the sky is blue\""
