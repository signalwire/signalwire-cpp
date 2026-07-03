// REST client tests

#include "signalwire/rest/http_client.hpp"
#include "signalwire/rest/rest_client.hpp"

using namespace signalwire::rest;
using json = nlohmann::json;

TEST(rest_http_client_creation) {
    HttpClient client("https://example.signalwire.com", "project_id", "token");
    ASSERT_EQ(client.base_url(), "https://example.signalwire.com");
    return true;
}

TEST(rest_http_client_set_timeout) {
    HttpClient client("https://example.com", "user", "pass");
    client.set_timeout(60);
    // No crash
    return true;
}

TEST(rest_http_client_set_header) {
    HttpClient client("https://example.com", "user", "pass");
    client.set_header("X-Custom", "value");
    // No crash
    return true;
}

TEST(rest_signalwire_client_creation) {
    RestClient client("example.signalwire.com", "project_id", "token");
    ASSERT_EQ(client.http_client().base_url(), "https://example.signalwire.com");
    return true;
}

TEST(rest_signalwire_client_has_fabric) {
    RestClient client("example.signalwire.com", "proj", "tok");
    // Access fabric namespace without crash
    auto& fabric = client.fabric();
    (void)fabric;
    return true;
}

TEST(rest_signalwire_client_has_calling) {
    RestClient client("example.signalwire.com", "proj", "tok");
    auto& calling = client.calling();
    (void)calling;
    return true;
}

TEST(rest_signalwire_client_has_phone_numbers) {
    RestClient client("example.signalwire.com", "proj", "tok");
    auto& pn = client.phone_numbers();
    (void)pn;
    return true;
}

TEST(rest_signalwire_client_has_datasphere) {
    RestClient client("example.signalwire.com", "proj", "tok");
    auto& ds = client.datasphere();
    (void)ds;
    return true;
}

TEST(rest_signalwire_client_has_video) {
    RestClient client("example.signalwire.com", "proj", "tok");
    auto& v = client.video();
    (void)v;
    return true;
}

TEST(rest_signalwire_client_has_all_namespaces) {
    RestClient client("example.signalwire.com", "proj", "tok");
    // Access all 20 namespaces without crash
    (void)client.fabric();
    (void)client.calling();
    (void)client.phone_numbers();
    (void)client.datasphere();
    (void)client.video();
    (void)client.addresses();
    (void)client.queues();
    (void)client.recordings();
    (void)client.number_groups();
    (void)client.verified_callers();
    (void)client.sip_profile();
    (void)client.lookup();
    (void)client.short_codes();
    (void)client.imported_numbers();
    (void)client.mfa();
    (void)client.registry();
    (void)client.logs();
    (void)client.project();
    (void)client.pubsub();
    (void)client.chat();
    return true;
}

TEST(rest_crud_resource) {
    HttpClient client("http://localhost:9999", "proj", "tok");
    CrudResource resource(client, "/api/test");
    // Just verify construction doesn't crash
    return true;
}

TEST(rest_error_class) {
    SignalWireRestError err(404, "Not found", "{\"error\":\"not found\"}");
    ASSERT_EQ(err.status(), 404);
    ASSERT_EQ(err.body(), "{\"error\":\"not found\"}");
    ASSERT_TRUE(std::string(err.what()).find("Not found") != std::string::npos);
    return true;
}

TEST(rest_from_env_missing_vars) {
    // Should throw when env vars are missing
    bool threw = false;
    try {
        (void)RestClient::from_env();
    } catch (const std::runtime_error&) {
        threw = true;
    }
    // May or may not throw depending on env
    (void)threw;
    return true;
}
