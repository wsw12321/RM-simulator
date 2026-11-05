#pragma once
// Minimal counting assertion helper for the source-linked rule module tests.
#include <cstdlib>
#include <iostream>
#include <variant>

inline long g_checks = 0, g_failures = 0;
#define CHECK(condition)                                                                   \
    do {                                                                                   \
        ++g_checks;                                                                        \
        if (!(condition)) {                                                                \
            ++g_failures;                                                                  \
            std::cerr << __FILE__ << ':' << __LINE__ << ": CHECK failed: " #condition "\n"; \
        }                                                                                  \
    } while (0)

// Unwraps a Result<T>; records a failure (and returns T{}) if it holds an error.
template<class T, class E>
T ok(const std::variant<T, E>& r, const char* where = "") {
    ++g_checks;
    if (const auto* v = std::get_if<T>(&r)) return *v;
    ++g_failures;
    std::cerr << "unexpected error result " << where << "\n";
    return T{};
}
template<class T, class E>
bool is_error(const std::variant<T, E>& r, E expected) {
    const auto* e = std::get_if<E>(&r);
    return e && *e == expected;
}

inline int finish(const char* name) {
    if (g_failures) {
        std::cerr << "FAIL " << name << ": " << g_failures << " of " << g_checks << " assertions failed\n";
        return 1;
    }
    std::cout << "PASS " << name << ": " << g_checks << " assertions\n";
    return 0;
}
