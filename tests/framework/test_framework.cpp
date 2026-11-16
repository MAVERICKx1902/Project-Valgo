#include "test_framework.h"

#include <algorithm>
#include <cstring>

namespace valgo_test {
namespace {

TestContext g_context;

}  // namespace

Registry& Registry::Get() {
    static Registry instance;
    return instance;
}

void Registry::Add(TestCase test_case) { tests_.push_back(test_case); }

TestContext& CurrentContext() { return g_context; }

void TestContext::RecordFailure(const char* file, int line, const std::string& message) {
    ++failures;
    std::printf("    [FAIL] %s:%d  %s\n", file, line, message.c_str());
    std::printf("           in %s :: %s\n", suite, name);
}

int RunAllTests(int argc, char** argv) {
    std::string filter;
    bool list_only = false;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i] != nullptr ? argv[i] : "";
        if (argument == "--list") {
            list_only = true;
        } else if (argument.rfind("--filter=", 0) == 0) {
            filter = argument.substr(9);
        } else if (argument.rfind("--suite=", 0) == 0) {
            filter = argument.substr(8);
        }
    }

    std::vector<TestCase> selected;
    for (const TestCase& test : Registry::Get().tests()) {
        if (filter.empty() || std::string(test.suite).find(filter) != std::string::npos ||
            std::string(test.name).find(filter) != std::string::npos) {
            selected.push_back(test);
        }
    }

    if (list_only) {
        for (const TestCase& test : selected) {
            std::printf("%.*s :: %.*s\n", static_cast<int>(test.suite.size()), test.suite.data(),
                        static_cast<int>(test.name.size()), test.name.data());
        }
        return 0;
    }

    std::printf("Running %zu test(s)", selected.size());
    if (!filter.empty()) {
        std::printf(" matching '%s'", filter.c_str());
    }
    std::printf("\n\n");

    int failed_tests = 0;
    int total_checks = 0;
    std::string current_suite;
    for (const TestCase& test : selected) {
        if (current_suite != test.suite) {
            current_suite = std::string(test.suite);
            std::printf("[%s]\n", current_suite.c_str());
        }
        g_context = TestContext{};
        g_context.suite = test.suite.data();
        g_context.name = test.name.data();

        test.function();

        total_checks += g_context.checks;
        if (g_context.failures > 0) {
            ++failed_tests;
            std::printf("  FAIL  %.*s (%d failed check(s))\n", static_cast<int>(test.name.size()),
                        test.name.data(), g_context.failures);
        } else {
            std::printf("  ok    %.*s\n", static_cast<int>(test.name.size()), test.name.data());
        }
    }

    std::printf("\n%zu test(s), %d failed, %d check(s)\n", selected.size(), failed_tests, total_checks);
    return failed_tests == 0 ? 0 : 1;
}

}  // namespace valgo_test

int main(int argc, char** argv) { return valgo_test::RunAllTests(argc, argv); }
