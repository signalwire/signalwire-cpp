// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
//
// secure_default_dump.cpp — the C++ port's SECURE-DEFAULT (A1 / PSDK-4a) dump
// program for the cross-port behavioral differ
// (porting-sdk/scripts/diff_port_secure_default.py, corpus
// porting-sdk/scripts/secure_default_corpus.py).
//
// The A1 contract: ``define_tool`` defaults ``secure=true`` fleet-wide. A tool
// defined WITHOUT an explicit ``secure`` MUST require SWAIG token validation,
// and the WIRE manifestation of ``secure`` is NOT a ``"secure": true`` key (that
// is not even a property of the SWML UserSWAIGFunction schema) — it is the
// per-tool ``__token`` appended to the rendered function's ``web_hook_url`` when
// the document is rendered with a ``call_id`` (reference agent_base.py:1040 /
// 1096-1100). A tool defined ``secure=false`` gets NO ``__token``.
//
// For each corpus fixture this program builds a fresh AgentBase, defines the
// tool, renders the SWML with the FIXED corpus call_id (passed as the ``call_id``
// query parameter — the reference reads it from exactly there,
// swml_service.py:807), and reduces to the deterministic pair the differ
// compares against the python golden:
//
//   secure_default_true  — the SDK-recorded ``secure`` flag for the tool.
//   wire_reflects_secure — a ``__token`` is present on the rendered webhook IFF
//                          the tool is secure (secure -> token present;
//                          insecure -> token absent).
//
// The token VALUE is an HMAC over (call_id, tool, expiry, nonce) and varies
// run-to-run, so only its PRESENCE folds into the boolean. That keeps the golden
// deterministic while the behavior producing it is real and unfakeable: this
// program cannot report a token for its default tool without the library
// actually minting one onto the wire.
//
// Protocol: stdout = ONE JSON object mapping fixture id -> classification.
// Nothing else is written to stdout (the library logger is suppressed).
//
// Build: the CMake target `secure_default_dump`; the differ invokes
// `build/secure_default_dump`.

#include <iostream>
#include <map>
#include <nlohmann/json.hpp>
#include <string>

#include "signalwire/agent/agent_base.hpp"
#include "signalwire/logging.hpp"
#include "signalwire/swaig/function_result.hpp"

using json = nlohmann::json;
using signalwire::agent::AgentBase;

namespace {

// Mirror porting-sdk/scripts/secure_default_corpus.py EXACTLY.
const char* kCallId = "call-secure-default-fixture";
const char* kDefaultTool = "sd_default_secure";
const char* kInsecureTool = "sd_explicit_insecure";

// Walk sections.main -> the `ai` verb -> SWAIG.functions and index by name.
// Mirrors the oracle's _find_swaig_functions.
std::map<std::string, json> swaig_functions_by_name(const json& doc) {
  std::map<std::string, json> by_name;
  if (!doc.contains("sections") || !doc["sections"].contains("main")) {
    return by_name;
  }
  const json& main = doc["sections"]["main"];
  if (!main.is_array()) {
    return by_name;
  }
  for (const auto& section : main) {
    if (!section.is_object() || !section.contains("ai")) {
      continue;
    }
    const json& ai = section["ai"];
    if (!ai.is_object() || !ai.contains("SWAIG")) {
      continue;
    }
    const json& swaig = ai["SWAIG"];
    if (!swaig.is_object() || !swaig.contains("functions") || !swaig["functions"].is_array()) {
      continue;
    }
    for (const auto& fn : swaig["functions"]) {
      if (fn.is_object() && fn.contains("function") && fn["function"].is_string()) {
        by_name[fn["function"].get<std::string>()] = fn;
      }
    }
  }
  return by_name;
}

// FixtureAgent exists solely to READ BACK the SDK-recorded ``secure`` flag off
// the protected tool registry rather than assume it. Subclassing is the C++
// idiom for reaching a protected member; it adds NO public library surface (the
// alternative — a new public getter — would be port-invented surface).
class FixtureAgent : public AgentBase {
 public:
  FixtureAgent()
      : AgentBase("secure-default-fixture", "/sd", "0.0.0.0", std::nullopt,
                  std::make_pair(std::string("u"), std::string("p"))) {}

  /// The ``ToolDefinition::secure`` the library recorded for ``name``.
  [[nodiscard]] bool recorded_secure(const std::string& name) const {
    auto it = tools_.find(name);
    return it != tools_.end() && it->second.secure;
  }
};

// True iff a rendered SWAIG function entry's webhook carries the reserved
// ``__token`` query parameter — the wire reflection of ``secure``. Mirrors the
// oracle's _webhook_has_token.
bool webhook_has_token(const std::map<std::string, json>& by_name, const std::string& tool) {
  auto it = by_name.find(tool);
  if (it == by_name.end() || !it->second.contains("web_hook_url") ||
      !it->second["web_hook_url"].is_string()) {
    return false;
  }
  return it->second["web_hook_url"].get<std::string>().find("__token=") != std::string::npos;
}

// Build a fresh agent for one fixture, define the fixture's tool, render with
// the fixed call_id, and reduce to the classification. `explicit_insecure`
// selects the corpus case: false = a DEFAULT define_tool (no explicit secure
// argument, so the library's default applies — this is the case that reds a port
// defaulting secure=false); true = an explicit secure=false define_tool.
json classify(const std::string& tool, bool explicit_insecure) {
  FixtureAgent agent;

  const json params = json::object({{"type", "object"}, {"properties", json::object()}});
  signalwire::swaig::ToolHandler handler = [](const json&, const json&) {
    return signalwire::swaig::FunctionResult("ok");
  };

  if (explicit_insecure) {
    agent.define_tool(tool, "secure-default fixture tool", params, handler, false);
  } else {
    // NO explicit secure argument — the library default is what is under test.
    agent.define_tool(tool, "secure-default fixture tool", params, handler);
  }

  // The SDK-recorded secure flag, read back off the registry (not assumed).
  const bool recorded_secure = agent.recorded_secure(tool);

  // Render with the fixed corpus call_id so a secure tool deterministically
  // mints its per-tool token.
  const std::map<std::string, std::string> query = {{"call_id", kCallId}};
  const json doc = agent.render_swml_for_request(query, json::object(), {});
  const bool token_present = webhook_has_token(swaig_functions_by_name(doc), tool);

  return json::object({
      {"secure_default_true", recorded_secure},
      // A token is present IFF the tool is secure.
      {"wire_reflects_secure", token_present == recorded_secure},
  });
}

}  // namespace

int main() {
  // stdout must carry ONLY the JSON classification; the library logger writes
  // debug/info to stdout, so suppress it.
  signalwire::get_logger().suppress();

  json out = json::object();
  out["define_tool_default_is_secure"] = classify(kDefaultTool, /*explicit_insecure=*/false);
  out["define_tool_explicit_insecure"] = classify(kInsecureTool, /*explicit_insecure=*/true);

  std::cout << out.dump() << std::endl;
  return 0;
}
