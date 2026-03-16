#include <iostream>
#include <string>
#include <cassert>
#include <functional>
#include <vector>
#include "signalwire/logging.hpp"

// ========================================================================
// Minimal test framework (no external deps)
// ========================================================================

static int tests_passed = 0;
static int tests_failed = 0;
static int tests_total = 0;

struct TestCase {
    std::string name;
    std::function<bool()> func;
};

static std::vector<TestCase>& get_tests() {
    static std::vector<TestCase> tests;
    return tests;
}

#define TEST(name) \
    static bool test_##name(); \
    static bool _reg_##name = (get_tests().push_back({#name, test_##name}), true); \
    static bool test_##name()

#define ASSERT_TRUE(x) do { \
    if (!(x)) { \
        std::cerr << "  FAIL: " << #x << " at " << __FILE__ << ":" << __LINE__ << "\n"; \
        return false; \
    } \
} while(0)

#define ASSERT_FALSE(x) ASSERT_TRUE(!(x))

#define ASSERT_EQ(a, b) do { \
    auto _a = (a); auto _b = (b); \
    if (_a != _b) { \
        std::cerr << "  FAIL: " << #a << " == " << #b << " at " << __FILE__ << ":" << __LINE__ << "\n"; \
        return false; \
    } \
} while(0)

#define ASSERT_NE(a, b) do { \
    auto _a = (a); auto _b = (b); \
    if (_a == _b) { \
        std::cerr << "  FAIL: " << #a << " != " << #b << " at " << __FILE__ << ":" << __LINE__ << "\n"; \
        return false; \
    } \
} while(0)

#define ASSERT_THROWS(expr) do { \
    bool threw = false; \
    try { expr; } catch (...) { threw = true; } \
    if (!threw) { \
        std::cerr << "  FAIL: expected exception from " << #expr << " at " << __FILE__ << ":" << __LINE__ << "\n"; \
        return false; \
    } \
} while(0)

// Include all test files
#include "test_swml.cpp"
#include "test_function_result.cpp"
#include "test_security.cpp"
#include "test_datamap.cpp"
#include "test_contexts.cpp"
#include "test_agent.cpp"
#include "test_skills.cpp"
#include "test_prefabs.cpp"
#include "test_server.cpp"
#include "test_rest.cpp"
#include "test_relay.cpp"

int main() {
    // Suppress logging during tests
    signalwire::Logger::instance().suppress();

    std::cerr << "Running " << get_tests().size() << " tests...\n\n";

    for (const auto& tc : get_tests()) {
        tests_total++;
        std::cerr << "  " << tc.name << "... ";
        try {
            if (tc.func()) {
                tests_passed++;
                std::cerr << "OK\n";
            } else {
                tests_failed++;
            }
        } catch (const std::exception& e) {
            tests_failed++;
            std::cerr << "EXCEPTION: " << e.what() << "\n";
        } catch (...) {
            tests_failed++;
            std::cerr << "UNKNOWN EXCEPTION\n";
        }
    }

    std::cerr << "\n========================================\n";
    std::cerr << "Total: " << tests_total
              << "  Passed: " << tests_passed
              << "  Failed: " << tests_failed << "\n";
    std::cerr << "========================================\n";

    return tests_failed > 0 ? 1 : 0;
}
