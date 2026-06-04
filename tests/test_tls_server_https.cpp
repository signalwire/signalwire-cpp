// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
//
// TLS capability test: prove the SDK's OWN in-process HTTP server serves a
// REAL, verified HTTPS endpoint.
//
// The third cross-port "every SDK does verified HTTPS + WSS" quadrant — the
// server side (template: signalwire-go b6b2b6d). It configures TLS the way the
// Python reference does (SWML_SSL_ENABLED + SWML_SSL_CERT_PATH/KEY_PATH ->
// security_config.py), which our serve() reads to construct an
// httplib::SSLServer (in-process TLS termination). It then reaches the
// unauthenticated /health route from an in-test httplib::Client over https://
// that trusts the test CA, asserting a real response over a completed TLS
// session.
//
// CA trust is wired via the SSLClient's set_ca_cert_path() + verification
// ENABLED. No InsecureSkipVerify. A negative subtest uses an EMPTY trust store
// and asserts the handshake is rejected, proving the server's cert is actually
// verified.

#include "tls_mocktest.hpp"
#include "signalwire/swml/service.hpp"
#include "httplib.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <string>
#include <thread>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

namespace tt = signalwire::tlstest;
using nlohmann::json;

namespace {

// Bind an ephemeral TCP port, then release it so the SSLServer can claim it.
// A small race window remains (the classic bind-0 pattern) but in a quiet test
// container it is reliable; the poll-until-up loop below tolerates a slow bind.
int pick_free_port() {
    httplib::Server probe;          // unused; we just need a socket helper
    (void)probe;
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    ::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    socklen_t len = sizeof(addr);
    ::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len);
    int port = ntohs(addr.sin_port);
    ::close(fd);
    return port;
}

} // namespace

TEST(tls_sdk_sslserver_verified_by_client) {
    std::string ca = tt::ca_cert_path();
    if (ca.empty()) {
        std::cerr << "(skipped: test CA not found) ";
        return true;
    }
    // Locate the leaf cert/key alongside the CA (server.crt / server.key).
    std::string certs_dir = ca.substr(0, ca.find_last_of('/'));
    std::string cert = certs_dir + "/server.crt";
    std::string key = certs_dir + "/server.key";

    int port = pick_free_port();

    // Configure TLS exactly like the Python reference (env-driven). serve()
    // reads these via resolve_tls_config_from_env() and builds an SSLServer.
    ::setenv("SWML_SSL_ENABLED", "true", 1);
    ::setenv("SWML_SSL_CERT_PATH", cert.c_str(), 1);
    ::setenv("SWML_SSL_KEY_PATH", key.c_str(), 1);
    ::unsetenv("PORT");  // don't let an ambient PORT override our chosen port

    signalwire::swml::Service svc;
    svc.set_name("tls-cap-test").set_host("127.0.0.1").set_port(port);

    // serve() blocks (listen), so run it in a thread and stop it on the way out.
    std::thread server_thread([&svc]() { svc.serve(); });

    // Clean up TLS env + the server regardless of assertion outcome.
    struct Guard {
        signalwire::swml::Service& s;
        std::thread& t;
        ~Guard() {
            s.stop();
            if (t.joinable()) t.join();
            ::unsetenv("SWML_SSL_ENABLED");
            ::unsetenv("SWML_SSL_CERT_PATH");
            ::unsetenv("SWML_SSL_KEY_PATH");
        }
    } guard{svc, server_thread};

    const std::string base = "https://127.0.0.1:" + std::to_string(port);

    // Poll /health over https:// (trusting the test CA) until the TLS listener
    // is up, then assert a real verified response.
    bool got_ok = false;
    int got_status = 0;
    std::string got_body;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline) {
        httplib::Client cli(base);
        cli.set_connection_timeout(1, 0);
        cli.set_ca_cert_path(ca.c_str());
        cli.enable_server_certificate_verification(true);
        auto res = cli.Get("/health");
        if (res && res->status == 200) {
            got_ok = true;
            got_status = res->status;
            got_body = res->body;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    ASSERT_TRUE(got_ok);                            // a verified TLS response came back
    ASSERT_EQ(got_status, 200);
    json payload = json::parse(got_body);
    ASSERT_EQ(payload.value("status", std::string("")), std::string("healthy"));

    // Negative control: a client that does NOT trust the test CA must be
    // rejected, proving the server presents a cert that is actually verified.
    {
        httplib::Client untrusted(base);
        untrusted.set_connection_timeout(3, 0);
        untrusted.set_ca_cert_path("/dev/null");    // empty trust store
        untrusted.enable_server_certificate_verification(true);
        auto neg = untrusted.Get("/health");
        ASSERT_FALSE(static_cast<bool>(neg));        // handshake/verify must fail
    }
    return true;
}
