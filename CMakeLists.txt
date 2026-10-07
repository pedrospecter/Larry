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

# The PostgreSQL database is parked (PLAN.md, section 4): memory lives in a
# local file for now. Build the database class with -DLARRY_POSTGRES=ON; it
# needs libpq and libpqxx 8.0.x (scripts/setup.sh --postgres).
option(LARRY_POSTGRES "Build the PostgreSQL database class" OFF)

set(LARRY_WARNINGS -Wall -Wextra -Wpedantic -Wconversion)

add_library(larry_core)
target_sources(larry_core PRIVATE src/assimilation.cpp src/atom_operations.cpp src/base_rules.cpp
                                  src/cognition.cpp src/memory.cpp)
target_compile_definitions(larry_core
                           PUBLIC LARRY_BASE_RULES_DIR="${CMAKE_SOURCE_DIR}/base_rules"
                                  LARRY_LESSONS_DIR="${CMAKE_SOURCE_DIR}/lessons"
                                  LARRY_MEMORY_DIR="${CMAKE_SOURCE_DIR}/memory")
target_include_directories(larry_core PUBLIC include)
target_compile_options(larry_core PRIVATE ${LARRY_WARNINGS})

if(LARRY_POSTGRES)
    find_path(PQXX_INCLUDE_DIR pqxx/pqxx
              HINTS /opt/homebrew/opt/libpqxx/include /usr/local/include REQUIRED)
    find_library(PQXX_LIBRARY pqxx HINTS /opt/homebrew/opt/libpqxx/lib /usr/local/lib REQUIRED)
    find_path(PQ_INCLUDE_DIR libpq-fe.h
              HINTS /opt/homebrew/opt/libpq/include /usr/include/postgresql REQUIRED)
    find_library(PQ_LIBRARY pq HINTS /opt/homebrew/opt/libpq/lib REQUIRED)
    target_sources(larry_core PRIVATE src/database.cpp)
    target_compile_definitions(larry_core PUBLIC LARRY_POSTGRES=1
                                                 LARRY_SQL_DIR="${CMAKE_SOURCE_DIR}/sql")
    target_include_directories(larry_core SYSTEM PUBLIC ${PQXX_INCLUDE_DIR} ${PQ_INCLUDE_DIR})
    target_link_libraries(larry_core PUBLIC ${PQXX_LIBRARY} ${PQ_LIBRARY})
endif()

add_executable(larry src/main.cpp)
target_link_libraries(larry PRIVATE larry_core)
target_compile_options(larry PRIVATE ${LARRY_WARNINGS})

# Tests: one executable per class (PLAN.md, item F4), run with ctest.
enable_testing()
set(LARRY_TESTS assimilation atom_operations base_rules cognition memory)
foreach(name ${LARRY_TESTS})
    add_executable(test_${name} tests/test_${name}.cpp)
    target_link_libraries(test_${name} PRIVATE larry_core)
    target_compile_definitions(test_${name} PRIVATE LARRY_TEST_DATA_DIR="${CMAKE_SOURCE_DIR}/tests/data")
    target_compile_options(test_${name} PRIVATE ${LARRY_WARNINGS})
    add_test(NAME ${name} COMMAND test_${name})
endforeach()
