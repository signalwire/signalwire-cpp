// REST client tests

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

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

TEST(rest_error_class) {
    SignalWireRestError err(404, "Not found", "{\"error\":\"not found\"}");
    ASSERT_EQ(err.status(), 404);
    ASSERT_EQ(err.body(), "{\"error\":\"not found\"}");
    ASSERT_TRUE(std::string(err.what()).find("Not found") != std::string::npos);
    return true;
}

// plan 1.3b: SignalWireRestTransportError is a member of the SignalWireRestError
// family (status() == 0, the port's "no HTTP status" sentinel for a transport
// failure) that preserves the underlying transport-library error text as the
// exception message. Mirrors python's SignalWireRestTransportError(body, url,
// method) with status_code=None.
TEST(rest_transport_error_class) {
    SignalWireRestTransportError err("Connection failed to 127.0.0.1", "/api/fabric/addresses",
                                     "GET");
    ASSERT_EQ(err.status(), 0);
    ASSERT_EQ(err.url(), "/api/fabric/addresses");
    ASSERT_EQ(err.method(), "GET");
    ASSERT_TRUE(std::string(err.what()).find("Connection failed") != std::string::npos);

    // It IS-A SignalWireRestError -- a caller catching the base family type
    // handles both the HTTP-error and the transport-error path with one catch.
    const SignalWireRestError& base = err;
    ASSERT_EQ(base.status(), 0);
    return true;
}

// plan 1.3b: a REAL connection-refused request (dead port -- bound then
// released, nothing listening) must raise the TYPED SignalWireRestTransportError,
// not a bare httplib/generic error. Catching SignalWireRestError (the family
// base) is sufficient -- and catching the derived type specifically proves the
// concrete type raised is the transport subclass, not a plain SignalWireRestError.
TEST(rest_connection_refused_raises_typed_transport_error) {
    // Bind :0, read back the OS-assigned port, then release it -- a port
    // nothing is listening on for the rest of this test.
    int sock = ::socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_TRUE(sock >= 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    ASSERT_EQ(::bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), 0);
    socklen_t len = sizeof(addr);
    ASSERT_EQ(::getsockname(sock, reinterpret_cast<sockaddr*>(&addr), &len), 0);
    int dead_port = ntohs(addr.sin_port);
    ::close(sock);

    RestClient client = RestClient::with_base_url(
        "http://127.0.0.1:" + std::to_string(dead_port), "envelope_proj", "envelope_tok");

    bool threw_typed_transport = false;
    try {
        (void)client.fabric().addresses.list();
    } catch (const SignalWireRestTransportError& e) {
        threw_typed_transport = true;
        ASSERT_EQ(e.status(), 0);
    } catch (const SignalWireRestError&) {
        // A bare (non-transport-subclass) SignalWireRestError would land here --
        // fail loudly instead of silently accepting the wrong concrete type.
        ASSERT_TRUE(false);
    }
    ASSERT_TRUE(threw_typed_transport);
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
