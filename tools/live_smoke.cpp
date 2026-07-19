// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
//
// live_smoke.cpp — 6.5 real-server smoke: exercise the four load-bearing paths
// against the REAL SignalWire platform (NOT the mock). Opt-in via
// SWSDK_LIVE_TESTS=1 and gated on real credentials; it SKIPS cleanly (exit 0)
// when either is absent, so the nightly workflow is a no-op without secrets.
//
//   1. auth         — construct a REST client from env creds
//   2. REST read    — one real list call (phone_numbers search)
//   3. SWML render  — render a trivial agent's SWML document (no network)
//   4. RELAY connect — open + close a RELAY WebSocket session
//
// Env:
//   SWSDK_LIVE_TESTS=1                             opt in (else skip)
//   SIGNALWIRE_PROJECT_ID / _API_TOKEN / _SPACE    real creds (else skip)
//
// Mirrors signalwire-ruby scripts/live_smoke.rb (the fleet 6.5 driver shape).

#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>

#include "signalwire/agent/agent_base.hpp"
#include "signalwire/relay/client.hpp"
#include "signalwire/rest/rest_client.hpp"

namespace {

int skip(const std::string& reason) {
  std::cerr << "live_smoke: SKIP — " << reason << "\n";
  return 0;
}

bool env_set(const char* key) {
  const char* v = std::getenv(key);
  return v != nullptr && *v != '\0';
}

}  // namespace

int main() {
  const char* opt_in = std::getenv("SWSDK_LIVE_TESTS");
  if (opt_in == nullptr || std::string(opt_in) != "1") {
    return skip("SWSDK_LIVE_TESTS not set");
  }
  if (!env_set("SIGNALWIRE_PROJECT_ID") || !env_set("SIGNALWIRE_API_TOKEN") ||
      !env_set("SIGNALWIRE_SPACE")) {
    return skip("real credentials absent");
  }

  // 1. auth + 2. REST read — construct the client and make one real GET.
  try {
    auto rest = signalwire::rest::RestClient::from_env();
    std::cerr << "live_smoke: [1/4] REST client constructed\n";
    auto result = rest.phone_numbers().search({{"areacode", "212"}});
    std::size_t rows = 0;
    if (result.contains("data") && result["data"].is_array()) {
      rows = result["data"].size();
    }
    std::cerr << "live_smoke: [2/4] REST read ok (" << rows << " rows)\n";
  } catch (const std::exception& e) {
    std::cerr << "live_smoke: FAIL — REST read: " << e.what() << "\n";
    return 1;
  }

  // 3. SWML render — no network; proves document generation end to end.
  try {
    signalwire::agent::AgentBase agent("live-smoke", "/agent");
    agent.prompt_add_section("Role", "You are a smoke test.");
    auto swml = agent.render_swml();
    if (!swml.contains("version")) {
      std::cerr << "live_smoke: FAIL — SWML render produced no version\n";
      return 1;
    }
    std::cerr << "live_smoke: [3/4] SWML render ok\n";
  } catch (const std::exception& e) {
    std::cerr << "live_smoke: FAIL — SWML render: " << e.what() << "\n";
    return 1;
  }

  // 4. RELAY connect — open + close a WS session against the real platform.
  try {
    auto relay = signalwire::relay::RelayClient::from_env();
    if (!relay.connect()) {
      std::cerr << "live_smoke: FAIL — RELAY connect returned false\n";
      return 1;
    }
    relay.disconnect();
    std::cerr << "live_smoke: [4/4] RELAY connect ok\n";
  } catch (const std::exception& e) {
    std::cerr << "live_smoke: FAIL — RELAY connect: " << e.what() << "\n";
    return 1;
  }

  std::cout << "live_smoke: PASS (auth + REST read + SWML render + RELAY connect)\n";
  return 0;
}
