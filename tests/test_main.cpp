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
// Original test files
#include "test_swml.cpp"
#include "test_swml_service_swaig.cpp"
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

// Utils
#include "test_url_validator.cpp"
#include "test_execution_mode.cpp"
#include "test_schema_utils_parity.cpp"

// Mixin-equivalent test files
#include "test_prompt.cpp"
#include "test_tool.cpp"
#include "test_aiconfig.cpp"
#include "test_web.cpp"
#include "test_auth.cpp"
#include "test_verb.cpp"
#include "test_render.cpp"

// Individual skill test files
#include "test_skill_datetime.cpp"
#include "test_skill_math.cpp"
#include "test_skill_joke.cpp"
#include "test_skill_weather.cpp"
#include "test_skill_websearch.cpp"
#include "test_skill_wikipedia.cpp"
#include "test_skill_spider.cpp"
#include "test_skill_datasphere.cpp"
#include "test_skill_datasphere_serverless.cpp"
#include "test_skill_transfer.cpp"
#include "test_skill_play_bg.cpp"
#include "test_skill_trivia.cpp"
#include "test_skill_info_gatherer.cpp"
#include "test_skill_vector_search.cpp"
#include "test_skill_claude.cpp"
#include "test_skill_mcp.cpp"
#include "test_skill_custom.cpp"
#include "test_skill_registry.cpp"

// Prefab test files
#include "test_prefab_info_gatherer.cpp"
#include "test_prefab_survey.cpp"
#include "test_prefab_receptionist.cpp"
#include "test_prefab_faqbot.cpp"
#include "test_prefab_concierge.cpp"

// RELAY split test files
#include "test_relay_call.cpp"
#include "test_relay_action.cpp"
#include "test_relay_event.cpp"
#include "test_relay_message.cpp"

// REST split test files
#include "test_rest_calling.cpp"
#include "test_rest_fabric.cpp"
#include "test_rest_namespaces.cpp"
#include "test_rest_phone_binding.cpp"

// MCP integration tests
#include "test_mcp.cpp"

// Utility test files
#include "test_schema_utils.cpp"
#include "test_logging.cpp"
#include "test_cli.cpp"

// Top-level signalwire:: convenience entry points
#include "test_signalwire_top_level.cpp"

// Mock-server-backed REST tests (translated from
// signalwire-python/tests/unit/rest/*.py). These hit the local
// mock_signalwire HTTP server on port 8772.
#include "test_rest_mock_compat_calls_streams.cpp"
#include "test_rest_mock_compat_messages_faxes.cpp"
#include "test_rest_mock_compat_phone_numbers.cpp"
#include "test_rest_mock_calling.cpp"
#include "test_rest_mock_small.cpp"
#include "test_rest_mock_compat_accounts.cpp"
#include "test_rest_mock_compat_misc.cpp"
#include "test_rest_mock_compat_conferences.cpp"
#include "test_rest_mock_compat_queues.cpp"
#include "test_rest_mock_compat_tokens.cpp"
#include "test_rest_mock_compat_recordings_transcriptions.cpp"
#include "test_rest_mock_video.cpp"
#include "test_rest_mock_fabric.cpp"
#include "test_rest_mock_logs.cpp"
#include "test_rest_mock_registry.cpp"
#include "test_rest_mock_pagination.cpp"

int main(int argc, char** argv) {
    // Suppress logging during tests
    signalwire::Logger::instance().suppress();

    // Optional filter argument. Catch2-style "[tag]" (e.g. "[rest_mock]")
    // is treated as a substring match against the test name with the
    // brackets stripped. A bare substring (no brackets) is also matched
    // verbatim so callers can run a single test by exact name.
    std::string filter;
    if (argc > 1) {
        filter = argv[1];
        if (!filter.empty() && filter.front() == '[' && filter.back() == ']') {
            filter = filter.substr(1, filter.size() - 2);
        }
    }

    auto matches_filter = [&filter](const std::string& name) {
        if (filter.empty()) return true;
        return name.find(filter) != std::string::npos;
    };

    std::vector<TestCase> selected;
    for (const auto& tc : get_tests()) {
        if (matches_filter(tc.name)) selected.push_back(tc);
    }

    std::cerr << "Running " << selected.size() << " tests";
    if (!filter.empty()) std::cerr << " (filter: " << filter << ")";
    std::cerr << "...\n\n";

    for (const auto& tc : selected) {
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
