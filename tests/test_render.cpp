// SWML rendering tests — all 5 phases, edge cases, contexts in SWML

#include "signalwire/agent/agent_base.hpp"

using namespace signalwire::agent;
using json = nlohmann::json;

TEST(render_default_swml_structure) {
  AgentBase agent;
  json swml = agent.render_swml();
  ASSERT_EQ(swml["version"].get<std::string>(), "1.0.0");
  ASSERT_TRUE(swml.contains("sections"));
  ASSERT_TRUE(swml["sections"].contains("main"));
  ASSERT_TRUE(swml["sections"]["main"].is_array());
  return true;
}

TEST(render_default_has_answer_and_ai) {
  AgentBase agent;
  json swml = agent.render_swml();
  auto& main = swml["sections"]["main"];
  ASSERT_TRUE(main.size() >= 2u);
  ASSERT_TRUE(main[0].contains("answer"));
  bool has_ai = false;
  for (const auto& v : main) {
    if (v.contains("ai")) has_ai = true;
  }
  ASSERT_TRUE(has_ai);
  return true;
}

TEST(render_empty_agent_produces_valid_swml) {
  AgentBase agent;
  json swml = agent.render_swml();
  std::string s = swml.dump();
  ASSERT_TRUE(s.find("version") != std::string::npos);
  ASSERT_TRUE(s.find("sections") != std::string::npos);
  ASSERT_TRUE(s.find("main") != std::string::npos);
  return true;
}

TEST(render_contexts_in_ai_prompt) {
  // Contexts belong INSIDE the prompt ($defs/AIPromptText / $defs/AIPromptPom),
  // not at the ai top level: $defs/AIObject is closed over nine keys via
  // `unevaluatedProperties: {"not": {}}` and `contexts` is not among them. This
  // test previously asserted `ai.contexts` — i.e. it pinned a schema-invalid
  // document. Reference: swml_handler.py:191 `prompt_config["contexts"] = ...`.
  AgentBase agent;
  auto& ctx = agent.add_context("default");
  ctx.add_step("greet", "Greet the user");
  json swml = agent.render_swml();
  auto& main = swml["sections"]["main"];
  for (const auto& verb : main) {
    if (verb.contains("ai")) {
      ASSERT_FALSE(verb["ai"].contains("contexts"));
      ASSERT_TRUE(verb["ai"]["prompt"]["contexts"].contains("default"));
      return true;
    }
  }
  ASSERT_TRUE(false);
  return true;
}

TEST(render_multiple_tools_order) {
  AgentBase agent;
  agent.set_auth("u", "p");
  agent.define_tool("z_tool", "Z", json::object(), nullptr);
  agent.define_tool("a_tool", "A", json::object(), nullptr);
  agent.define_tool("m_tool", "M", json::object(), nullptr);

  json swml = agent.render_swml();
  auto& main = swml["sections"]["main"];
  for (const auto& verb : main) {
    if (verb.contains("ai") && verb["ai"].contains("SWAIG")) {
      auto& funcs = verb["ai"]["SWAIG"]["functions"];
      ASSERT_EQ(funcs.size(), 3u);
      ASSERT_EQ(funcs[0]["function"].get<std::string>(), "z_tool");
      ASSERT_EQ(funcs[1]["function"].get<std::string>(), "a_tool");
      ASSERT_EQ(funcs[2]["function"].get<std::string>(), "m_tool");
      return true;
    }
  }
  ASSERT_TRUE(false);
  return true;
}

TEST(render_for_request_with_no_headers) {
  AgentBase agent;
  agent.set_prompt_text("Test");
  json swml = agent.render_swml_for_request({}, json::object(), {});
  ASSERT_TRUE(swml.contains("version"));
  ASSERT_TRUE(swml.contains("sections"));
  return true;
}

TEST(render_all_config_combined) {
  AgentBase agent;
  agent.set_auth("u", "p");
  agent.set_prompt_text("You are helpful");
  agent.set_post_prompt("Summary");
  agent.add_hint("SignalWire");
  agent.set_param("temperature", 0.5);
  agent.add_pronunciation("SW", "SignalWire");
  agent.set_global_data(json::object({{"k", "v"}}));
  agent.add_language({"English", "en-US", "rachel", "", ""});
  agent.enable_debug_events(1);
  agent.add_internal_filler("en-US", {"Hold on..."});
  agent.define_tool("test", "Test tool", json::object(), [](const json&, const json&) {
    return signalwire::swaig::FunctionResult("ok");
  });

  json swml = agent.render_swml();
  auto& main = swml["sections"]["main"];
  bool found_ai = false;
  for (const auto& verb : main) {
    if (verb.contains("ai")) {
      found_ai = true;
      auto& ai = verb["ai"];
      ASSERT_TRUE(ai.contains("prompt"));
      ASSERT_TRUE(ai.contains("post_prompt"));
      ASSERT_TRUE(ai.contains("hints"));
      ASSERT_TRUE(ai.contains("params"));
      ASSERT_TRUE(ai.contains("pronounce"));
      ASSERT_TRUE(ai.contains("global_data"));
      ASSERT_TRUE(ai.contains("languages"));
      // Debug events ride in ai.params, not a top-level ai.debug_events.
      ASSERT_FALSE(ai.contains("debug_events"));
      ASSERT_TRUE(ai["params"].contains("debug_webhook_level"));
      ASSERT_TRUE(ai.contains("SWAIG"));
      // Internal fillers ride at SWAIG.internal_fillers — `ai.fillers` is
      // not a key $defs/AIObject declares, and that object is closed.
      ASSERT_FALSE(ai.contains("fillers"));
      ASSERT_TRUE(ai["SWAIG"].contains("internal_fillers"));
    }
  }
  ASSERT_TRUE(found_ai);
  return true;
}

TEST(render_no_swaig_when_no_tools) {
  AgentBase agent;
  json swml = agent.render_swml();
  auto& main = swml["sections"]["main"];
  for (const auto& verb : main) {
    if (verb.contains("ai")) {
      // SWAIG section should not exist when no tools
      ASSERT_FALSE(verb["ai"].contains("SWAIG"));
      return true;
    }
  }
  ASSERT_TRUE(false);
  return true;
}

TEST(render_datamap_and_webhook_tools_combined) {
  AgentBase agent;
  agent.set_auth("u", "p");
  // Regular webhook tool
  agent.define_tool("webhook_tool", "Webhook", json::object(), [](const json&, const json&) {
    return signalwire::swaig::FunctionResult("ok");
  });
  // DataMap tool
  agent.register_swaig_function(
      json::object({{"function", "datamap_tool"},
                    {"description", "DataMap"},
                    {"data_map", json::object({{"webhooks", json::array()}})}}));

  json swml = agent.render_swml();
  auto& main = swml["sections"]["main"];
  for (const auto& verb : main) {
    if (verb.contains("ai") && verb["ai"].contains("SWAIG")) {
      auto& funcs = verb["ai"]["SWAIG"]["functions"];
      ASSERT_EQ(funcs.size(), 2u);
      return true;
    }
  }
  ASSERT_TRUE(false);
  return true;
}
