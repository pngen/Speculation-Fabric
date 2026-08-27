// Speculation Fabric - self-contained test framework.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs
//
// A minimal, dependency-free harness so the repository is standalone (it does
// not fetch external test libraries). Tests register via SF_TEST_FN and are
// run by sf_test::run_all(); a non-zero exit signals failure.

#pragma once

#include <cstdio>
#include <string>
#include <vector>

namespace sf_test {

struct Test {
    const char* name;
    void (*fn)();
};

inline std::vector<Test>& registry() {
    static std::vector<Test> r;
    return r;
}
inline int& failure_count() {
    static int c = 0;
    return c;
}
inline int& check_count() {
    static int c = 0;
    return c;
}
inline std::string& current_mut() {
    static std::string c;
    return c;
}

struct Registrar {
    Registrar(const char* name, void (*fn)()) { registry().push_back(Test{name, fn}); }
};

inline void fail(const char* file, int line, const std::string& msg) {
    ++failure_count();
    std::printf("FAIL [%s, %s:%d] %s\n", current_mut().c_str(), file, line, msg.c_str());
}

inline int run_all() {
    int total_checks = 0;
    for (const auto& t : registry()) {
        current_mut() = t.name;
        std::printf("[ RUN ] %s\n", t.name); std::fflush(stdout);
        const int before_fail = failure_count();
        const int before_check = check_count();
        t.fn();
        total_checks += (check_count() - before_check);
        std::printf("[  %s  ] %s (%d checks)\n",
                    failure_count() == before_fail ? "OK" : "FAIL", t.name,
                    check_count() - before_check);
        std::fflush(stdout);
    }
    std::printf("TOTAL checks=%d failures=%d\n", total_checks, failure_count());
    return failure_count() == 0 ? 0 : 1;
}

}  // namespace sf_test

#define SF_TEST_FN(name)                          \
    static void name();                           \
    static ::sf_test::Registrar sf_reg_##name(    \
        #name, &name);                            \
    static void name()

#define SF_CHECK(cond)                                   \
    do {                                                 \
        ++::sf_test::check_count();                      \
        if (!(cond)) {                                   \
            ::sf_test::fail(__FILE__, __LINE__,          \
                            std::string("check failed: ") + #cond); \
        }                                                \
    } while (0)

#define SF_CHECK_EQ(a, b)                                      \
    do {                                                       \
        ++::sf_test::check_count();                            \
        const auto& _a = (a);                                  \
        const auto& _b = (b);                                  \
        if (!(_a == _b)) {                                     \
            ::sf_test::fail(__FILE__, __LINE__,                \
                            std::string("not equal: ") + #a);  \
        }                                                      \
    } while (0)

#define SF_CHECK_NE(a, b)                                      \
    do {                                                       \
        ++::sf_test::check_count();                            \
        const auto& _a = (a);                                  \
        const auto& _b = (b);                                  \
        if (_a == _b) {                                        \
            ::sf_test::fail(__FILE__, __LINE__,                \
                            std::string("unexpected equal: ") + #a); \
        }                                                      \
    } while (0)