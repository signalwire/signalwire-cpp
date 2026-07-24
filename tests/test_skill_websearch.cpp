// Web search skill tests
#include "signalwire/skills/skill_registry.hpp"
#include "httplib.h"
#include <atomic>
#include <chrono>
#include <thread>
namespace sw_skills = signalwire::skills;
using json = nlohmann::json;

TEST(skill_websearch_name) {
    auto skill = sw_skills::SkillRegistry::instance().create("web_search");
    ASSERT_EQ(skill->skill_name(), "web_search");
    return true;
}

TEST(skill_websearch_multi_instance) {
    auto skill = sw_skills::SkillRegistry::instance().create("web_search");
    ASSERT_TRUE(skill->supports_multiple_instances());
    return true;
}

TEST(skill_websearch_version) {
    auto skill = sw_skills::SkillRegistry::instance().create("web_search");
    ASSERT_EQ(skill->skill_version(), "2.0.0");
    return true;
}

TEST(skill_websearch_setup_with_keys) {
    auto skill = sw_skills::SkillRegistry::instance().create("web_search");
    ASSERT_TRUE(skill->setup(json::object({
        {"api_key", "gkey"}, {"search_engine_id", "seid"}
    })));
    return true;
}

TEST(skill_websearch_registers_tool) {
    auto skill = sw_skills::SkillRegistry::instance().create("web_search");
    skill->setup(json::object({{"api_key", "k"}, {"search_engine_id", "s"}}));
    auto tools = skill->register_tools();
    ASSERT_EQ(tools.size(), 1u);
    ASSERT_EQ(tools[0].name, "web_search");
    return true;
}

TEST(skill_websearch_custom_tool_name) {
    auto skill = sw_skills::SkillRegistry::instance().create("web_search");
    skill->setup(json::object({{"api_key", "k"}, {"search_engine_id", "s"}, {"tool_name", "search"}}));
    auto tools = skill->register_tools();
    ASSERT_EQ(tools[0].name, "search");
    return true;
}

// Drive the handler against a local HTTP fixture: prove that the skill
// actually issues a real GET to its upstream and parses the items[]
// response, instead of returning canned data. The fixture serves on a
// kernel-assigned ephemeral port and answers the customsearch path with
// a minimal Google CSE-shaped body.
TEST(skill_websearch_handler_works) {
    httplib::Server srv;
    std::atomic<bool> got_request{false};
    std::string captured_path;
    srv.Get("/customsearch/v1", [&](const httplib::Request& req, httplib::Response& res) {
        got_request = true;
        captured_path = req.path + (req.params.empty() ? "" : "?...");
        res.set_content(R"({"items":[{"title":"Test Result","link":"https://t/1","snippet":"hit for test query"}]})",
                        "application/json");
    });

    int port = 0;
    std::thread th([&]{ port = srv.bind_to_any_port("127.0.0.1"); srv.listen_after_bind(); });
    // Spin until bound.
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (port == 0 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_TRUE(port > 0);

    ::setenv("WEB_SEARCH_BASE_URL", ("http://127.0.0.1:" + std::to_string(port)).c_str(), 1);
    auto skill = sw_skills::SkillRegistry::instance().create("web_search");
    skill->setup(json::object({{"api_key", "k"}, {"search_engine_id", "s"}}));
    auto tools = skill->register_tools();
    auto result = tools[0].handler(json::object({{"query", "test query"}}), json::object());
    auto resp = result.to_json()["response"].get<std::string>();

    srv.stop();
    th.join();
    ::unsetenv("WEB_SEARCH_BASE_URL");

    ASSERT_TRUE(got_request);  // proves the skill issued real HTTP
    ASSERT_TRUE(resp.find("test query") != std::string::npos);
    ASSERT_TRUE(resp.find("Test Result") != std::string::npos);  // proves real parse
    return true;
}

// ============================================================================
// response_prefix / response_postfix (Python parity: 8aad242)
// Wrap successful (non-empty) results only — error / transport-error paths
// are NOT wrapped. SWML key stays snake_case.
// ============================================================================

namespace {
// Helper: spin a fixture server returning a canned CSE body, point the
// skill at it, call the handler, return the response string. Cleans up
// the server + env var on the way out so test ordering doesn't matter.
static std::string run_websearch_with_params(const json& extra_params,
                                              const std::string& query = "test query") {
    httplib::Server srv;
    srv.Get("/customsearch/v1", [&](const httplib::Request&, httplib::Response& res) {
        res.set_content(R"({"items":[{"title":"Test Result","link":"https://t/1","snippet":"hit for test query"}]})",
                        "application/json");
    });
    int port = 0;
    std::thread th([&]{ port = srv.bind_to_any_port("127.0.0.1"); srv.listen_after_bind(); });
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (port == 0 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ::setenv("WEB_SEARCH_BASE_URL", ("http://127.0.0.1:" + std::to_string(port)).c_str(), 1);

    auto skill = sw_skills::SkillRegistry::instance().create("web_search");
    json setup_params = json::object({{"api_key", "k"}, {"search_engine_id", "s"}});
    for (auto& [k, v] : extra_params.items()) setup_params[k] = v;
    skill->setup(setup_params);
    auto tools = skill->register_tools();
    auto result = tools[0].handler(json::object({{"query", query}}), json::object());
    std::string resp = result.to_json()["response"].get<std::string>();

    srv.stop();
    th.join();
    ::unsetenv("WEB_SEARCH_BASE_URL");
    return resp;
}
}  // namespace

TEST(skill_websearch_no_prefix_postfix_unchanged) {
    // Baseline: without the params, the body has no wrapper text.
    std::string resp = run_websearch_with_params(json::object());
    ASSERT_TRUE(resp.find("Test Result") != std::string::npos);
    ASSERT_TRUE(resp.find("AGENT_HINT") == std::string::npos);
    ASSERT_TRUE(resp.find("SOURCE_NOTE") == std::string::npos);
    return true;
}

TEST(skill_websearch_response_prefix_wraps_success) {
    std::string resp = run_websearch_with_params(
        json::object({{"response_prefix", "AGENT_HINT: from-public-web"}}));
    // Prefix appears, response body appears, and prefix precedes the body.
    auto p = resp.find("AGENT_HINT: from-public-web");
    auto b = resp.find("Test Result");
    ASSERT_TRUE(p != std::string::npos);
    ASSERT_TRUE(b != std::string::npos);
    ASSERT_TRUE(p < b);
    return true;
}

TEST(skill_websearch_response_postfix_wraps_success) {
    std::string resp = run_websearch_with_params(
        json::object({{"response_postfix", "SOURCE_NOTE: public-search"}}));
    auto b = resp.find("Test Result");
    auto pf = resp.find("SOURCE_NOTE: public-search");
    ASSERT_TRUE(b != std::string::npos);
    ASSERT_TRUE(pf != std::string::npos);
    ASSERT_TRUE(b < pf);
    return true;
}

TEST(skill_websearch_response_prefix_and_postfix_both_wrap) {
    std::string resp = run_websearch_with_params(json::object({
        {"response_prefix", "AGENT_HINT: from-public-web"},
        {"response_postfix", "SOURCE_NOTE: public-search"}
    }));
    auto p = resp.find("AGENT_HINT: from-public-web");
    auto b = resp.find("Test Result");
    auto pf = resp.find("SOURCE_NOTE: public-search");
    ASSERT_TRUE(p != std::string::npos);
    ASSERT_TRUE(b != std::string::npos);
    ASSERT_TRUE(pf != std::string::npos);
    ASSERT_TRUE(p < b);
    ASSERT_TRUE(b < pf);
    return true;
}

// SECURITY (r5 F3.3): a web_search failure must NOT leak the Google CSE api_key
// into the returned FunctionResult (it flows into the AI transcript + logs). The
// error string embeds the request URL, which carries `?key=<api_key>&...`; the
// transport layer must redact the query before it reaches the user-visible error.
// Drive a real transport failure by pointing the base URL at a closed port, then
// assert the response contains neither the key nor `key=`.
TEST(skill_websearch_transport_error_redacts_api_key) {
    // Bind an ephemeral port, capture it, then release it so the connect refuses.
    int dead_port = 0;
    {
        httplib::Server probe;
        std::thread pth([&]{ dead_port = probe.bind_to_any_port("127.0.0.1"); });
        auto dl = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (dead_port == 0 && std::chrono::steady_clock::now() < dl) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        probe.stop();
        pth.join();
    }
    ASSERT_TRUE(dead_port > 0);

    const std::string secret = "AIzaSyLEAKTESTSECRETKEY0123456789";
    ::setenv("WEB_SEARCH_BASE_URL",
             ("http://127.0.0.1:" + std::to_string(dead_port)).c_str(), 1);
    auto skill = sw_skills::SkillRegistry::instance().create("web_search");
    skill->setup(json::object({{"api_key", secret}, {"search_engine_id", "seid"}}));
    auto tools = skill->register_tools();
    auto result = tools[0].handler(json::object({{"query", "test query"}}), json::object());
    std::string resp = result.to_json()["response"].get<std::string>();
    ::unsetenv("WEB_SEARCH_BASE_URL");

    // The key must never appear, and neither must the `key=` query param that
    // would carry it (redaction strips the whole query).
    ASSERT_TRUE(resp.find(secret) == std::string::npos);
    ASSERT_TRUE(resp.find("key=") == std::string::npos);
    return true;
}

TEST(skill_websearch_has_prompt_sections) {
    auto skill = sw_skills::SkillRegistry::instance().create("web_search");
    skill->setup(json::object({{"api_key", "k"}, {"search_engine_id", "s"}}));
    auto sections = skill->get_prompt_sections();
    ASSERT_TRUE(sections.size() >= 1u);
    return true;
}

TEST(skill_websearch_global_data) {
    auto skill = sw_skills::SkillRegistry::instance().create("web_search");
    skill->setup(json::object({{"api_key", "k"}, {"search_engine_id", "s"}}));
    auto gd = skill->get_global_data();
    ASSERT_TRUE(gd.contains("web_search_enabled"));
    return true;
}

// ============================================================================
// Latency-control params: per_page_timeout / overall_deadline / parallel_scrape
// / snippets_only + snippet fallback.
//
// Ports Python 51101da + 295745b. overall_deadline + per_page_timeout are the
// CONTRACT — a slow site must not blow past the kernel webhook timeout (~55s).
// These tests drive the deadline path deterministically against a local
// httplib content server that sleeps longer than the configured budget. The
// CSE result links point back at the SAME server so the scrape phase actually
// fetches the (slow) content endpoint.
// ============================================================================

namespace {

// A latency fixture: serves the Google CSE call instantly with `num_items`
// results (links point at this server's /pageK), and answers every OTHER path
// (the content fetch) after sleeping `content_delay_ms` — but in short,
// stop-checkable increments so srv.stop() in teardown returns fast even when a
// content fetch is mid-sleep. `hits` counts how many content fetches started,
// so a test can assert whether scraping was attempted.
struct LatencyFixture {
    httplib::Server srv;
    std::atomic<int> hits{0};
    std::atomic<bool> stopping{false};
    std::thread th;
    int port = 0;

    LatencyFixture(int num_items, int content_delay_ms) {
        srv.Get("/customsearch/v1", [this, num_items](const httplib::Request&,
                                                      httplib::Response& res) {
            json items = json::array();
            for (int i = 0; i < num_items; ++i) {
                items.push_back(json::object({
                    {"title", "Result " + std::to_string(i)},
                    {"link", "http://127.0.0.1:" + std::to_string(port) +
                             "/page" + std::to_string(i)},
                    {"snippet", "snippet " + std::to_string(i)}
                }));
            }
            res.set_content(json::object({{"items", items}}).dump(),
                            "application/json");
        });
        // Catch-all content endpoint: record the hit, then sleep in 25ms slices
        // up to content_delay_ms (or until teardown), then return rich HTML.
        srv.Get(R"(/page\d+)", [this, content_delay_ms](const httplib::Request&,
                                                        httplib::Response& res) {
            hits.fetch_add(1);
            int slept = 0;
            while (slept < content_delay_ms && !stopping.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(25));
                slept += 25;
            }
            res.set_content("<html><body><article>"
                            "Lots of relevant page content here. "
                            "Lots of relevant page content here.</article></body></html>",
                            "text/html");
        });
        th = std::thread([this] {
            port = srv.bind_to_any_port("127.0.0.1");
            srv.listen_after_bind();
        });
        auto dl = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (port == 0 && std::chrono::steady_clock::now() < dl) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        ::setenv("WEB_SEARCH_BASE_URL",
                 ("http://127.0.0.1:" + std::to_string(port)).c_str(), 1);
    }

    ~LatencyFixture() {
        stopping.store(true);
        srv.stop();
        if (th.joinable()) th.join();
        ::unsetenv("WEB_SEARCH_BASE_URL");
    }
};

// Run the registered web_search handler with `extra` setup params merged over a
// fast/deterministic baseline. Returns {response, elapsed_ms}.
static std::pair<std::string, long> run_latency_handler(const json& extra,
                                                        const std::string& query) {
    auto skill = sw_skills::SkillRegistry::instance().create("web_search");
    json setup = json::object({
        {"api_key", "k"}, {"search_engine_id", "s"}, {"num_results", 2}
    });
    for (auto& [k, v] : extra.items()) setup[k] = v;
    skill->setup(setup);
    auto tools = skill->register_tools();
    auto t0 = std::chrono::steady_clock::now();
    auto result = tools[0].handler(json::object({{"query", query}}), json::object());
    auto t1 = std::chrono::steady_clock::now();
    std::string resp = result.to_json()["response"].get<std::string>();
    long ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    return {resp, ms};
}

}  // namespace

// Defaults: per_page_timeout=2.0, overall_deadline=10.0, parallel_scrape=true,
// snippets_only=false are advertised in the schema with the matching type +
// default. (Setup() reads them; the schema is the observable surface.)
TEST(skill_websearch_latency_defaults_in_schema) {
    auto skill = sw_skills::SkillRegistry::instance().create("web_search");
    skill->setup(json::object({{"api_key", "k"}, {"search_engine_id", "s"}}));
    auto schema = skill->get_parameter_schema();
    ASSERT_TRUE(schema.contains("per_page_timeout"));
    ASSERT_EQ(schema["per_page_timeout"]["type"].get<std::string>(), "number");
    ASSERT_EQ(schema["per_page_timeout"]["default"].get<double>(), 2.0);
    ASSERT_TRUE(schema.contains("overall_deadline"));
    ASSERT_EQ(schema["overall_deadline"]["default"].get<double>(), 10.0);
    ASSERT_TRUE(schema.contains("parallel_scrape"));
    ASSERT_EQ(schema["parallel_scrape"]["type"].get<std::string>(), "boolean");
    ASSERT_EQ(schema["parallel_scrape"]["default"].get<bool>(), true);
    ASSERT_TRUE(schema.contains("snippets_only"));
    ASSERT_EQ(schema["snippets_only"]["default"].get<bool>(), false);
    return true;
}

// Schema drift guard (Python parity: test_every_setup_param_is_advertised).
// All 6 latency/response params must be advertised, each not required.
TEST(skill_websearch_schema_advertises_all_six) {
    auto skill = sw_skills::SkillRegistry::instance().create("web_search");
    skill->setup(json::object({{"api_key", "k"}, {"search_engine_id", "s"}}));
    auto schema = skill->get_parameter_schema();
    for (const char* key : {"response_prefix", "response_postfix",
                            "per_page_timeout", "overall_deadline",
                            "parallel_scrape", "snippets_only"}) {
        ASSERT_TRUE(schema.contains(key));
        ASSERT_EQ(schema[key]["required"].get<bool>(), false);
    }
    ASSERT_EQ(schema["response_prefix"]["default"].get<std::string>(), "");
    ASSERT_EQ(schema["response_postfix"]["default"].get<std::string>(), "");
    return true;
}

// snippets_only must short-circuit BEFORE any page fetch. The content endpoint
// would sleep 5s; if scraping ran the call would take ~5s. Instead it returns
// immediately with snippet-only formatting and ZERO content hits.
TEST(skill_websearch_snippets_only_skips_scraping) {
    LatencyFixture fx(/*num_items=*/2, /*content_delay_ms=*/5000);
    ASSERT_TRUE(fx.port > 0);

    auto [resp, ms] = run_latency_handler(
        json::object({{"snippets_only", true}}), "golang");

    ASSERT_EQ(fx.hits.load(), 0);          // proves no page was fetched
    ASSERT_TRUE(ms < 2000);                // sub-second-ish, not the 5s stall
    ASSERT_TRUE(resp.find("Snippet-only results for 'golang'") != std::string::npos);
    ASSERT_TRUE(resp.find("snippet 0") != std::string::npos);  // snippet carried
    ASSERT_TRUE(resp.find("page content not scraped") != std::string::npos);
    return true;
}

// overall_deadline IS THE CONTRACT: a content server that stalls 5s with a 1s
// budget must cause the call to return within ~deadline+slack (NOT 5s) AND fall
// back to the CSE snippets — never an empty no-results message. Parallel mode.
// per_page_timeout is set large so the DEADLINE (not per-page) is what truncates.
TEST(skill_websearch_overall_deadline_truncates_to_snippet_fallback) {
    LatencyFixture fx(/*num_items=*/3, /*content_delay_ms=*/5000);
    ASSERT_TRUE(fx.port > 0);

    auto [resp, ms] = run_latency_handler(json::object({
        {"overall_deadline", 1.0},   // budget well under the 5s content stall
        {"per_page_timeout", 30.0},  // large, so the deadline truncates
        {"parallel_scrape", true}
    }), "kubernetes");

    // Returned at the deadline, not after the full 5s stall (allow slack).
    ASSERT_TRUE(ms < 3000);
    // Non-empty snippet fallback, NOT the empty no-results / no-items message.
    ASSERT_TRUE(resp.find("Snippet-only results for 'kubernetes'") != std::string::npos);
    ASSERT_TRUE(resp.find("(no results)") == std::string::npos);
    ASSERT_TRUE(resp.find("snippet 0") != std::string::npos);
    ASSERT_FALSE(resp.empty());
    return true;
}

// Same contract in sequential mode (parallel_scrape=false). With a 5s-per-page
// stall and a 1s budget, per_page_timeout=0.5s force-fails each fetch; the loop
// then sees the (already-)passed deadline and breaks, falling back to snippets.
TEST(skill_websearch_overall_deadline_sequential_falls_back) {
    LatencyFixture fx(/*num_items=*/3, /*content_delay_ms=*/5000);
    ASSERT_TRUE(fx.port > 0);

    auto [resp, ms] = run_latency_handler(json::object({
        {"overall_deadline", 1.0},
        {"per_page_timeout", 0.5},   // a single fetch is force-failed at 0.5s
        {"parallel_scrape", false}
    }), "rustlang");

    ASSERT_TRUE(ms < 3000);
    ASSERT_TRUE(resp.find("Snippet-only results for 'rustlang'") != std::string::npos);
    ASSERT_TRUE(resp.find("(no results)") == std::string::npos);
    return true;
}

// per_page_timeout caps a single fetch independent of the overall budget. With
// a 5s content stall, a 0.3s per-page timeout, a generous 8s overall budget and
// parallel mode, every fetch errors near 0.3s, no page yields content, and we
// fall back to snippets WELL before the 5s stall (and before the 8s budget).
TEST(skill_websearch_per_page_timeout_honored) {
    LatencyFixture fx(/*num_items=*/2, /*content_delay_ms=*/5000);
    ASSERT_TRUE(fx.port > 0);

    auto [resp, ms] = run_latency_handler(json::object({
        {"per_page_timeout", 0.3},
        {"overall_deadline", 8.0},   // large, so it can't be what truncates
        {"parallel_scrape", true}
    }), "elixir");

    ASSERT_TRUE(ms < 3000);                 // governed by 0.3s per-page, not 5s/8s
    ASSERT_TRUE(fx.hits.load() > 0);        // fetches were attempted (then timed out)
    ASSERT_TRUE(resp.find("Snippet-only results for 'elixir'") != std::string::npos);
    return true;
}

// Happy path UNDER the deadline: a FAST content server (no stall) with parallel
// scraping returns fully-scraped results (the scraped header), NOT the snippet
// fallback — proving the std::async harvest path delivers real content when
// pages are quick.
TEST(skill_websearch_parallel_fast_path_scrapes_content) {
    LatencyFixture fx(/*num_items=*/3, /*content_delay_ms=*/0);
    ASSERT_TRUE(fx.port > 0);

    auto [resp, ms] = run_latency_handler(json::object({
        {"overall_deadline", 10.0},
        {"per_page_timeout", 5.0},
        {"parallel_scrape", true}
    }), "postgres");

    ASSERT_TRUE(fx.hits.load() > 0);        // pages were actually fetched
    // Fully-scraped output includes the page-content header + scraped marker.
    ASSERT_TRUE(resp.find("page content scraped") != std::string::npos);
    ASSERT_TRUE(resp.find("Content:") != std::string::npos);
    ASSERT_TRUE(resp.find("Snippet-only results") == std::string::npos);
    return true;
}

// Deadline fallback must still honor response_prefix/response_postfix wrapping
// (the snippet fallback is a "non-empty success" path, unlike the no-items
// branch). Proves wrapping composes with the latency machinery.
TEST(skill_websearch_snippet_fallback_is_wrapped) {
    LatencyFixture fx(/*num_items=*/2, /*content_delay_ms=*/5000);
    ASSERT_TRUE(fx.port > 0);

    auto [resp, ms] = run_latency_handler(json::object({
        {"overall_deadline", 1.0},
        {"per_page_timeout", 0.4},
        {"parallel_scrape", true},
        {"response_prefix", "WRAP_PRE"},
        {"response_postfix", "WRAP_POST"}
    }), "scala");

    ASSERT_TRUE(ms < 3000);
    auto pre = resp.find("WRAP_PRE");
    auto body = resp.find("Snippet-only results for 'scala'");
    auto post = resp.find("WRAP_POST");
    ASSERT_TRUE(pre != std::string::npos);
    ASSERT_TRUE(body != std::string::npos);
    ASSERT_TRUE(post != std::string::npos);
    ASSERT_TRUE(pre < body);
    ASSERT_TRUE(body < post);
    return true;
}
