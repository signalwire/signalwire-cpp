#include <iostream>
#include <string>
#include <cassert>
#include <functional>
#include <vector>
#include <atomic>
#include <cstdlib>
#include <mutex>
#include <thread>
#include "signalwire/logging.hpp"

// ========================================================================
// Minimal test framework (no external deps)
// ========================================================================

static std::atomic<int> tests_passed{0};
static std::atomic<int> tests_failed{0};
static std::atomic<int> tests_total{0};

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
#include "test_parameter_schema.cpp"
#include "test_media_enums.cpp"
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
#include "test_pom.cpp"
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
#include "test_skill_name_enum.cpp"

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
#include "test_tts_gender_enum.cpp"
#include "test_relay_states.cpp"

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

// Webhook signature validation (porting-sdk/webhooks.md)
#include "test_webhook_validator.cpp"
#include "test_webhook_middleware.cpp"
#include "test_security_utils.cpp"

// Mock-server-backed REST tests (translated from
// signalwire-python/tests/unit/rest/*.py). These hit the local
// mock_signalwire HTTP server on port 8772.
#include "test_rest_mock_calling.cpp"
#include "test_rest_mock_small.cpp"
#include "test_rest_mock_video.cpp"
#include "test_rest_mock_fabric.cpp"
#include "test_rest_mock_logs.cpp"
#include "test_rest_mock_registry.cpp"
#include "test_rest_mock_pagination.cpp"
#include "test_rest_full_coverage.cpp"

// TLS capability tests (template: signalwire-go b6b2b6d). Prove the SDK does
// REAL, certificate-verified TLS: REST https:// + RELAY wss:// against the
// porting-sdk --tls mocks, and the SDK's own httplib::SSLServer reached by a
// verifying client. Each pairs a positive (verified) assertion with a negative
// control (untrusted CA -> rejected). They skip cleanly when the --tls mocks
// aren't reachable (infra); CI brings the mocks up so the assertions run.
#include "test_tls_rest_https.cpp"
#include "test_tls_relay_wss.cpp"
#include "test_tls_server_https.cpp"

// Mock-RELAY-backed tests (translated from
// signalwire-python/tests/unit/relay/test_*_mock.py). These hit the local
// mock_relay WebSocket server on port 8782.
#include "test_relay_mock_connect.cpp"
#include "test_relay_mock_inbound_call.cpp"
#include "test_relay_mock_messaging.cpp"
#include "test_relay_mock_actions.cpp"
#include "test_relay_mock_convenience.cpp"
#include "test_relay_mock_event_dispatch.cpp"
#include "test_relay_mock_outbound_call.cpp"
#include "test_relay_states_mock.cpp"

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

    // Concurrency knob. SW_TEST_PARALLEL=<N> runs the test cases on an N-thread
    // pool; unset or "1" keeps the legacy serial behavior. The mock-backed
    // cases (REST + RELAY) are session-isolated — each test's harness scope is
    // a per-thread thread-local keyed by a unique session id (RELAY handshake
    // `sessionid`) or a unique random project's auth header (REST) — so they
    // run safely concurrently against the one shared mock. This is the point of
    // the parallel runner: prove session-isolation under real parallel load,
    // not merely keep the suite serial. Pure-unit cases are thread-safe too
    // (no shared mutable global state beyond the atomics/mutex below).
    int parallel = 1;
    if (const char* env = std::getenv("SW_TEST_PARALLEL")) {
        if (env && *env) {
            try {
                int n = std::stoi(env);
                if (n > 1) parallel = n;
            } catch (...) {}
        }
    }
    if (parallel > static_cast<int>(selected.size())) {
        parallel = static_cast<int>(selected.size());
    }
    if (parallel < 1) parallel = 1;

    std::cerr << "Running " << selected.size() << " tests";
    if (!filter.empty()) std::cerr << " (filter: " << filter << ")";
    if (parallel > 1) std::cerr << " on " << parallel << " threads";
    std::cerr << "...\n\n";

    std::mutex io_mutex;  // serialize stderr writes only

    auto run_one = [&io_mutex](const TestCase& tc) {
        tests_total++;
        std::string line;
        bool ok = false;
        std::string err;
        try {
            ok = tc.func();
        } catch (const std::exception& e) {
            err = std::string("EXCEPTION: ") + e.what();
        } catch (...) {
            err = "UNKNOWN EXCEPTION";
        }
        if (ok) {
            tests_passed++;
            line = "  " + tc.name + "... OK\n";
        } else {
            tests_failed++;
            line = "  " + tc.name + "... " + (err.empty() ? "FAILED" : err) + "\n";
        }
        std::lock_guard<std::mutex> lk(io_mutex);
        std::cerr << line;
    };

    if (parallel <= 1) {
        for (const auto& tc : selected) run_one(tc);
    } else {
        // Only the SESSION-ISOLATED mock-backed cases run concurrently — those
        // are the ones proving isolation under real parallel load. They are
        // named with a "rest_mock_" or "relay_mock_" prefix; each gets its own
        // RELAY session (handshake `sessionid`) or REST random-project auth
        // header, scoped via a per-thread thread-local. Everything else runs
        // serially: pure-unit cases touch process-global singletons (skill
        // registry, logger) and the TLS capability cases mutate global env
        // (SIGNALWIRE_RELAY_SCHEME / SSL_CERT_FILE) — neither is safe to run
        // concurrently with anything, and neither exercises session isolation.
        auto is_mock_backed = [](const std::string& n) {
            return n.rfind("rest_mock_", 0) == 0 || n.rfind("relay_mock_", 0) == 0;
        };
        std::vector<const TestCase*> par, serial;
        for (const auto& tc : selected) {
            if (is_mock_backed(tc.name)) par.push_back(&tc);
            else serial.push_back(&tc);
        }
        // Serial batch first (e.g. TLS sets/unsets global env), then the
        // concurrent mock-backed batch.
        for (const auto* tc : serial) run_one(*tc);
        std::atomic<size_t> next{0};
        auto worker = [&]() {
            for (;;) {
                size_t i = next.fetch_add(1);
                if (i >= par.size()) break;
                run_one(*par[i]);
            }
        };
        std::vector<std::thread> pool;
        pool.reserve(parallel);
        for (int t = 0; t < parallel; ++t) pool.emplace_back(worker);
        for (auto& th : pool) th.join();
    }

    std::cerr << "\n========================================\n";
    std::cerr << "Total: " << tests_total.load()
              << "  Passed: " << tests_passed.load()
              << "  Failed: " << tests_failed.load() << "\n";
    std::cerr << "========================================\n";

    return tests_failed.load() > 0 ? 1 : 0;
}
