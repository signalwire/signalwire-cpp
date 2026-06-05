// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
//
// TLS capability test: prove the REST client performs a REAL, verified HTTPS
// request.
//
// One of the three cross-port "every SDK does verified HTTPS + WSS" quadrants
// (template: signalwire-go b6b2b6d). It points a real RestClient at the shared
// mock_signalwire running in --tls mode (https://, backed by the porting-sdk
// self-signed test CA) and performs a GET against a spec-backed endpoint,
// asserting a real JSON response + the mock's journal entry.
//
// Two CA-trust paths are exercised, both with verification ENABLED:
//   1. HttpClient::set_ca_cert_path()  — the explicit per-client hook
//      (C++-only ergonomic addition; verified directly here).
//   2. SSL_CERT_FILE env var           — the cross-port idiom RestClient honors
//      automatically (cpp-httplib's SSLClient consults it).
// A negative control points an HttpClient at a bogus CA and asserts the GET
// fails, proving real certificate verification.

#include "tls_mocktest.hpp"
#include "signalwire/rest/rest_client.hpp"
#include "signalwire/rest/http_client.hpp"

#include <cstdlib>

// NOTE: these *_tls test files are #included into the single test_main.cpp
// translation unit, so a file-scope `using namespace` would leak/collide with
// other tests. In particular `RestClient` is ambiguous in this TU (the class
// signalwire::rest::RestClient vs the top-level factory function
// signalwire::RestClient in signalwire.hpp), so it is fully qualified below.
namespace tt = signalwire::tlstest;
using signalwire::rest::HttpClient;
using nlohmann::json;

TEST(tls_rest_client_https_get) {
    if (tt::ca_cert_path().empty() || !tt::rest_tls_available()) {
        std::cerr << "(skipped: mock_signalwire --tls not reachable on "
                  << tt::rest_tls_base_url() << ") ";
        return true;
    }

    const std::string base = tt::rest_tls_base_url();   // https://127.0.0.1:<port>

    // ---- Path 1: explicit set_ca_cert_path() on a directly-built HttpClient.
    // Clear SSL_CERT_FILE first so this proves the SETTER (not the env) carries
    // the trust.
    ::unsetenv("SSL_CERT_FILE");
    tt::rest_journal_reset();
    {
        HttpClient http(base, "test_proj", "test_tok");
        http.set_ca_cert_path(tt::ca_cert_path());
        json body = http.get("/api/relay/rest/addresses", {{"page_size", "5"}});
        ASSERT_TRUE(body.is_object());
        ASSERT_TRUE(body.contains("data"));         // real JSON over verified TLS
        ASSERT_TRUE(body["data"].is_array());

        json last = tt::rest_journal_last();        // journal read over HTTPS too
        ASSERT_EQ(last.value("method", std::string("")), std::string("GET"));
        ASSERT_EQ(last.value("path", std::string("")),
                  std::string("/api/relay/rest/addresses"));
    }

    // ---- Path 2: SSL_CERT_FILE env trust via the user-facing RestClient.
    tt::trust_test_ca();                            // SSL_CERT_FILE -> test CA
    tt::rest_journal_reset();
    {
        signalwire::rest::RestClient client =
            signalwire::rest::RestClient::with_base_url(base, "test_proj", "test_tok");
        json body = client.addresses().list({{"page_size", "5"}});
        ASSERT_TRUE(body.is_object());
        ASSERT_TRUE(body.contains("data"));
        ASSERT_TRUE(body["data"].is_array());

        json last = tt::rest_journal_last();
        ASSERT_EQ(last.value("path", std::string("")),
                  std::string("/api/relay/rest/addresses"));
    }

    // ---- Negative control: a bogus CA must make verification fail. cpp-httplib
    // raises SignalWireRestError (status 0, "Connection failed") when the TLS
    // handshake can't verify the server cert.
    {
        ::unsetenv("SSL_CERT_FILE");
        HttpClient http(base, "test_proj", "test_tok");
        http.set_ca_cert_path("/dev/null");          // empty/invalid trust store
        ASSERT_THROWS((void)http.get("/api/relay/rest/addresses"));
        tt::trust_test_ca();                          // restore trust
    }
    return true;
}
