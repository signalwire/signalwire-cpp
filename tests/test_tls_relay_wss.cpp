// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
//
// TLS capability test: prove the RELAY client performs a REAL, verified WSS
// handshake.
//
// One of the three cross-port "every SDK does verified HTTPS + WSS" quadrants
// (template: signalwire-go b6b2b6d). It points the real RelayClient at the
// shared mock_relay running in --tls mode (wss://, backed by the porting-sdk
// self-signed test CA), trusts that CA via SSL_CERT_FILE, and drives the full
// connect + authenticate handshake over TLS.
//
// CA trust is wired idiomatically: SSL_CERT_FILE -> test CA (IXWebSocket's
// OpenSSL backend honors it through ix::SocketTLSOptions, which our
// WebSocketClient::connect() reads). No verification is disabled, no transport
// mock: the server-issued RELAY protocol string returned by the connect
// round-trip can only come back over a genuinely-completed TLS session.
//
// The connect() path here is the production TLS path (NOT connect_plain):
// SIGNALWIRE_RELAY_SCHEME is left unset so RelayClient::connect() calls
// WebSocketClient::connect() (wss://). A negative control drives a raw
// WebSocketClient with an EMPTY trust store and asserts the handshake is
// rejected, proving the cert is actually verified.

#include "tls_mocktest.hpp"
#include "signalwire/relay/client.hpp"
#include "signalwire/relay/websocket.hpp"

#include <cstdlib>

// #included into the single test_main.cpp TU -> avoid file-scope
// `using namespace`; use targeted declarations instead.
namespace tt = signalwire::tlstest;
using signalwire::relay::RelayConfig;
using signalwire::relay::RelayClient;
using signalwire::relay::WebSocketClient;
using nlohmann::json;

TEST(tls_relay_client_wss_connect_authenticate) {
    if (tt::ca_cert_path().empty() || !tt::relay_tls_available()) {
        // TLS mock not reachable / certs missing -> skip cleanly (infra), the
        // same discipline as the conftest mock-discovery skip. CI runs the
        // --tls mock so the assertions below actually execute.
        std::cerr << "(skipped: mock_relay --tls not reachable on "
                  << tt::relay_tls_http_url() << ") ";
        return true;
    }

    tt::trust_test_ca();              // SSL_CERT_FILE -> test CA, before any dial
    tt::relay_journal_reset();

    // Production TLS path: leave SIGNALWIRE_RELAY_SCHEME UNSET so connect()
    // routes through WebSocketClient::connect() (wss://), not connect_plain().
    ::unsetenv("SIGNALWIRE_RELAY_SCHEME");

    RelayConfig cfg;
    cfg.project = "test_proj";
    cfg.token = "test_tok";
    // Connect by the DNS name the test cert was issued for (SAN DNS:localhost,
    // resolves to 127.0.0.1 via /etc/hosts). TLS hostname verification matches
    // against DNS SANs, not bare IP literals — this is the production pattern
    // (you reach a TLS endpoint by its certificate name, not its IP).
    cfg.host = "localhost";
    cfg.port = tt::relay_tls_ws_port();
    cfg.contexts = {"default"};

    RelayClient client(cfg);
    bool ok = client.connect();
    ASSERT_TRUE(ok);                    // connect+authenticate completed over TLS
    ASSERT_TRUE(client.is_connected());

    // Behavioral proof the TLS session carried a real RELAY handshake: the
    // mock only issues a protocol string on a successful credential exchange.
    std::string proto = client.relay_protocol();
    ASSERT_FALSE(proto.empty());
    ASSERT_TRUE(proto.find("signalwire") != std::string::npos);

    // Wire proof: the mock journaled the inbound signalwire.connect frame on
    // the same (TLS) WebSocket, carrying our credentials.
    auto recvs = tt::relay_journal_recv("signalwire.connect");
    ASSERT_FALSE(recvs.empty());
    json auth = recvs.back()["frame"]["params"]["authentication"];
    ASSERT_EQ(auth.value("project", std::string("x")), std::string("test_proj"));
    ASSERT_EQ(auth.value("token", std::string("x")), std::string("test_tok"));

    client.disconnect();
    ASSERT_FALSE(client.is_connected());

    // Negative control: the same wss:// endpoint must reject a client that does
    // NOT trust the test CA, proving real certificate verification is in force.
    // Point SSL_CERT_FILE at a path with no valid CA (the server cert itself is
    // not a CA for itself under default verification) so the chain can't build.
    {
        ::setenv("SSL_CERT_FILE", "/dev/null", 1);  // empty/invalid trust store
        WebSocketClient raw;
        bool neg_ok = raw.connect("localhost", tt::relay_tls_ws_port());
        ASSERT_FALSE(neg_ok);                 // handshake must fail (cert unverifiable)
        ASSERT_FALSE(raw.is_connected());
        tt::trust_test_ca();                  // restore trust for any later tests
    }
    return true;
}
