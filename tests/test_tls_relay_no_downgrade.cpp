// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
//
// TLS security test (#90, the silent-downgrade shape): setting
// SIGNALWIRE_RELAY_CA_FILE is an explicit request to VERIFY the RELAY peer
// against that CA, which is meaningless without TLS. If the transport then
// resolves to plain `ws://` -- a stale SIGNALWIRE_RELAY_SCHEME, a harness
// export leaking out of a test run, an operator who changed one setting and not
// the other -- the caller asked for encryption, gets NONE, and is never told.
//
// The RELAY client must REFUSE that combination and name the setting that would
// otherwise have been silently ignored, rather than completing a plaintext
// session. Same guard rust already ships (signalwire-rust
// src/relay/client.rs: "NO SILENT DOWNGRADE").
//
// Behavioral, against the real plain-ws mock: with the CA var set and the
// scheme forced to ws, connect() must fail; with the CA var UNSET the very same
// plaintext connect must still SUCCEED (the negative control that keeps this
// test from passing merely because the mock is unreachable, and that pins the
// guard to the CA-var condition rather than to plaintext in general).

#include <cstdlib>
#include <string>

#include "relay_mocktest.hpp"
#include "signalwire/relay/client.hpp"

// #included into the single test_main.cpp TU -> no file-scope `using
// namespace`; targeted declarations only.
namespace rmt = signalwire::relay::mocktest;
using signalwire::relay::RelayClient;
using signalwire::relay::RelayConfig;

TEST(tls_relay_ca_file_refuses_plaintext_downgrade) {
  try {
    (void)rmt::ensure_server();
  } catch (const std::exception& e) {
    std::cerr << "(skipped: mock_relay not reachable: " << e.what() << ") ";
    return true;
  }

  // Snapshot + restore the two globals this case mutates, so it cannot leak
  // into any other test (the runner puts env-mutating cases in the serial
  // batch, but a leaked CA var would still poison later plaintext cases).
  const char* prev_ca_raw = std::getenv("SIGNALWIRE_RELAY_CA_FILE");
  const std::string prev_ca = (prev_ca_raw != nullptr) ? prev_ca_raw : std::string();
  const bool had_ca = (prev_ca_raw != nullptr);

  rmt::force_ws_scheme();  // SIGNALWIRE_RELAY_SCHEME=ws -> plain transport

  auto make_cfg = []() {
    RelayConfig cfg;
    cfg.project = "test_proj";
    cfg.token = "test_tok";
    cfg.host = "127.0.0.1";
    cfg.port = rmt::resolve_ws_port();
    cfg.contexts = {"default"};
    return cfg;
  };

  // (1) CONTROL: plaintext with NO CA request must still connect. This proves
  //     the mock is up and the plain path works, so the assertion below can
  //     only fail for the reason it is testing.
  ::unsetenv("SIGNALWIRE_RELAY_CA_FILE");
  {
    RelayClient control(make_cfg());
    bool ok = control.connect();
    ASSERT_TRUE(ok);
    ASSERT_TRUE(control.is_connected());
    control.disconnect();
  }

  // (2) THE GUARD: with SIGNALWIRE_RELAY_CA_FILE set, the identical plaintext
  //     connect must be REFUSED rather than silently completing in the clear.
  //     The CA path need not exist -- the refusal is about the REQUEST for
  //     verification, which a plaintext transport can never honour.
  ::setenv("SIGNALWIRE_RELAY_CA_FILE", "/nonexistent/ca-bundle.pem", 1);
  {
    RelayClient guarded(make_cfg());
    bool ok = guarded.connect();
    ASSERT_FALSE(ok);  // must NOT downgrade to plaintext
    ASSERT_FALSE(guarded.is_connected());
  }

  if (had_ca) {
    ::setenv("SIGNALWIRE_RELAY_CA_FILE", prev_ca.c_str(), 1);
  } else {
    ::unsetenv("SIGNALWIRE_RELAY_CA_FILE");
  }
  return true;
}
