// BedrockAgent tests — amazon_bedrock verb transform + public surface
#include "signalwire/agents/bedrock.hpp"

using namespace signalwire::agents;
using json = nlohmann::json;

static json find_verb(const json& swml, const std::string& key) {
  const auto& main = swml["sections"]["main"];
  for (const auto& verb : main) {
    if (verb.contains(key)) {
      return verb[key];
    }
  }
  return json();
}

TEST(bedrock_default_name_and_route) {
  BedrockAgent agent;
  ASSERT_EQ(agent.name(), "bedrock_agent");
  ASSERT_EQ(agent.route(), "/bedrock");
  return true;
}

TEST(bedrock_renders_amazon_bedrock_verb_not_ai) {
  BedrockAgent agent;
  agent.prompt_add_section("Role", "You are a helpful voice agent.");
  json swml = agent.render_swml();
  ASSERT_FALSE(find_verb(swml, "ai").is_object());
  json bedrock = find_verb(swml, "amazon_bedrock");
  ASSERT_TRUE(bedrock.is_object());
  ASSERT_TRUE(bedrock.contains("prompt"));
  return true;
}

TEST(bedrock_prompt_carries_voice_and_inference) {
  BedrockAgent agent("a", "/a", "", "joanna", 0.5, 0.8, 512);
  agent.prompt_add_section("Role", "hi");
  json swml = agent.render_swml();
  json bedrock = find_verb(swml, "amazon_bedrock");
  ASSERT_EQ(bedrock["prompt"]["voice_id"].get<std::string>(), "joanna");
  ASSERT_EQ(bedrock["prompt"]["temperature"].get<double>(), 0.5);
  ASSERT_EQ(bedrock["prompt"]["top_p"].get<double>(), 0.8);
  return true;
}

TEST(bedrock_set_voice_updates_prompt) {
  BedrockAgent agent;
  agent.prompt_add_section("Role", "hi");
  agent.set_voice("stephen");
  json swml = agent.render_swml();
  json bedrock = find_verb(swml, "amazon_bedrock");
  ASSERT_EQ(bedrock["prompt"]["voice_id"].get<std::string>(), "stephen");
  return true;
}

TEST(bedrock_set_inference_params_partial_update) {
  BedrockAgent agent("a", "/a", "", "matthew", 0.7, 0.9, 1024);
  agent.set_inference_params(0.2);  // only temperature; others unchanged
  agent.prompt_add_section("Role", "hi");
  json swml = agent.render_swml();
  json bedrock = find_verb(swml, "amazon_bedrock");
  ASSERT_EQ(bedrock["prompt"]["temperature"].get<double>(), 0.2);
  ASSERT_EQ(bedrock["prompt"]["top_p"].get<double>(), 0.9);
  return true;
}

TEST(bedrock_prompt_filters_text_model_params) {
  // barge_confidence / presence_penalty / frequency_penalty are dropped.
  BedrockAgent agent;
  agent.prompt_add_section("Role", "hi");
  agent.set_prompt_llm_params(json::object({{"barge_confidence", 0.6}}));  // no-op warn path
  json swml = agent.render_swml();
  json bedrock = find_verb(swml, "amazon_bedrock");
  ASSERT_FALSE(bedrock["prompt"].contains("barge_confidence"));
  return true;
}

TEST(bedrock_repr_contains_identity) {
  BedrockAgent agent("bot", "/bot", "", "matthew");
  std::string r = agent.repr();
  ASSERT_TRUE(r.find("BedrockAgent") != std::string::npos);
  ASSERT_TRUE(r.find("bot") != std::string::npos);
  ASSERT_TRUE(r.find("matthew") != std::string::npos);
  return true;
}

TEST(bedrock_set_llm_model_is_noop_warning) {
  BedrockAgent agent;
  agent.set_llm_model("gpt-4");  // logs warning, no throw
  return true;
}

TEST(bedrock_carries_swaig_when_tools_defined) {
  BedrockAgent agent;
  agent.prompt_add_section("Role", "hi");
  agent.define_tool(
      "noop", "does nothing", json::object({{"type", "object"}}),
      [](const json&, const json&) { return signalwire::swaig::FunctionResult("ok"); });
  json swml = agent.render_swml();
  json bedrock = find_verb(swml, "amazon_bedrock");
  ASSERT_TRUE(bedrock.contains("SWAIG"));
  return true;
}

// ============================================================================
// transform_swml is a post-render document REWRITE — it rebuilds the verb
// key-by-key against a fixed six-key allowlist rather than passing the original
// through. That shape drops silently: any key the transform does not know about
// vanishes with no error. The typescript port lost debug_webhook_url/-_level
// exactly this way. These tests pin what must survive and what must not.
// ============================================================================

TEST(bedrock_transform_preserves_debug_webhook_params) {
  // params is copied WHOLESALE (not key-by-key), and $defs/BedrockParams is
  // open, so debug-event wiring survives the rewrite. If this ever regresses to
  // a per-key copy against a fixed list, debug events go unreachable on every
  // Bedrock agent with no error raised.
  BedrockAgent agent;
  agent.prompt_add_section("Role", "You are a helpful voice agent.");
  agent.enable_debug_events(1);
  json swml = agent.render_swml();
  json bedrock = find_verb(swml, "amazon_bedrock");
  ASSERT_TRUE(bedrock.is_object());
  ASSERT_TRUE(bedrock.contains("params"));
  ASSERT_TRUE(bedrock["params"].contains("debug_webhook_url"));
  ASSERT_EQ(bedrock["params"]["debug_webhook_level"].get<int>(), 1);
  return true;
}

TEST(bedrock_transform_drops_only_keys_the_schema_forbids) {
  // $defs/AmazonBedrockObject declares exactly
  // [SWAIG, global_data, params, post_prompt, post_prompt_url, prompt] and is
  // CLOSED (unevaluatedProperties: {"not": {}}). hints/languages/pronounce are
  // valid on `ai` but NOT on `amazon_bedrock`, so dropping them is required by
  // the schema, not an accident of the allowlist.
  BedrockAgent agent;
  agent.prompt_add_section("Role", "You are a helpful voice agent.");
  agent.add_hint("SignalWire");
  agent.add_pronunciation("SW", "SignalWire");
  agent.set_global_data(json::object({{"k", "v"}}));
  json swml = agent.render_swml();
  json bedrock = find_verb(swml, "amazon_bedrock");
  ASSERT_TRUE(bedrock.is_object());
  ASSERT_FALSE(bedrock.contains("hints"));
  ASSERT_FALSE(bedrock.contains("pronounce"));
  ASSERT_FALSE(bedrock.contains("languages"));
  // global_data IS in the closed set, so it must survive.
  ASSERT_EQ(bedrock["global_data"]["k"].get<std::string>(), std::string("v"));
  return true;
}
