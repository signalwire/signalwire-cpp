// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
//
// secure_default_dump.cpp — the C++ port's SECURE-DEFAULT (A1 / PSDK-4a) dump
// program for the cross-port behavioral differ
// (porting-sdk/scripts/diff_port_secure_default.py, corpus
// porting-sdk/scripts/secure_default_corpus.py).
//
// PROTOCOL (the 2026-07-27 differ redesign): this program emits the RENDERED
// WIRE PAYLOAD and makes NO judgement about it. Per corpus fixture:
//
//   {"<fixture id>": {"secure_default_true": bool, "rendered": {<functions[] entry>}}}
//
//   secure_default_true — the SDK-recorded ``ToolDefinition::secure`` flag,
//                         read back off the registry (not assumed).
//   rendered            — that tool's own ``SWAIG.functions[]`` entry, VERBATIM,
//                         with every nondeterministic token VALUE (an HMAC)
//                         replaced by the corpus placeholder ``<TOKEN>``. Every
//                         KEY and key path is preserved exactly, because the
//                         KEYS are the whole contract.
//
// The differ derives the comparable topology {secure_default_true,
// has_own_webhook, token_carrier} from those keys. The PREVIOUS version of this
// program emitted a self-computed ``wire_reflects_secure`` boolean, which made
// the gate vacuous by construction: the differ never saw the wire, so it could
// not see WHICH key a port had classified on, nor that an INSECURE tool was
// being handed its own (unauthenticated) per-tool ``web_hook_url``. A dump still
// emitting that boolean is now REJECTED as a legacy self-classification.
//
// The A1 contract this pins (reference agent_base.py:1040 / 1085-1099):
//   define_tool_default_is_secure -> secure_default_true=true, own web_hook_url
//                                    carrying ?__token=<hmac>.
//   define_tool_explicit_insecure -> secure_default_true=false, and NO per-tool
//                                    web_hook_url key at all (it falls back to
//                                    the shared SWAIG.defaults.web_hook_url).
//
// Both tools are registered on ONE agent and rendered in ONE pass, mirroring the
// oracle in diff_port_secure_default.build_oracle.
//
// Protocol: stdout = ONE JSON object. Nothing else is written to stdout (the
// library logger is suppressed).
//
// Build: the CMake target `secure_default_dump`; the differ invokes
// `build/secure_default_dump`.

#include <cctype>
#include <cstddef>
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
const char* kTokenPlaceholder = "<TOKEN>";

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

// True iff `s` ends with "token", case-insensitively — the differ's
// _TOKENISH_SUFFIX test for a key that carries a security token.
bool ends_with_token(const std::string& s) {
  static const std::string kSuffix = "token";
  if (s.size() < kSuffix.size()) {
    return false;
  }
  const size_t off = s.size() - kSuffix.size();
  for (size_t i = 0; i < kSuffix.size(); ++i) {
    const auto c = static_cast<unsigned char>(s[off + i]);
    if (static_cast<char>(std::tolower(c)) != kSuffix[i]) {
      return false;
    }
  }
  return true;
}

// Replace the VALUE of every token-suffixed query parameter in a URL with the
// placeholder, preserving every key and the parameter order. Mirrors the
// differ's _redact_url_token so its re-application is a fixed point.
std::string redact_url_tokens(const std::string& url) {
  const auto q = url.find('?');
  if (q == std::string::npos) {
    return url;
  }
  std::string out = url.substr(0, q + 1);
  const std::string query = url.substr(q + 1);
  size_t pos = 0;
  bool first = true;
  while (pos <= query.size()) {
    const auto amp = query.find('&', pos);
    const std::string pair =
        query.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
    if (!first) {
      out += '&';
    }
    first = false;
    const auto eq = pair.find('=');
    if (eq != std::string::npos && ends_with_token(pair.substr(0, eq))) {
      out += pair.substr(0, eq + 1);
      out += kTokenPlaceholder;
    } else {
      out += pair;
    }
    if (amp == std::string::npos) {
      break;
    }
    pos = amp + 1;
  }
  return out;
}

// Normalize a rendered functions[] entry: replace every nondeterministic token
// VALUE with the placeholder while preserving every KEY and key path exactly.
// Mirrors the differ's redact_entry.
json redact_entry(const json& entry) {
  json out = json::object();
  if (!entry.is_object()) {
    return out;
  }
  for (auto it = entry.begin(); it != entry.end(); ++it) {
    const std::string& key = it.key();
    const json& value = it.value();
    if (value.is_string() && ends_with_token(key)) {
      out[key] = kTokenPlaceholder;
      continue;
    }
    if (value.is_string()) {
      const std::string s = value.get<std::string>();
      if (s.find("://") != std::string::npos || (!s.empty() && s.front() == '/')) {
        out[key] = redact_url_tokens(s);
        continue;
      }
    }
    out[key] = value;
  }
  return out;
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

// One fixture's dump entry: the SDK-recorded secure flag plus the rendered
// entry with token values redacted. NO classification — the differ does that.
json emit(const std::map<std::string, json>& by_name, const std::string& tool,
          bool recorded_secure) {
  auto it = by_name.find(tool);
  const json entry = it == by_name.end() ? json::object() : it->second;
  return json::object({
      {"secure_default_true", recorded_secure},
      {"rendered", redact_entry(entry)},
  });
}

}  // namespace

int main() {
  // exception-escape guard: main() must not let an exception escape
  // (that is std::terminate, with no message). Report and exit nonzero.
  try {
    // stdout must carry ONLY the JSON payload; the library logger writes
    // debug/info to stdout, so suppress it.
    signalwire::get_logger().suppress();

    FixtureAgent agent;

    const json params = json::object({{"type", "object"}, {"properties", json::object()}});
    signalwire::swaig::ToolHandler handler = [](const json&, const json&) {
      return signalwire::swaig::FunctionResult("ok");
    };

    // Both corpus tools on ONE agent, rendered in ONE pass (mirrors the oracle).
    // NO explicit secure argument — the library default is what is under test.
    agent.define_tool(kDefaultTool, "secure-default fixture tool", params, handler);
    agent.define_tool(kInsecureTool, "secure-default fixture tool", params, handler, false);

    // Render with the fixed corpus call_id so a secure tool deterministically
    // mints its per-tool token (the reference reads call_id from exactly this
    // query parameter, swml_service.py:807).
    const std::map<std::string, std::string> query = {{"call_id", kCallId}};
    const json doc = agent.render_swml_for_request(query, json::object(), {});
    const std::map<std::string, json> by_name = swaig_functions_by_name(doc);

    json out = json::object();
    out["define_tool_default_is_secure"] =
        emit(by_name, kDefaultTool, agent.recorded_secure(kDefaultTool));
    out["define_tool_explicit_insecure"] =
        emit(by_name, kInsecureTool, agent.recorded_secure(kInsecureTool));

    std::cout << out.dump() << '\n';
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "fatal: " << e.what() << "\n";
    return 1;
  }
}
