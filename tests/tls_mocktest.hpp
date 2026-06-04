// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
//
// tls_mocktest.hpp -- shared helpers for the three TLS capability tests
// (REST https://, RELAY wss://, and the SDK's own SSLServer). These prove the
// SDK does REAL, certificate-verified TLS against the porting-sdk --tls mocks
// and against its own in-process HTTPS server.
//
// CA trust is wired the cross-port way: SSL_CERT_FILE -> the porting-sdk test
// CA (test_harness/tls/certs/ca.crt). OpenSSL-based clients (cpp-httplib's
// SSLClient, IXWebSocket's OpenSSL backend) honor it, so no verification is
// ever disabled. Each test pairs a positive (verified) assertion with a
// negative control (untrusted CA -> handshake rejected).
//
// The --tls mocks run on the HOST (the build/test container reaches them over
// --network host). Ports default to the non-TLS C++ slot + 1 so a TLS mock can
// run alongside the plain one:
//   REST  https://127.0.0.1:8773         (env MOCK_SIGNALWIRE_TLS_PORT)
//   RELAY wss://127.0.0.1:8783  (control http://127.0.0.1:9783)
//                                        (env MOCK_RELAY_TLS_PORT / _HTTP_PORT)
// When a TLS mock is not reachable the helpers report it via
// tls_mocks_available(false-ish) so the test can skip cleanly rather than fail
// on infra; CI brings the mocks up so the assertions actually run.
#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace signalwire {
namespace tlstest {

using nlohmann::json;

// Absolute path to the shared test CA (test_harness/tls/certs/ca.crt),
// discovered by walking up from the build's source dir. Empty if not found.
std::string ca_cert_path();

// Set SSL_CERT_FILE in this process's environment to ca_cert_path() so every
// subsequent OpenSSL-based client trusts the test CA. Idempotent. Returns the
// path it set (or "" if the CA could not be located).
std::string trust_test_ca();

// ---- REST (mock_signalwire --tls) --------------------------------------
int rest_tls_port();                       // default 8773
std::string rest_tls_base_url();           // https://127.0.0.1:<port>
std::string rest_tls_control_base();       // https://127.0.0.1:<port> (same host:port)
// True iff the https:// REST mock answers /__mock__/health when trusting the
// test CA. Performs a real verified GET.
bool rest_tls_available();
// Reset + read the REST mock journal over its (HTTPS) control plane.
void rest_journal_reset();
json rest_journal_last();                  // throws if empty

// ---- RELAY (mock_relay --tls) ------------------------------------------
int relay_tls_ws_port();                   // default 8783
int relay_tls_http_port();                 // default 9783 (control plane: HTTP)
std::string relay_tls_http_url();          // http://127.0.0.1:<http_port>
bool relay_tls_available();                // control-plane /__mock__/health ok
void relay_journal_reset();
// Inbound (SDK->server) journal frames, optionally filtered by JSON-RPC method.
std::vector<json> relay_journal_recv(const std::string& method = "");

} // namespace tlstest
} // namespace signalwire
