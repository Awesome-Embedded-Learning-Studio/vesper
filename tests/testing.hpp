#pragma once

#include <cstdio>
#include <print>

namespace vesper::testing {

/// Cumulative count of failed checks, zero when everything passed. main()
/// maps it to the exit code.
inline int& failure_count() {
    static int count = 0;
    return count;
}

/// Prints one failure line and bumps the counter. Called by the assert
/// macros, not by test bodies.
inline void report(const char* file, int line, const char* detail) {
    ++failure_count();
    std::println(stderr, "FAIL {}:{} {}", file, line, detail);
}

} // namespace vesper::testing

/// On a false condition, prints the expression text and counts. No
/// exceptions, no abort: a failed check never hides the ones after it.
#define VESPER_CHECK(cond)                                        \
    do {                                                          \
        if (!(cond)) {                                            \
            ::vesper::testing::report(__FILE__, __LINE__, #cond); \
        }                                                         \
    } while (false)

/// Asserts both values are equal, prints both expression texts and counts on
/// mismatch.
#define VESPER_CHECK_EQ(actual, expected)                                            \
    do {                                                                             \
        const auto& vesper_check_actual_ = (actual);                                 \
        const auto& vesper_check_expected_ = (expected);                             \
        if (!(vesper_check_actual_ == vesper_check_expected_)) {                     \
            ::vesper::testing::report(__FILE__, __LINE__, #actual " == " #expected); \
        }                                                                            \
    } while (false)
