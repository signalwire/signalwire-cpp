// MCP gateway skill tests
#include "signalwire/skills/skill_registry.hpp"
#include "tls_mocktest.hpp"
#include "httplib.h"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <thread>
namespace sw_skills = signalwire::skills;
using json = nlohmann::json;

TEST(skill_mcp_name) {
    auto skill = sw_skills::SkillRegistry::instance().create("mcp_gateway");
    ASSERT_EQ(skill->skill_name(), "mcp_gateway");
    return true;
}

TEST(skill_mcp_setup_requires_url) {
    auto skill = sw_skills::SkillRegistry::instance().create("mcp_gateway");
    ASSERT_FALSE(skill->setup(json::object()));
    return true;
}

TEST(skill_mcp_setup_with_url) {
    auto skill = sw_skills::SkillRegistry::instance().create("mcp_gateway");
    ASSERT_TRUE(skill->setup(json::object({{"gateway_url", "https://mcp.example.com"}})));
    return true;
}

TEST(skill_mcp_registers_tools) {
    auto skill = sw_skills::SkillRegistry::instance().create("mcp_gateway");
    skill->setup(json::object({{"gateway_url", "https://mcp.example.com"}}));
    auto tools = skill->register_tools();
    // May or may not have tools depending on implementation
    // Just verify it doesn't crash
    (void)tools;
    return true;
}

TEST(skill_mcp_with_services) {
    auto skill = sw_skills::SkillRegistry::instance().create("mcp_gateway");
    skill->setup(json::object({
        {"gateway_url", "https://mcp.example.com"},
        {"services", json::array({
            json::object({{"name", "calendar"}}),
            json::object({{"name", "email"}})
        })}
    }));
    auto tools = skill->register_tools();
    // Tools should exist
    ASSERT_TRUE(tools.size() >= 1u);
    return true;
}

TEST(skill_mcp_tool_handler_works) {
    auto skill = sw_skills::SkillRegistry::instance().create("mcp_gateway");
    skill->setup(json::object({
        {"gateway_url", "https://mcp.example.com"},
        {"services", json::array({json::object({{"name", "svc1"}})})}
    }));
    auto tools = skill->register_tools();
    ASSERT_TRUE(tools.size() >= 1u);
    auto result = tools[0].handler(json::object({{"query", "test"}}), json::object());
    auto resp = result.to_json()["response"].get<std::string>();
    ASSERT_FALSE(resp.empty());
    return true;
}

TEST(skill_mcp_has_hints) {
    auto skill = sw_skills::SkillRegistry::instance().create("mcp_gateway");
    skill->setup(json::object({
        {"gateway_url", "x"},
        {"services", json::array({json::object({{"name", "search"}})})}
    }));
    auto hints = skill->get_hints();
    ASSERT_TRUE(hints.size() >= 1u);
    bool has_mcp = false;
    for (const auto& h : hints) {
        if (h == "MCP" || h == "gateway") has_mcp = true;
    }
    ASSERT_TRUE(has_mcp);
    return true;
}

TEST(skill_mcp_global_data_is_object) {
    auto skill = sw_skills::SkillRegistry::instance().create("mcp_gateway");
    skill->setup(json::object({
        {"gateway_url", "https://mcp.example.com"},
        {"services", json::array({json::object({{"name", "svc1"}})})}
    }));
    auto gd = skill->get_global_data();
    ASSERT_TRUE(gd.is_object());
    return true;
}

// -- verify_ssl -----------------------------------------------------------
// The skill advertises verify_ssl (default TRUE) in its parameter schema so it
// is discoverable, and setting it false actually turns off server-certificate
// verification on the gateway HTTP call (proven behaviorally against an
// in-process HTTPS gateway whose cert is NOT in the client trust store).

TEST(skill_mcp_schema_exposes_verify_ssl_default_true) {
    auto skill = sw_skills::SkillRegistry::instance().create("mcp_gateway");
    auto schema = skill->get_parameter_schema();
    ASSERT_TRUE(schema.contains("verify_ssl"));
    ASSERT_EQ(schema["verify_ssl"].value("type", std::string("")), std::string("boolean"));
    // Secure default: verification ON unless explicitly disabled.
    ASSERT_TRUE(schema["verify_ssl"].value("default", false));
    return true;
}

namespace {

// Stand up an in-process HTTPS server using the shared test cert (signed by the
// test CA, which we deliberately do NOT trust here) and return its base URL.
// The server answers POST /services/<svc>/call with {"result": "..."}.
struct HttpsGateway {
    httplib::SSLServer* srv = nullptr;
    std::thread th;
    int port = 0;
    bool ok = false;

    explicit HttpsGateway(const std::string& certs_dir) {
        std::string cert = certs_dir + "/server.crt";
        std::string key = certs_dir + "/server.key";
        srv = new httplib::SSLServer(cert.c_str(), key.c_str());
        if (!srv->is_valid()) { return; }
        srv->Post(R"(/services/[^/]+/call)",
                  [](const httplib::Request&, httplib::Response& res) {
                      res.set_content(R"({"result":"gateway-ok"})", "application/json");
                  });
        th = std::thread([this]() {
            port = srv->bind_to_any_port("127.0.0.1");
            srv->listen_after_bind();
        });
        // Wait for bind.
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (port == 0 && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        ok = (port != 0);
    }
    ~HttpsGateway() {
        if (srv) srv->stop();
        if (th.joinable()) th.join();
        delete srv;
    }
    std::string base() const { return "https://127.0.0.1:" + std::to_string(port); }
};

std::string run_first_tool(const json& setup_params) {
    auto skill = sw_skills::SkillRegistry::instance().create("mcp_gateway");
    skill->setup(setup_params);
    auto tools = skill->register_tools();
    if (tools.empty()) return "<no-tools>";
    auto result = tools[0].handler(json::object({{"query", "hi"}}), json::object());
    return result.to_json()["response"].get<std::string>();
}

} // namespace

TEST(skill_mcp_verify_ssl_false_disables_cert_check) {
    std::string ca = signalwire::tlstest::ca_cert_path();
    if (ca.empty()) { std::cerr << "(skipped: test CA not found) "; return true; }
    std::string certs_dir = ca.substr(0, ca.find_last_of('/'));

    HttpsGateway gw(certs_dir);
    if (!gw.ok) { std::cerr << "(skipped: could not start https gateway) "; return true; }

    // Make sure NO ambient CA var makes the self-signed cert trusted, so
    // verify_ssl=true genuinely fails the handshake.
    ::unsetenv("SIGNALWIRE_REST_CA_FILE");
    ::unsetenv("SSL_CERT_FILE");

    json base_params = json::object({
        {"gateway_url", gw.base()},
        {"services", json::array({json::object({{"name", "svc1"}})})}
    });

    // verify_ssl=false -> cert verification OFF -> the call reaches the gateway
    // and returns its result.
    {
        json p = base_params;
        p["verify_ssl"] = false;
        std::string resp = run_first_tool(p);
        ASSERT_EQ(resp, std::string("gateway-ok"));
    }

    // verify_ssl=true (secure default form, set explicitly here) -> the
    // untrusted self-signed cert is rejected -> the call fails and never
    // returns the gateway payload.
    {
        json p = base_params;
        p["verify_ssl"] = true;
        std::string resp = run_first_tool(p);
        ASSERT_NE(resp, std::string("gateway-ok"));
    }
    return true;
}

TEST(skill_mcp_verify_ssl_default_verifies) {
    std::string ca = signalwire::tlstest::ca_cert_path();
    if (ca.empty()) { std::cerr << "(skipped: test CA not found) "; return true; }
    std::string certs_dir = ca.substr(0, ca.find_last_of('/'));

    HttpsGateway gw(certs_dir);
    if (!gw.ok) { std::cerr << "(skipped: could not start https gateway) "; return true; }

    ::unsetenv("SIGNALWIRE_REST_CA_FILE");
    ::unsetenv("SSL_CERT_FILE");

    // No verify_ssl key at all -> secure default TRUE -> untrusted cert rejected.
    json p = json::object({
        {"gateway_url", gw.base()},
        {"services", json::array({json::object({{"name", "svc1"}})})}
    });
    std::string resp = run_first_tool(p);
    ASSERT_NE(resp, std::string("gateway-ok"));
    return true;
}
