// Tool mixin tests — registration, dispatch, DataMap tools, ordering

#include "signalwire/agent/agent_base.hpp"
#include "signalwire/datamap/datamap.hpp"

using namespace signalwire::agent;
using namespace signalwire::swaig;
using namespace signalwire::datamap;
using json = nlohmann::json;

// ========================================================================
// Tool registration
// ========================================================================

TEST(tool_define_tool_basic) {
  AgentBase agent;
  agent.define_tool(
      "greet", "Say hello", json::object(),
      [](const json&, const json&) -> FunctionResult { return FunctionResult("Hello!"); });
  ASSERT_TRUE(agent.has_tool("greet"));
  return true;
}

TEST(tool_define_tool_with_definition) {
  AgentBase agent;
  ToolDefinition td;
  td.name = "my_tool";
  td.description = "A tool";
  td.parameters = json::object({{"type", "object"}, {"properties", json::object()}});
  td.handler = [](const json&, const json&) { return FunctionResult("ok"); };
  agent.define_tool(td);
  ASSERT_TRUE(agent.has_tool("my_tool"));
  return true;
}

TEST(tool_has_tool_false_for_unknown) {
  AgentBase agent;
  ASSERT_FALSE(agent.has_tool("does_not_exist"));
  return true;
}

TEST(tool_list_tools_empty) {
  AgentBase agent;
  auto tools = agent.list_tools();
  ASSERT_EQ(tools.size(), 0u);
  return true;
}

TEST(tool_list_tools_preserves_order) {
  AgentBase agent;
  agent.define_tool("charlie", "C", json::object(), nullptr);
  agent.define_tool("alpha", "A", json::object(), nullptr);
  agent.define_tool("bravo", "B", json::object(), nullptr);
  auto tools = agent.list_tools();
  ASSERT_EQ(tools.size(), 3u);
  ASSERT_EQ(tools[0], "charlie");
  ASSERT_EQ(tools[1], "alpha");
  ASSERT_EQ(tools[2], "bravo");
  return true;
}

TEST(tool_redefine_does_not_duplicate_order) {
  AgentBase agent;
  agent.define_tool("tool_a", "A", json::object(), nullptr);
  agent.define_tool("tool_b", "B", json::object(), nullptr);
  agent.define_tool("tool_a", "A updated", json::object(), nullptr);
  auto tools = agent.list_tools();
  ASSERT_EQ(tools.size(), 2u);
  ASSERT_EQ(tools[0], "tool_a");
  ASSERT_EQ(tools[1], "tool_b");
  return true;
}

// ========================================================================
// Tool dispatch
// ========================================================================

TEST(tool_dispatch_returns_result) {
  AgentBase agent;
  agent.define_tool("add", "Add numbers", json::object(),
                    [](const json& args, const json&) -> FunctionResult {
                      int a = args.value("a", 0);
                      int b = args.value("b", 0);
                      return FunctionResult("Result: " + std::to_string(a + b));
                    });
  auto result = agent.on_function_call("add", json::object({{"a", 3}, {"b", 4}}), json::object());
  ASSERT_EQ(result.to_json()["response"].get<std::string>(), "Result: 7");
  return true;
}

TEST(tool_dispatch_unknown_function) {
  AgentBase agent;
  auto result = agent.on_function_call("missing", json::object(), json::object());
  auto j = result.to_json();
  ASSERT_TRUE(j["response"].get<std::string>().find("Unknown") != std::string::npos);
  return true;
}

TEST(tool_dispatch_null_handler) {
  AgentBase agent;
  agent.define_tool("null_handler", "No handler", json::object(), nullptr);
  auto result = agent.on_function_call("null_handler", json::object(), json::object());
  auto j = result.to_json();
  ASSERT_TRUE(j["response"].get<std::string>().find("No handler") != std::string::npos);
  return true;
}

TEST(tool_dispatch_with_raw_data) {
  AgentBase agent;
  agent.define_tool("echo_raw", "Echo raw", json::object(),
                    [](const json&, const json& raw) -> FunctionResult {
                      return FunctionResult("call_id=" + raw.value("call_id", "none"));
                    });
  auto result =
      agent.on_function_call("echo_raw", json::object(), json::object({{"call_id", "call-123"}}));
  ASSERT_EQ(result.to_json()["response"].get<std::string>(), "call_id=call-123");
  return true;
}

// ========================================================================
// DataMap registration
// ========================================================================

TEST(tool_register_datamap_function) {
  AgentBase agent;
  agent.set_auth("u", "p");
  auto dm = DataMap("get_weather")
                .purpose("Get weather")
                .parameter("city", "string", "City", true)
                .webhook("GET", "https://api.example.com/weather")
                .output(FunctionResult("Weather: ${response.temp}"))
                .to_swaig_function();
  agent.register_swaig_function(dm);

  json swml = agent.render_swml();
  auto& main = swml["sections"]["main"];
  for (const auto& verb : main) {
    if (verb.contains("ai") && verb["ai"].contains("SWAIG")) {
      auto& funcs = verb["ai"]["SWAIG"]["functions"];
      bool found = false;
      for (const auto& f : funcs) {
        if (f.value("function", "") == "get_weather") {
          found = true;
          ASSERT_TRUE(f.contains("data_map"));
        }
      }
      ASSERT_TRUE(found);
      return true;
    }
  }
  ASSERT_TRUE(false);
  return true;
}

// ========================================================================
// SWAIG function rendering in SWML
// ========================================================================

TEST(tool_swaig_functions_in_swml) {
  AgentBase agent;
  agent.set_auth("u", "p");
  agent.define_tool(
      "search", "Search web",
      json::object({{"type", "object"},
                    {"properties", json::object({{"q", json::object({{"type", "string"}})}})}}),
      [](const json&, const json&) { return FunctionResult("ok"); });

  // Render WITH a call_id: a per-tool web_hook_url is only emitted when the
  // entry carries a token (or SWAIG query params) — reference
  // agent_base.py:1085-1099.
  const std::map<std::string, std::string> query = {{"call_id", "call-abc"}};
  json swml = agent.render_swml_for_request(query, json::object(), {});
  auto& main = swml["sections"]["main"];
  for (const auto& verb : main) {
    if (verb.contains("ai") && verb["ai"].contains("SWAIG")) {
      auto& funcs = verb["ai"]["SWAIG"]["functions"];
      ASSERT_EQ(funcs.size(), 1u);
      ASSERT_EQ(funcs[0]["function"].get<std::string>(), "search");
      ASSERT_TRUE(funcs[0].contains("web_hook_url"));
      return true;
    }
  }
  ASSERT_TRUE(false);
  return true;
}

// Locate the single rendered SWAIG function entry in a rendered SWML document.
static json swaig_only_function(const json& swml) {
  const auto& main = swml["sections"]["main"];
  for (const auto& verb : main) {
    if (verb.contains("ai") && verb["ai"].contains("SWAIG") &&
        verb["ai"]["SWAIG"].contains("functions")) {
      const auto& funcs = verb["ai"]["SWAIG"]["functions"];
      if (funcs.size() == 1u) {
        return funcs[0];
      }
    }
  }
  return json();
}

// A tool defined WITHOUT an explicit secure argument is SECURE (the A1 default),
// and rendering with a call_id puts the per-tool ``__token`` on its webhook —
// the wire manifestation of secure. ``secure`` itself is never emitted as a
// function property (not in the SWML schema, not in the reference).
TEST(tool_secure_tool_in_swml_carries_token) {
  AgentBase agent;
  agent.set_auth("u", "p");
  agent.define_tool("secure_tool", "Secure", json::object(),
                    [](const json&, const json&) { return FunctionResult("ok"); });

  const std::map<std::string, std::string> query = {{"call_id", "call-abc"}};
  json fn = swaig_only_function(agent.render_swml_for_request(query, json::object(), {}));
  ASSERT_FALSE(fn.is_null());
  ASSERT_FALSE(fn.contains("secure"));
  ASSERT_TRUE(fn.contains("web_hook_url"));
  ASSERT_TRUE(fn["web_hook_url"].get<std::string>().find("__token=") != std::string::npos);
  return true;
}

// Locate a rendered SWAIG function entry BY NAME (the multi-tool analog of
// swaig_only_function). Returns a null json when the name is absent.
static json swaig_function_named(const json& swml, const std::string& name) {
  const auto& main = swml["sections"]["main"];
  for (const auto& verb : main) {
    if (verb.contains("ai") && verb["ai"].contains("SWAIG") &&
        verb["ai"]["SWAIG"].contains("functions")) {
      for (const auto& fn : verb["ai"]["SWAIG"]["functions"]) {
        if (fn.contains("function") && fn["function"] == name) {
          return fn;
        }
      }
    }
  }
  return json();
}

// The other direction, and the SECURITY half of the contract: an explicitly
// INSECURE tool gets no token AND NO ``web_hook_url`` KEY AT ALL.
//
// Reference agent_base.py:1085-1099 — external URL wins; else a local URL ONLY
// when a token or SWAIG query params exist; else the key is absent and the tool
// falls back to the shared ``SWAIG.defaults.web_hook_url``. Emitting the local
// URL here would publish an UNAUTHENTICATED, function-specific callback on the
// wire, which is exactly what ``secure=false`` must not do. An empty string, a
// null, or a tokenless URL are the same defect — the KEY must be absent.
TEST(tool_insecure_tool_in_swml_has_no_webhook_key) {
  AgentBase agent;
  agent.set_auth("u", "p");
  agent.define_tool(
      "open_tool", "Insecure", json::object(),
      [](const json&, const json&) { return FunctionResult("ok"); }, false /* secure */);

  const std::map<std::string, std::string> query = {{"call_id", "call-abc"}};
  json fn = swaig_only_function(agent.render_swml_for_request(query, json::object(), {}));
  ASSERT_FALSE(fn.is_null());
  ASSERT_FALSE(fn.contains("web_hook_url"));
  return true;
}

// SWAIG query params are the OTHER arm of the reference's ``elif token or
// agent._swaig_query_params`` guard: with them set, even an insecure tool gets
// its own (still tokenless) local webhook, because the params must reach the
// callback. This pins that the guard is the reference's disjunction and not a
// blanket "insecure => no webhook".
TEST(tool_insecure_tool_with_swaig_query_params_keeps_webhook) {
  AgentBase agent;
  agent.set_auth("u", "p");
  agent.add_swaig_query_param("tenant", "acme");
  agent.define_tool(
      "open_tool", "Insecure", json::object(),
      [](const json&, const json&) { return FunctionResult("ok"); }, false /* secure */);

  const std::map<std::string, std::string> query = {{"call_id", "call-abc"}};
  json fn = swaig_only_function(agent.render_swml_for_request(query, json::object(), {}));
  ASSERT_FALSE(fn.is_null());
  ASSERT_TRUE(fn.contains("web_hook_url"));
  const std::string url = fn["web_hook_url"].get<std::string>();
  ASSERT_TRUE(url.find("tenant=acme") != std::string::npos);
  ASSERT_TRUE(url.find("__token=") == std::string::npos);
  return true;
}

// The SECURE-DEFAULT corpus shape, in-process: one default (secure) tool and one
// secure=false tool on the SAME agent, rendered in ONE pass. The secure entry
// HAS a web_hook_url carrying ``__token``; the insecure entry has NO
// web_hook_url key at all. This is the pair the cross-port
// diff_port_secure_default gate compares.
TEST(tool_secure_and_insecure_tools_render_divergent_webhooks) {
  AgentBase agent;
  agent.set_auth("u", "p");
  agent.define_tool("sd_default_secure", "Secure", json::object(),
                    [](const json&, const json&) { return FunctionResult("ok"); });
  agent.define_tool(
      "sd_explicit_insecure", "Insecure", json::object(),
      [](const json&, const json&) { return FunctionResult("ok"); }, false /* secure */);

  const std::map<std::string, std::string> query = {{"call_id", "call-abc"}};
  const json swml = agent.render_swml_for_request(query, json::object(), {});

  json secure_fn = swaig_function_named(swml, "sd_default_secure");
  ASSERT_FALSE(secure_fn.is_null());
  ASSERT_TRUE(secure_fn.contains("web_hook_url"));
  ASSERT_TRUE(secure_fn["web_hook_url"].get<std::string>().find("__token=") != std::string::npos);

  json insecure_fn = swaig_function_named(swml, "sd_explicit_insecure");
  ASSERT_FALSE(insecure_fn.is_null());
  ASSERT_FALSE(insecure_fn.contains("web_hook_url"));

  // ...and the shared fallback the insecure tool relies on MUST be present.
  // Withholding the per-tool webhook without emitting SWAIG.defaults leaves an
  // insecure tool with NO reachable callback at all — a worse failure than the
  // unauthenticated per-tool callback the guard removes, and one the
  // cross-port SECURE-DEFAULT gate cannot see (it inspects only functions[]).
  // Reference agent_base.py:1108-1113 adds defaults whenever functions exist.
  json swaig;
  for (const auto& verb : swml["sections"]["main"]) {
    if (verb.contains("ai") && verb["ai"].contains("SWAIG")) {
      swaig = verb["ai"]["SWAIG"];
    }
  }
  ASSERT_FALSE(swaig.is_null());
  ASSERT_TRUE(swaig.contains("defaults"));
  ASSERT_TRUE(swaig["defaults"].contains("web_hook_url"));
  const std::string fallback = swaig["defaults"]["web_hook_url"].get<std::string>();
  ASSERT_TRUE(fallback.find("/swaig") != std::string::npos);
  // The shared endpoint is not per-tool, so it carries no per-tool token.
  ASSERT_TRUE(fallback.find("__token=") == std::string::npos);
  return true;
}

// No call_id = no call to scope a token to under C++'s ``if secure && call_id``
// guard, so no token is minted — and with no token and no SWAIG query params the
// reference's ``elif token or _swaig_query_params`` is false on both arms, so no
// ``web_hook_url`` key is emitted either.
//
// NOTE (measured, not inferred; out of scope for this security fix): the python
// reference GENERATES a call_id when the request supplies none
// (agent_base.py ``generated_call_id``), so a secure tool there always mints a
// token and always carries its own webhook. C++ renders bare instead. That is a
// separate divergence in WHEN a call_id exists, not in this webhook-key guard,
// and the SECURE-DEFAULT corpus always passes an explicit call_id so it does not
// exercise it. This test pins C++'s current no-call_id behavior.
TEST(tool_secure_tool_without_call_id_has_no_webhook_key) {
  AgentBase agent;
  agent.set_auth("u", "p");
  agent.define_tool("secure_tool", "Secure", json::object(),
                    [](const json&, const json&) { return FunctionResult("ok"); });

  json fn = swaig_only_function(agent.render_swml());
  ASSERT_FALSE(fn.is_null());
  ASSERT_FALSE(fn.contains("web_hook_url"));
  return true;
}

TEST(tool_function_includes) {
  AgentBase agent;
  agent.set_auth("u", "p");
  agent.add_function_include(json::object({{"url", "https://example.com/functions.json"},
                                           {"functions", json::array({"func1", "func2"})}}));

  json swml = agent.render_swml();
  auto& main = swml["sections"]["main"];
  for (const auto& verb : main) {
    if (verb.contains("ai") && verb["ai"].contains("SWAIG")) {
      ASSERT_TRUE(verb["ai"]["SWAIG"].contains("includes"));
      ASSERT_EQ(verb["ai"]["SWAIG"]["includes"].size(), 1u);
      return true;
    }
  }
  ASSERT_TRUE(false);
  return true;
}

TEST(tool_set_function_includes_replaces) {
  AgentBase agent;
  agent.set_auth("u", "p");
  // set_function_includes REPLACES the prior add_function_include. Use
  // well-formed entries (non-empty string `url` + array `functions`) so the
  // #191 validity filter keeps them; this test isolates replace-vs-merge.
  agent.add_function_include(
      json::object({{"url", "https://a/swaig"}, {"functions", json::array({"a"})}}));
  agent.set_function_includes({
      json::object({{"url", "https://b/swaig"}, {"functions", json::array({"b"})}}),
      json::object({{"url", "https://c/swaig"}, {"functions", json::array({"c"})}}),
  });

  json swml = agent.render_swml();
  auto& main = swml["sections"]["main"];
  for (const auto& verb : main) {
    if (verb.contains("ai") && verb["ai"].contains("SWAIG")) {
      ASSERT_EQ(verb["ai"]["SWAIG"]["includes"].size(), 2u);
      return true;
    }
  }
  ASSERT_TRUE(false);
  return true;
}
