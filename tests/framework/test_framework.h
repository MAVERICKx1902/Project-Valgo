// =============================================================================
// Project Valgo — minimal test framework.
//
// Dependency free by design: the engine must be buildable and testable with
// nothing but a C++20 compiler and CMake. The framework provides registries,
// expectations that record failures (instead of aborting, so one broken
// assertion does not hide the rest of a test), and a small command line filter.
// =============================================================================
#pragma once

#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "valgo/core/types.h"

namespace valgo_test {

struct TestCase {
    std::string_view suite;
    std::string_view name;
    void (*function)();
};

class Registry {
public:
    static Registry& Get();

    void Add(TestCase test_case);
    const std::vector<TestCase>& tests() const { return tests_; }

private:
    std::vector<TestCase> tests_;
};

struct TestContext {
    const char* suite = "";
    const char* name = "";
    int failures = 0;
    int checks = 0;

    void RecordFailure(const char* file, int line, const std::string& message);
};

TestContext& CurrentContext();

/// Parses argv: optional substring filter and --list.
int RunAllTests(int argc, char** argv);

class Registrar {
public:
    Registrar(std::string_view suite, std::string_view name, void (*function)()) {
        Registry::Get().Add(TestCase{suite, name, function});
    }
};

}  // namespace valgo_test

#define VALGO_TEST(suite_name, test_name)                                          \
    static void VALGO_CONCAT(test_body_, __LINE__)();                              \
    static ::valgo_test::Registrar VALGO_CONCAT(test_registrar_, __LINE__)(        \
        suite_name, test_name, &VALGO_CONCAT(test_body_, __LINE__));               \
    static void VALGO_CONCAT(test_body_, __LINE__)()

#define VALGO_CHECK(condition)                                                     \
    do {                                                                           \
        ::valgo_test::CurrentContext().checks += 1;                                \
        if (!(condition)) {                                                        \
            ::valgo_test::CurrentContext().RecordFailure(__FILE__, __LINE__,       \
                                                         "CHECK failed: " #condition); \
        }                                                                          \
    } while (false)

#define VALGO_REQUIRE(condition)                                                   \
    do {                                                                           \
        ::valgo_test::CurrentContext().checks += 1;                                \
        if (!(condition)) {                                                        \
            ::valgo_test::CurrentContext().RecordFailure(__FILE__, __LINE__,       \
                                                         "REQUIRE failed: " #condition); \
            return;                                                                \
        }                                                                          \
    } while (false)

#define VALGO_CHECK_MSG(condition, message)                                        \
    do {                                                                           \
        ::valgo_test::CurrentContext().checks += 1;                                \
        if (!(condition)) {                                                        \
            ::valgo_test::CurrentContext().RecordFailure(__FILE__, __LINE__,       \
                                                         std::string("CHECK failed: ") + (message)); \
        }                                                                          \
    } while (false)

#define VALGO_CHECK_NEAR(actual, expected, tolerance)                              \
    do {                                                                           \
        ::valgo_test::CurrentContext().checks += 1;                                \
        const double actual_value = static_cast<double>(actual);                   \
        const double expected_value = static_cast<double>(expected);               \
        if (std::fabs(actual_value - expected_value) > static_cast<double>(tolerance)) { \
            char buffer[256];                                                      \
            std::snprintf(buffer, sizeof(buffer),                                  \
                          "CHECK_NEAR failed: %s = %.6f, expected %.6f +/- %.6f",  \
                          #actual, actual_value, expected_value,                   \
                          static_cast<double>(tolerance));                         \
            ::valgo_test::CurrentContext().RecordFailure(__FILE__, __LINE__, buffer); \
        }                                                                          \
    } while (false)

#define VALGO_CHECK_EQ(actual, expected)                                           \
    do {                                                                           \
        ::valgo_test::CurrentContext().checks += 1;                                \
        if (!((actual) == (expected))) {                                           \
            ::valgo_test::CurrentContext().RecordFailure(__FILE__, __LINE__,       \
                                                         "CHECK_EQ failed: " #actual " == " #expected); \
        }                                                                          \
    } while (false)
