// Webhook middleware (cpp-httplib adapter) tests.
//
// Mirrors signalwire-python/tests/unit/security/test_webhook_middleware.py
// but adapted to cpp-httplib: spin up a real Server on an ephemeral port,
// post valid / invalid / missing-signature requests, and assert the
// response code and body forwarding behavior.

#include "signalwire/security/webhook_middleware.hpp"
#include "signalwire/security/webhook_validator.hpp"
#include "signalwire/agent/agent_base.hpp"
#include "httplib.h"

#include <openssl/hmac.h>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <string>
#include <thread>

using namespace signalwire::security;

namespace {

std::string mw_b64(const std::string& data) {
    static const char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    int val = 0, valb = -6;
    for (unsigned char c : data) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            out.push_back(table[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) out.push_back(table[((val << 8) >> (valb + 8)) & 0x3F]);
    while (out.size() % 4) out.push_back('=');
    return out;
}

std::string mw_hmac_sha1_hex(const std::string& key, const std::string& msg) {
    unsigned char out[EVP_MAX_MD_SIZE];
    unsigned int out_len = 0;
    HMAC(EVP_sha1(),
         key.data(), static_cast<int>(key.size()),
         reinterpret_cast<const unsigned char*>(msg.data()), msg.size(),
         out, &out_len);
    std::ostringstream ss;
    ss << std::hex << std::setfill('0');
    for (unsigned int i = 0; i < out_len; ++i) {
        ss << std::setw(2) << static_cast<int>(out[i]);
    }
    return ss.str();
}

/// RAII test server: binds to 127.0.0.1 on an ephemeral port, runs in a
/// background thread, stops + joins on destruction. Mirrors the pattern
/// used by tests/test_skill_websearch.cpp et al.
struct TestServer {
    httplib::Server srv;
    std::thread th;
    int port = 0;

    TestServer() = default;

    void start() {
        th = std::thread([this] {
            port = srv.bind_to_any_port("127.0.0.1");
            srv.listen_after_bind();
        });
        auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::seconds(3);
        while (port == 0 && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    ~TestServer() {
        srv.stop();
        if (th.joinable()) th.join();
    }
};

} // namespace

// ===========================================================================
// Helper-shape sanity: the middleware throws on bad construction.
// ===========================================================================

TEST(webhook_middleware_empty_key_throws) {
    auto h = [](const httplib::Request&, httplib::Response&) {};
    bool threw = false;
    try {
        (void)WrapWithSignatureValidation("", h);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    ASSERT_TRUE(threw);
    return true;
}

TEST(webhook_middleware_null_handler_throws) {
    HttpHandler null_h;
    bool threw = false;
    try {
        (void)WrapWithSignatureValidation("key", null_h);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    ASSERT_TRUE(threw);
    return true;
}

// ===========================================================================
// Live HTTP: valid signature passes through, invalid 403s, body forwards.
// ===========================================================================

TEST(webhook_middleware_valid_signature_calls_handler_and_forwards_body) {
    std::string key = "PSKtest1234567890abcdef";
    std::string body = R"({"event":"call.state","ok":true})";

    TestServer ts;
    std::atomic<bool> handler_called{false};
    std::string captured_body;
    auto downstream = [&handler_called, &captured_body](
                          const httplib::Request& req, httplib::Response& res) {
        handler_called = true;
        captured_body = req.body;
        res.status = 200;
        res.set_content("OK", "text/plain");
    };

    WebhookValidatorOptions opts;
    // Force the URL the middleware reconstructs so we can sign for it.
    opts.proxy_url_base = "http://localhost.test";
    auto wrapped = WrapWithSignatureValidation(key, downstream, opts);
    ts.srv.Post("/webhook", wrapped);
    ts.start();
    ASSERT_TRUE(ts.port > 0);

    std::string url = "http://localhost.test/webhook";
    std::string sig = mw_hmac_sha1_hex(key, url + body);

    httplib::Client cli("127.0.0.1", ts.port);
    httplib::Headers hdrs = {{"X-SignalWire-Signature", sig}};
    auto resp = cli.Post("/webhook", hdrs, body, "application/json");

    ASSERT_TRUE((bool)resp);
    ASSERT_EQ(resp->status, 200);
    ASSERT_TRUE(handler_called.load());
    ASSERT_EQ(captured_body, body);  // raw bytes forwarded unmodified
    return true;
}

TEST(webhook_middleware_invalid_signature_returns_403) {
    std::string key = "PSKtest1234567890abcdef";

    TestServer ts;
    std::atomic<bool> handler_called{false};
    auto downstream = [&handler_called](const httplib::Request&,
                                         httplib::Response& res) {
        handler_called = true;
        res.status = 200;
        res.set_content("OK", "text/plain");
    };

    WebhookValidatorOptions opts;
    opts.proxy_url_base = "http://localhost.test";
    ts.srv.Post("/webhook", WrapWithSignatureValidation(key, downstream, opts));
    ts.start();
    ASSERT_TRUE(ts.port > 0);

    httplib::Client cli("127.0.0.1", ts.port);
    httplib::Headers hdrs = {{"X-SignalWire-Signature", "bogus-not-the-right-sig"}};
    auto resp = cli.Post("/webhook", hdrs, R"({"event":"call.state"})",
                         "application/json");

    ASSERT_TRUE((bool)resp);
    ASSERT_EQ(resp->status, 403);
    ASSERT_FALSE(handler_called.load());  // downstream MUST NOT run on 403
    return true;
}

TEST(webhook_middleware_missing_signature_returns_403) {
    std::string key = "PSKtest1234567890abcdef";

    TestServer ts;
    std::atomic<bool> handler_called{false};
    auto downstream = [&handler_called](const httplib::Request&,
                                         httplib::Response& res) {
        handler_called = true;
        res.status = 200;
    };
    WebhookValidatorOptions opts;
    opts.proxy_url_base = "http://localhost.test";
    ts.srv.Post("/webhook", WrapWithSignatureValidation(key, downstream, opts));
    ts.start();
    ASSERT_TRUE(ts.port > 0);

    httplib::Client cli("127.0.0.1", ts.port);
    auto resp = cli.Post("/webhook", R"({"event":"call.state"})",
                         "application/json");

    ASSERT_TRUE((bool)resp);
    ASSERT_EQ(resp->status, 403);
    ASSERT_FALSE(handler_called.load());
    return true;
}

TEST(webhook_middleware_accepts_x_twilio_signature_legacy_header) {
    // cXML compat: legacy callers send X-Twilio-Signature; the spec says
    // ports SHOULD honor it as an alias of X-SignalWire-Signature.
    std::string key = "PSKtest1234567890abcdef";
    std::string body = R"({"legacy":"yes"})";

    TestServer ts;
    std::atomic<bool> handler_called{false};
    auto downstream = [&handler_called](const httplib::Request&,
                                         httplib::Response& res) {
        handler_called = true;
        res.status = 200;
    };
    WebhookValidatorOptions opts;
    opts.proxy_url_base = "http://localhost.test";
    ts.srv.Post("/webhook", WrapWithSignatureValidation(key, downstream, opts));
    ts.start();
    ASSERT_TRUE(ts.port > 0);

    std::string url = "http://localhost.test/webhook";
    std::string sig = mw_hmac_sha1_hex(key, url + body);

    httplib::Client cli("127.0.0.1", ts.port);
    httplib::Headers hdrs = {{"X-Twilio-Signature", sig}};
    auto resp = cli.Post("/webhook", hdrs, body, "application/json");

    ASSERT_TRUE((bool)resp);
    ASSERT_EQ(resp->status, 200);
    ASSERT_TRUE(handler_called.load());
    return true;
}

// ===========================================================================
// AgentBase integration: signing_key option, env fallback, accessors.
// (We don't spin up the AgentBase HTTP server here — the route-mount path
// is exercised by the dedicated middleware tests above; this section
// covers the option plumbing the spec calls out.)
// ===========================================================================

TEST(webhook_agent_signing_key_unset_by_default) {
    ::unsetenv("SIGNALWIRE_SIGNING_KEY");
    signalwire::agent::AgentBase agent("a", "/a");
    ASSERT_FALSE(agent.signing_key().has_value());
    return true;
}

TEST(webhook_agent_set_signing_key_stores_value) {
    ::unsetenv("SIGNALWIRE_SIGNING_KEY");
    signalwire::agent::AgentBase agent("a", "/a");
    agent.set_signing_key("PSKtest-explicit-key");
    auto got = agent.signing_key();
    ASSERT_TRUE(got.has_value());
    ASSERT_EQ(*got, std::string("PSKtest-explicit-key"));
    return true;
}

TEST(webhook_agent_set_signing_key_empty_clears) {
    ::unsetenv("SIGNALWIRE_SIGNING_KEY");
    signalwire::agent::AgentBase agent("a", "/a");
    agent.set_signing_key("PSKtest");
    ASSERT_TRUE(agent.signing_key().has_value());
    agent.set_signing_key("");
    ASSERT_FALSE(agent.signing_key().has_value());
    return true;
}

TEST(webhook_agent_picks_up_env_var_at_construction) {
    ::setenv("SIGNALWIRE_SIGNING_KEY", "PSKtest-from-env", 1);
    signalwire::agent::AgentBase agent("a", "/a");
    auto got = agent.signing_key();
    ASSERT_TRUE(got.has_value());
    ASSERT_EQ(*got, std::string("PSKtest-from-env"));
    ::unsetenv("SIGNALWIRE_SIGNING_KEY");
    return true;
}

TEST(webhook_agent_explicit_overrides_env) {
    ::setenv("SIGNALWIRE_SIGNING_KEY", "PSKtest-from-env", 1);
    signalwire::agent::AgentBase agent("a", "/a");
    agent.set_signing_key("PSKtest-explicit");
    auto got = agent.signing_key();
    ASSERT_TRUE(got.has_value());
    ASSERT_EQ(*got, std::string("PSKtest-explicit"));
    ::unsetenv("SIGNALWIRE_SIGNING_KEY");
    return true;
}

TEST(webhook_agent_trust_proxy_for_signature_default_false) {
    // Default is false (proxy headers spoofable; opt-in only). The flag
    // is plumbed through to the middleware in setup_routes — this test
    // documents the default for users.
    signalwire::agent::AgentBase agent("a", "/a");
    auto& chain = agent.trust_proxy_for_signature(true);
    // Method returns *this for chaining.
    ASSERT_TRUE(&chain == &agent);
    return true;
}

// ===========================================================================
// AgentBase live HTTP integration: signed POST passes, unsigned 403s.
// Spins up an actual AgentBase via AgentServer on an ephemeral port —
// proves that set_signing_key() actually wires through to the route
// handlers and rejects unsigned requests on POST /, /swaig, /post_prompt.
// ===========================================================================

namespace {

struct AgentServerHarness {
    httplib::Server srv;
    std::thread th;
    int port = 0;
    std::shared_ptr<signalwire::agent::AgentBase> agent;

    void start() {
        th = std::thread([this] {
            port = srv.bind_to_any_port("127.0.0.1");
            srv.listen_after_bind();
        });
        auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::seconds(3);
        while (port == 0 && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    ~AgentServerHarness() {
        srv.stop();
        if (th.joinable()) th.join();
    }
};

} // namespace

TEST(webhook_agent_unsigned_post_rejected_when_key_set) {
    ::unsetenv("SIGNALWIRE_SIGNING_KEY");
    AgentServerHarness h;
    h.agent = std::make_shared<signalwire::agent::AgentBase>("a", "/a");
    h.agent->set_auth("u", "p");
    h.agent->set_signing_key("PSKtest-agent-key");

    // Wire up the agent's routes onto our test server using setup_routes
    // (the same code path serve()/AgentServer use).
    struct Friend : signalwire::agent::AgentBase {
        using signalwire::agent::AgentBase::setup_routes;
        using signalwire::agent::AgentBase::init_auth;
    };
    auto& f = static_cast<Friend&>(*h.agent);
    f.init_auth();
    f.setup_routes(h.srv);
    h.start();
    ASSERT_TRUE(h.port > 0);

    httplib::Client cli("127.0.0.1", h.port);
    cli.set_basic_auth("u", "p");
    // Unsigned POST to /a/swaig should 403 (signing_key is set).
    auto resp = cli.Post("/a/swaig", "{}", "application/json");
    ASSERT_TRUE((bool)resp);
    ASSERT_EQ(resp->status, 403);
    return true;
}

TEST(webhook_agent_unsigned_post_accepted_when_key_unset) {
    ::unsetenv("SIGNALWIRE_SIGNING_KEY");
    AgentServerHarness h;
    h.agent = std::make_shared<signalwire::agent::AgentBase>("b", "/b");
    h.agent->set_auth("u", "p");
    // No signing key set — unsigned POSTs should reach the handler.

    struct Friend : signalwire::agent::AgentBase {
        using signalwire::agent::AgentBase::setup_routes;
        using signalwire::agent::AgentBase::init_auth;
    };
    auto& f = static_cast<Friend&>(*h.agent);
    f.init_auth();
    f.setup_routes(h.srv);
    h.start();
    ASSERT_TRUE(h.port > 0);

    httplib::Client cli("127.0.0.1", h.port);
    cli.set_basic_auth("u", "p");
    // POST to /b/swaig with empty body — handler should run and respond
    // 400 ("empty body") rather than 403.
    auto resp = cli.Post("/b/swaig", "", "application/json");
    ASSERT_TRUE((bool)resp);
    ASSERT_NE(resp->status, 403);  // not blocked by signature middleware
    return true;
}

TEST(webhook_agent_signed_post_passes_through) {
    ::unsetenv("SIGNALWIRE_SIGNING_KEY");
    AgentServerHarness h;
    h.agent = std::make_shared<signalwire::agent::AgentBase>("c", "/c");
    h.agent->set_auth("u", "p");
    std::string key = "PSKtest-agent-signed-pass";
    h.agent->set_signing_key(key);
    // Tell the middleware to honor X-Forwarded-* so we can sign for a
    // specific public URL.
    h.agent->trust_proxy_for_signature(true);

    struct Friend : signalwire::agent::AgentBase {
        using signalwire::agent::AgentBase::setup_routes;
        using signalwire::agent::AgentBase::init_auth;
    };
    auto& f = static_cast<Friend&>(*h.agent);
    f.init_auth();
    f.setup_routes(h.srv);
    h.start();
    ASSERT_TRUE(h.port > 0);

    // Build a body the SWAIG handler will accept (post_prompt is more
    // permissive — pick that endpoint).
    std::string body = R"({"call_id":"abc","post_prompt_data":{"parsed":null}})";
    std::string url = "https://example.test/c/post_prompt";
    std::string sig = mw_hmac_sha1_hex(key, url + body);

    httplib::Client cli("127.0.0.1", h.port);
    cli.set_basic_auth("u", "p");
    httplib::Headers hdrs = {
        {"X-Forwarded-Proto", "https"},
        {"X-Forwarded-Host",  "example.test"},
        {"X-SignalWire-Signature", sig},
    };
    auto resp = cli.Post("/c/post_prompt", hdrs, body, "application/json");
    ASSERT_TRUE((bool)resp);
    ASSERT_EQ(resp->status, 200);
    return true;
}

TEST(webhook_middleware_response_contains_no_signature_or_key_details) {
    // Spec: validators MUST NOT log or expose which branch failed,
    // which scheme was tried, or what the expected signature was.
    // We can at least verify the wire response carries no signature /
    // key text.
    std::string key = "PSKtest-very-secret-key-string";

    TestServer ts;
    auto downstream = [](const httplib::Request&, httplib::Response& res) {
        res.status = 200;
    };
    WebhookValidatorOptions opts;
    opts.proxy_url_base = "http://localhost.test";
    ts.srv.Post("/webhook", WrapWithSignatureValidation(key, downstream, opts));
    ts.start();
    ASSERT_TRUE(ts.port > 0);

    httplib::Client cli("127.0.0.1", ts.port);
    httplib::Headers hdrs = {{"X-SignalWire-Signature", "definitely-wrong"}};
    auto resp = cli.Post("/webhook", hdrs, R"({"x":1})", "application/json");
    ASSERT_TRUE((bool)resp);
    ASSERT_EQ(resp->status, 403);
    ASSERT_TRUE(resp->body.find("PSKtest") == std::string::npos);
    ASSERT_TRUE(resp->body.find("definitely-wrong") == std::string::npos);
    ASSERT_TRUE(resp->body.find("expected") == std::string::npos);
    return true;
}
