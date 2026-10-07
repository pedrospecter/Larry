cmake_minimum_required(VERSION 3.28)
project(larry LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(LARRY_SANITIZE "Build with address and undefined behaviour sanitizers" OFF)
if(LARRY_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

find_path(PQXX_INCLUDE_DIR pqxx/pqxx HINTS /opt/homebrew/opt/libpqxx/include REQUIRED)
find_library(PQXX_LIBRARY pqxx HINTS /opt/homebrew/opt/libpqxx/lib REQUIRED)
find_path(PQ_INCLUDE_DIR libpq-fe.h HINTS /opt/homebrew/opt/libpq/include REQUIRED)
find_library(PQ_LIBRARY pq HINTS /opt/homebrew/opt/libpq/lib REQUIRED)

add_library(larry_core)
target_sources(larry_core PRIVATE src/atom_operations.cpp src/base_rules.cpp src/cognition.cpp
                                  src/database.cpp)
target_compile_definitions(larry_core
                           PRIVATE LARRY_BASE_RULES_DIR="${CMAKE_SOURCE_DIR}/base_rules")
target_include_directories(larry_core PUBLIC include)
target_include_directories(larry_core SYSTEM PUBLIC ${PQXX_INCLUDE_DIR} ${PQ_INCLUDE_DIR})
target_link_libraries(larry_core PUBLIC ${PQXX_LIBRARY} ${PQ_LIBRARY})
target_compile_options(larry_core PRIVATE -Wall -Wextra -Wpedantic -Wconversion)

add_executable(larry src/main.cpp)
target_link_libraries(larry PRIVATE larry_core)
