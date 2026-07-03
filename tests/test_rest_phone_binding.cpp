// REST phone-number binding tests — PhoneCallHandler enum contract and the
// typed set* helpers on the generated PhoneNumbers resource.
//
// The typed helpers wrap ``phone_numbers.update`` with the correct
// ``call_handler`` + companion field for each handler type. Each helper is
// exercised end-to-end against a local capture server so the on-wire body is
// pinned to the exact contract. A regression test pins the full happy-path
// binding to exactly one PUT against
// ``/api/relay/rest/phone_numbers/{sid}`` — no fabric.swml_webhooks.create
// call, no assign_phone_route call (the two post-mortem anti-patterns).

#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include <memory>
#include <chrono>
#include "signalwire/rest/http_client.hpp"
#include "signalwire/rest/rest_client.hpp"
#include "signalwire/rest/phone_call_handler.hpp"
#include "signalwire/rest/namespaces/generated/PhoneNumbers.hpp"
#include "httplib.h"

using namespace signalwire::rest;
using json = nlohmann::json;

// ---- Enum contract ----------------------------------------------------------

TEST(phone_call_handler_all_11_wire_values) {
    ASSERT_EQ(to_wire_string(PhoneCallHandler::RelayScript),      std::string("relay_script"));
    ASSERT_EQ(to_wire_string(PhoneCallHandler::LamlWebhooks),     std::string("laml_webhooks"));
    ASSERT_EQ(to_wire_string(PhoneCallHandler::LamlApplication),  std::string("laml_application"));
    ASSERT_EQ(to_wire_string(PhoneCallHandler::AiAgent),          std::string("ai_agent"));
    ASSERT_EQ(to_wire_string(PhoneCallHandler::CallFlow),         std::string("call_flow"));
    ASSERT_EQ(to_wire_string(PhoneCallHandler::RelayApplication), std::string("relay_application"));
    ASSERT_EQ(to_wire_string(PhoneCallHandler::RelayTopic),       std::string("relay_topic"));
    ASSERT_EQ(to_wire_string(PhoneCallHandler::RelayContext),     std::string("relay_context"));
    ASSERT_EQ(to_wire_string(PhoneCallHandler::RelayConnector),   std::string("relay_connector"));
    ASSERT_EQ(to_wire_string(PhoneCallHandler::VideoRoom),        std::string("video_room"));
    ASSERT_EQ(to_wire_string(PhoneCallHandler::Dialogflow),       std::string("dialogflow"));
    return true;
}

TEST(phone_call_handler_covers_11_members) {
    // Pins the enum count. If the server adds a value, bumping this also
    // forces an audit of the ``set*`` helpers.
    constexpr int expected = 11;
    int seen = 0;
    for (auto h : {
            PhoneCallHandler::RelayScript, PhoneCallHandler::LamlWebhooks,
            PhoneCallHandler::LamlApplication, PhoneCallHandler::AiAgent,
            PhoneCallHandler::CallFlow, PhoneCallHandler::RelayApplication,
            PhoneCallHandler::RelayTopic, PhoneCallHandler::RelayContext,
            PhoneCallHandler::RelayConnector, PhoneCallHandler::VideoRoom,
            PhoneCallHandler::Dialogflow,
        }) {
        (void)to_wire_string(h);
        seen++;
    }
    ASSERT_EQ(seen, expected);
    return true;
}

// ---- Local capture server ---------------------------------------------------
//
// Spin up a local httplib server, point the RestClient/PhoneNumbers at it, and
// invoke each set* helper. We capture the exact request(s) and assert on the
// on-wire method / path / body.

namespace {

struct CapturedRequest {
    std::string method;
    std::string path;
    std::string body;
};

class LocalCaptureServer {
public:
    LocalCaptureServer() {
        server_.Put(".*", [this](const httplib::Request& req, httplib::Response& res) {
            record(req, "PUT");
            res.status = 200;
            res.set_content("{}", "application/json");
        });
        server_.Post(".*", [this](const httplib::Request& req, httplib::Response& res) {
            record(req, "POST");
            res.status = 200;
            res.set_content("{}", "application/json");
        });
        server_.Delete(".*", [this](const httplib::Request& req, httplib::Response& res) {
            record(req, "DELETE");
            res.status = 200;
            res.set_content("{}", "application/json");
        });
        server_.Get(".*", [this](const httplib::Request& req, httplib::Response& res) {
            record(req, "GET");
            res.status = 200;
            res.set_content("{}", "application/json");
        });
        server_.Patch(".*", [this](const httplib::Request& req, httplib::Response& res) {
            record(req, "PATCH");
            res.status = 200;
            res.set_content("{}", "application/json");
        });

        // Bind to an ephemeral port
        port_ = server_.bind_to_any_port("127.0.0.1");
        thread_ = std::thread([this]() { server_.listen_after_bind(); });
        // Small settle-in wait so the server socket is fully accepting
        for (int i = 0; i < 50 && !server_.is_running(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    ~LocalCaptureServer() {
        server_.stop();
        if (thread_.joinable()) thread_.join();
    }

    int port() const { return port_; }
    std::string base_url() const { return "http://127.0.0.1:" + std::to_string(port_); }

    std::vector<CapturedRequest> captured() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return captured_;
    }

private:
    void record(const httplib::Request& req, const std::string& method) {
        std::lock_guard<std::mutex> lock(mutex_);
        captured_.push_back({method, req.path, req.body});
    }

    httplib::Server server_;
    std::thread thread_;
    int port_ = 0;
    mutable std::mutex mutex_;
    std::vector<CapturedRequest> captured_;
};

} // namespace

// ---- Body-wire tests (one per set* helper) ----------------------------------

TEST(phone_binding_swml_webhook_body) {
    LocalCaptureServer srv;
    HttpClient http(srv.base_url(), "proj", "tok");
    generated::PhoneNumbers pn(http);
    (void)pn.setSwmlWebhook("pn-1", {.url = "https://example.com/swml"});
    auto reqs = srv.captured();
    ASSERT_EQ(reqs.size(), 1u);
    json body = json::parse(reqs[0].body);
    ASSERT_EQ(body["call_handler"].get<std::string>(), std::string("relay_script"));
    ASSERT_EQ(body["call_relay_script_url"].get<std::string>(), std::string("https://example.com/swml"));
    ASSERT_EQ(body.size(), 2u);
    return true;
}

TEST(phone_binding_cxml_webhook_body_minimal) {
    LocalCaptureServer srv;
    HttpClient http(srv.base_url(), "proj", "tok");
    generated::PhoneNumbers pn(http);
    (void)pn.setCxmlWebhook("pn-1", {.url = "https://example.com/voice.xml"});
    auto reqs = srv.captured();
    ASSERT_EQ(reqs.size(), 1u);
    json body = json::parse(reqs[0].body);
    ASSERT_EQ(body["call_handler"].get<std::string>(), std::string("laml_webhooks"));
    ASSERT_EQ(body["call_request_url"].get<std::string>(), std::string("https://example.com/voice.xml"));
    ASSERT_FALSE(body.contains("call_fallback_url"));
    ASSERT_FALSE(body.contains("call_status_callback_url"));
    ASSERT_EQ(body.size(), 2u);
    return true;
}

TEST(phone_binding_cxml_webhook_body_with_options) {
    LocalCaptureServer srv;
    HttpClient http(srv.base_url(), "proj", "tok");
    generated::PhoneNumbers pn(http);
    (void)pn.setCxmlWebhook("pn-1", {
        .url = "https://example.com/voice.xml",
        .fallback_url = "https://example.com/fallback.xml",
        .status_callback_url = "https://example.com/status",
    });
    auto reqs = srv.captured();
    ASSERT_EQ(reqs.size(), 1u);
    json body = json::parse(reqs[0].body);
    ASSERT_EQ(body["call_handler"].get<std::string>(), std::string("laml_webhooks"));
    ASSERT_EQ(body["call_request_url"].get<std::string>(), std::string("https://example.com/voice.xml"));
    ASSERT_EQ(body["call_fallback_url"].get<std::string>(), std::string("https://example.com/fallback.xml"));
    ASSERT_EQ(body["call_status_callback_url"].get<std::string>(), std::string("https://example.com/status"));
    return true;
}

TEST(phone_binding_cxml_application_body) {
    LocalCaptureServer srv;
    HttpClient http(srv.base_url(), "proj", "tok");
    generated::PhoneNumbers pn(http);
    (void)pn.setCxmlApplication("pn-1", {.application_id = "app-1"});
    auto reqs = srv.captured();
    ASSERT_EQ(reqs.size(), 1u);
    json body = json::parse(reqs[0].body);
    ASSERT_EQ(body["call_handler"].get<std::string>(), std::string("laml_application"));
    ASSERT_EQ(body["call_laml_application_id"].get<std::string>(), std::string("app-1"));
    ASSERT_EQ(body.size(), 2u);
    return true;
}

TEST(phone_binding_ai_agent_body) {
    LocalCaptureServer srv;
    HttpClient http(srv.base_url(), "proj", "tok");
    generated::PhoneNumbers pn(http);
    (void)pn.setAiAgent("pn-1", {.agent_id = "agent-1"});
    auto reqs = srv.captured();
    ASSERT_EQ(reqs.size(), 1u);
    json body = json::parse(reqs[0].body);
    ASSERT_EQ(body["call_handler"].get<std::string>(), std::string("ai_agent"));
    ASSERT_EQ(body["call_ai_agent_id"].get<std::string>(), std::string("agent-1"));
    ASSERT_EQ(body.size(), 2u);
    return true;
}

TEST(phone_binding_call_flow_body_minimal) {
    LocalCaptureServer srv;
    HttpClient http(srv.base_url(), "proj", "tok");
    generated::PhoneNumbers pn(http);
    (void)pn.setCallFlow("pn-1", {.flow_id = "cf-1"});
    auto reqs = srv.captured();
    ASSERT_EQ(reqs.size(), 1u);
    json body = json::parse(reqs[0].body);
    ASSERT_EQ(body["call_handler"].get<std::string>(), std::string("call_flow"));
    ASSERT_EQ(body["call_flow_id"].get<std::string>(), std::string("cf-1"));
    ASSERT_FALSE(body.contains("call_flow_version"));
    ASSERT_EQ(body.size(), 2u);
    return true;
}

TEST(phone_binding_call_flow_body_with_version) {
    LocalCaptureServer srv;
    HttpClient http(srv.base_url(), "proj", "tok");
    generated::PhoneNumbers pn(http);
    (void)pn.setCallFlow("pn-1", {.flow_id = "cf-1", .version = "current_deployed"});
    auto reqs = srv.captured();
    ASSERT_EQ(reqs.size(), 1u);
    json body = json::parse(reqs[0].body);
    ASSERT_EQ(body["call_handler"].get<std::string>(), std::string("call_flow"));
    ASSERT_EQ(body["call_flow_id"].get<std::string>(), std::string("cf-1"));
    ASSERT_EQ(body["call_flow_version"].get<std::string>(), std::string("current_deployed"));
    return true;
}

TEST(phone_binding_relay_application_body) {
    LocalCaptureServer srv;
    HttpClient http(srv.base_url(), "proj", "tok");
    generated::PhoneNumbers pn(http);
    (void)pn.setRelayApplication("pn-1", {.name = "my-app"});
    auto reqs = srv.captured();
    ASSERT_EQ(reqs.size(), 1u);
    json body = json::parse(reqs[0].body);
    ASSERT_EQ(body["call_handler"].get<std::string>(), std::string("relay_application"));
    ASSERT_EQ(body["call_relay_application"].get<std::string>(), std::string("my-app"));
    ASSERT_EQ(body.size(), 2u);
    return true;
}

TEST(phone_binding_relay_topic_body_minimal) {
    LocalCaptureServer srv;
    HttpClient http(srv.base_url(), "proj", "tok");
    generated::PhoneNumbers pn(http);
    (void)pn.setRelayTopic("pn-1", {.topic = "office"});
    auto reqs = srv.captured();
    ASSERT_EQ(reqs.size(), 1u);
    json body = json::parse(reqs[0].body);
    ASSERT_EQ(body["call_handler"].get<std::string>(), std::string("relay_topic"));
    ASSERT_EQ(body["call_relay_topic"].get<std::string>(), std::string("office"));
    ASSERT_FALSE(body.contains("call_relay_topic_status_callback_url"));
    return true;
}

TEST(phone_binding_relay_topic_body_with_status_callback) {
    LocalCaptureServer srv;
    HttpClient http(srv.base_url(), "proj", "tok");
    generated::PhoneNumbers pn(http);
    (void)pn.setRelayTopic("pn-1", {
        .topic = "office",
        .status_callback_url = "https://example.com/status",
    });
    auto reqs = srv.captured();
    ASSERT_EQ(reqs.size(), 1u);
    json body = json::parse(reqs[0].body);
    ASSERT_EQ(body["call_handler"].get<std::string>(), std::string("relay_topic"));
    ASSERT_EQ(body["call_relay_topic"].get<std::string>(), std::string("office"));
    ASSERT_EQ(body["call_relay_topic_status_callback_url"].get<std::string>(),
              std::string("https://example.com/status"));
    return true;
}

// ---- Regression: the post-mortem happy path ---------------------------------
//
// Call ``setSwmlWebhook`` and assert:
//   - exactly ONE HTTP request was made
//   - method = PUT
//   - path = /api/relay/rest/phone_numbers/{sid}
//   - NOT /api/fabric/... (no swml_webhooks create)
//   - NOT /phone_routes (no assign_phone_route)
//   - body matches the wire contract exactly

TEST(phone_binding_regression_swml_single_put) {
    LocalCaptureServer srv;
    HttpClient http(srv.base_url(), "proj", "tok");
    generated::PhoneNumbers pn(http);

    (void)pn.setSwmlWebhook("pn-1", {.url = "https://example.com/swml"});

    auto reqs = srv.captured();
    // Exactly one HTTP request — NOT two (no fabric resource pre-creation)
    ASSERT_EQ(reqs.size(), 1u);
    ASSERT_EQ(reqs[0].method, std::string("PUT"));
    ASSERT_EQ(reqs[0].path, std::string("/api/relay/rest/phone_numbers/pn-1"));
    // Anti-pattern guards: the path is NOT a fabric webhook create and NOT
    // an assign_phone_route call.
    ASSERT_TRUE(reqs[0].path.find("/api/fabric/") == std::string::npos);
    ASSERT_TRUE(reqs[0].path.find("/phone_routes") == std::string::npos);
    ASSERT_TRUE(reqs[0].path.find("swml_webhooks") == std::string::npos);

    json body = json::parse(reqs[0].body);
    ASSERT_EQ(body["call_handler"].get<std::string>(), std::string("relay_script"));
    ASSERT_EQ(body["call_relay_script_url"].get<std::string>(), std::string("https://example.com/swml"));
    return true;
}

TEST(phone_binding_regression_wire_level_form) {
    // A user who doesn't know about the enum can still use the wire value
    // directly through ``update`` — identical on-wire.
    LocalCaptureServer srv;
    HttpClient http(srv.base_url(), "proj", "tok");
    generated::PhoneNumbers pn(http);

    (void)pn.update("pn-1", {
        {"call_handler", "relay_script"},
        {"call_relay_script_url", "https://example.com/swml"},
    });

    auto reqs = srv.captured();
    ASSERT_EQ(reqs.size(), 1u);
    ASSERT_EQ(reqs[0].method, std::string("PUT"));
    ASSERT_EQ(reqs[0].path, std::string("/api/relay/rest/phone_numbers/pn-1"));
    json body = json::parse(reqs[0].body);
    ASSERT_EQ(body["call_handler"].get<std::string>(), std::string("relay_script"));
    return true;
}
