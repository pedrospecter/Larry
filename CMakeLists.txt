cmake_minimum_required(VERSION 3.28)

# Larry needs C++23 <print>. On Linux the default compiler is often too old
# for it (Ubuntu 24.04 ships GCC 13), so when no compiler was chosen, prefer
# the newest GCC that scripts/setup.sh installs. macOS keeps Apple clang.
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux" AND NOT DEFINED CMAKE_CXX_COMPILER AND NOT DEFINED ENV{CXX})
    find_program(LARRY_NEWER_GXX NAMES g++-15 g++-14)
    if(LARRY_NEWER_GXX)
        set(CMAKE_CXX_COMPILER "${LARRY_NEWER_GXX}")
    endif()
endif()

project(larry LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(CheckCXXSourceCompiles)
set(CMAKE_REQUIRED_FLAGS "-std=c++23")
check_cxx_source_compiles(
    "#include <algorithm>\n#include <array>\n#include <print>\nint main() { std::println(\"{}\", std::ranges::contains(std::array{1}, 1)); }"
    LARRY_HAS_CXX23_LIBRARY)
unset(CMAKE_REQUIRED_FLAGS)
if(NOT LARRY_HAS_CXX23_LIBRARY)
    message(FATAL_ERROR
        "${CMAKE_CXX_COMPILER} has no C++23 <print>. Larry needs GCC 14 or newer, or Clang 18 "
        "or newer with libc++ 18. Run scripts/setup.sh, or configure with "
        "-DCMAKE_CXX_COMPILER=<compiler>.")
endif()

option(LARRY_SANITIZE "Build with address and undefined behaviour sanitizers" OFF)
if(LARRY_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

# The database: PostgreSQL through libpq, which every distribution and
# Homebrew package (libpq-dev, libpq). pg_config gives the paths when it is
# there. Without libpq, or with -DLARRY_CLOUD=OFF, Larry builds with the
# cache alone and says so.
option(LARRY_CLOUD "Build the cloud (needs libpq)" ON)
if(LARRY_CLOUD)
    execute_process(COMMAND pg_config --includedir OUTPUT_VARIABLE LARRY_PG_INCLUDE
                    OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    execute_process(COMMAND pg_config --libdir OUTPUT_VARIABLE LARRY_PG_LIB
                    OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    find_path(PQ_INCLUDE_DIR libpq-fe.h
              HINTS ${LARRY_PG_INCLUDE} /opt/homebrew/opt/libpq/include /usr/local/opt/libpq/include
                    /usr/include/postgresql /usr/local/include)
    find_library(PQ_LIBRARY pq
                 HINTS ${LARRY_PG_LIB} /opt/homebrew/opt/libpq/lib /usr/local/opt/libpq/lib /usr/local/lib)
endif()
if(LARRY_CLOUD AND PQ_INCLUDE_DIR AND PQ_LIBRARY)
    set(LARRY_HAS_CLOUD ON)
    message(STATUS "libpq: ${PQ_LIBRARY}")
else()
    set(LARRY_HAS_CLOUD OFF)
    message(WARNING "libpq not found: Larry builds without the cloud (cache only). "
                    "Install it (apt: libpq-dev, brew: libpq) and configure again.")
endif()

set(LARRY_WARNINGS -Wall -Wextra -Wpedantic -Wconversion)

add_library(larry_core)
target_sources(larry_core PRIVATE src/arithmetic.cpp src/assimilation.cpp src/atom_operations.cpp src/base_rules.cpp
                                  src/brain.cpp src/calendar.cpp src/cognition.cpp src/connection.cpp src/content.cpp
                                  src/dictionary.cpp src/grammar.cpp src/harness.cpp src/lesson.cpp
                                  src/memory.cpp src/study.cpp src/tolerance.cpp src/web.cpp)
target_compile_definitions(larry_core
                           PUBLIC LARRY_BASE_RULES_DIR="${CMAKE_SOURCE_DIR}/base_rules"
                                  LARRY_DICTIONARY_DIR="${CMAKE_SOURCE_DIR}/dictionary"
                                  LARRY_LESSONS_DIR="${CMAKE_SOURCE_DIR}/lessons"
                                  LARRY_MEMORY_DIR="${CMAKE_SOURCE_DIR}/memory"
                                  LARRY_ROOT_DIR="${CMAKE_SOURCE_DIR}"
                                  LARRY_SQL_DIR="${CMAKE_SOURCE_DIR}/sql"
                                  LARRY_CONTENT_DIR="${CMAKE_SOURCE_DIR}/content")
target_include_directories(larry_core PUBLIC include)
if(LARRY_HAS_CLOUD)
    target_sources(larry_core PRIVATE src/database.cpp)
    target_compile_definitions(larry_core PUBLIC LARRY_HAS_CLOUD=1)
    target_include_directories(larry_core SYSTEM PUBLIC ${PQ_INCLUDE_DIR})
    target_link_libraries(larry_core PUBLIC ${PQ_LIBRARY})
else()
    target_sources(larry_core PRIVATE src/no_database.cpp)
endif()
target_compile_options(larry_core PRIVATE ${LARRY_WARNINGS})

add_executable(larry src/main.cpp)
target_link_libraries(larry PRIVATE larry_core)
target_compile_options(larry PRIVATE ${LARRY_WARNINGS})

# Tests: one executable per class (PLAN.md, item F4), run with ctest. The
# database test needs a PostgreSQL server (scripts/setup.sh starts one) and
# reports "skipped" (exit 77) when it cannot connect.
enable_testing()
set(LARRY_TESTS arithmetic assimilation atom_operations base_rules brain calendar cognition connection content database dictionary grammar harness lesson memory study tolerance web)
foreach(name ${LARRY_TESTS})
    add_executable(test_${name} tests/test_${name}.cpp)
    target_link_libraries(test_${name} PRIVATE larry_core)
    target_compile_definitions(test_${name} PRIVATE LARRY_TEST_DATA_DIR="${CMAKE_SOURCE_DIR}/tests/data")
    target_compile_options(test_${name} PRIVATE ${LARRY_WARNINGS})
    add_test(NAME ${name} COMMAND test_${name})
endforeach()
set_tests_properties(database PROPERTIES SKIP_RETURN_CODE 77)
