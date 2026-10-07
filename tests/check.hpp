#pragma once

// The test harness: no dependency, one executable per class (PLAN.md, F4).
//
//   TEST(name) { CHECK(x == 1); CHECK_THROWS(f(), std::invalid_argument); }
//   int main() { return larry::test::run(); }

#include <cstdio>
#include <exception>
#include <functional>
#include <print>
#include <source_location>
#include <string>
#include <string_view>
#include <vector>

namespace larry::test {

struct Case {
    std::string_view name;
    std::function<void()> body;
};

inline std::vector<Case>& cases() {
    static std::vector<Case> all;
    return all;
}

inline int& failures() {
    static int count = 0;
    return count;
}

struct Register {
    Register(std::string_view name, std::function<void()> body) {
        cases().push_back({name, std::move(body)});
    }
};

inline void fail(std::string_view what, std::source_location where) {
    ++failures();
    std::println(stderr, "{}:{}: FAILED: {}", where.file_name(), where.line(), what);
}

inline void check(bool ok, std::string_view what,
                  std::source_location where = std::source_location::current()) {
    if (!ok) {
        fail(what, where);
    }
}

/// The exit code for ctest: 0 when every check passed.
inline int run() {
    int ran = 0;
    for (const Case& c : cases()) {
        const int before = failures();
        try {
            c.body();
        } catch (const std::exception& e) {
            fail(std::string("unexpected exception: ") + e.what(), std::source_location::current());
        }
        ++ran;
        if (failures() != before) {
            std::println(stderr, "  in test {}", c.name);
        }
    }
    std::println("{} tests, {} failures", ran, failures());
    return failures() == 0 ? 0 : 1;
}

/// The exit code that tells ctest the test was skipped (SKIP_RETURN_CODE).
inline int skip(std::string_view why) {
    std::println("skipped: {}", why);
    return 77;
}

}  // namespace larry::test

#define LARRY_TEST_CONCAT2(a, b) a##b
#define LARRY_TEST_CONCAT(a, b) LARRY_TEST_CONCAT2(a, b)

#define TEST(name)                                                                       \
    static void LARRY_TEST_CONCAT(larry_test_, name)();                                  \
    static const ::larry::test::Register LARRY_TEST_CONCAT(larry_register_, name){       \
        #name, &LARRY_TEST_CONCAT(larry_test_, name)};                                   \
    static void LARRY_TEST_CONCAT(larry_test_, name)()

#define CHECK(condition) ::larry::test::check((condition), #condition)

#define CHECK_THROWS(expression, Exception)                                              \
    do {                                                                                 \
        bool larry_thrown = false;                                                       \
        try {                                                                            \
            (void)(expression);                                                          \
        } catch (const Exception&) {                                                     \
            larry_thrown = true;                                                         \
        } catch (...) {                                                                  \
        }                                                                                \
        ::larry::test::check(larry_thrown, #expression " throws " #Exception);          \
    } while (false)
