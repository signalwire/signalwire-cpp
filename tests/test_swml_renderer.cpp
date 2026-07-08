// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
//
// Behavioral tests for core::SwmlRenderer.
// Python parity: signalwire.core.swml_renderer.SwmlRenderer.

#include "signalwire/core/swml_renderer.hpp"
#include "signalwire/swml/service.hpp"

using namespace signalwire::core;
using json = nlohmann::json;

namespace {
json first_main_verb(const json& doc, const std::string& verb_name) {
  for (const auto& v : doc["sections"]["main"]) {
    if (v.contains(verb_name)) {
      return v[verb_name];
    }
  }
  return json(nullptr);
}
}  // namespace

TEST(renderer_basic_ai_text) {
  signalwire::swml::Service svc;
  std::string s = SwmlRenderer::render_swml("Be nice", svc);
  json doc = json::parse(s);
  json ai = first_main_verb(doc, "ai");
  ASSERT_EQ(ai["prompt"]["text"].get<std::string>(), std::string("Be nice"));
  return true;
}

TEST(renderer_add_answer) {
  signalwire::swml::Service svc;
  RenderOptions opts;
  opts.add_answer = true;
  std::string s = SwmlRenderer::render_swml("Hi", svc, opts);
  json doc = json::parse(s);
  ASSERT_TRUE(first_main_verb(doc, "answer").is_object());
  return true;
}

TEST(renderer_record_call) {
  signalwire::swml::Service svc;
  RenderOptions opts;
  opts.record_call = true;
  opts.record_format = "wav";
  opts.record_stereo = false;
  std::string s = SwmlRenderer::render_swml("Hi", svc, opts);
  json rc = first_main_verb(json::parse(s), "record_call");
  ASSERT_EQ(rc["format"].get<std::string>(), std::string("wav"));
  ASSERT_EQ(rc["stereo"].get<bool>(), false);
  return true;
}

TEST(renderer_startup_and_hangup_hooks) {
  signalwire::swml::Service svc;
  RenderOptions opts;
  opts.startup_hook_url = "https://x/start";
  opts.hangup_hook_url = "https://x/end";
  std::string s = SwmlRenderer::render_swml("Hi", svc, opts);
  json ai = first_main_verb(json::parse(s), "ai");
  const auto& fns = ai["SWAIG"]["functions"];
  ASSERT_EQ(fns.size(), static_cast<size_t>(2));
  ASSERT_EQ(fns[0]["function"].get<std::string>(), std::string("startup_hook"));
  ASSERT_EQ(fns[1]["function"].get<std::string>(), std::string("hangup_hook"));
  return true;
}

TEST(renderer_dedupes_hook_functions_from_caller_list) {
  signalwire::swml::Service svc;
  RenderOptions opts;
  opts.startup_hook_url = "https://x/start";
  std::vector<json> caller = {json::object({{"function", "startup_hook"}, {"web_hook_url", "dup"}}),
                              json::object({{"function", "my_tool"}, {"description", "d"}})};
  opts.swaig_functions = caller;
  std::string s = SwmlRenderer::render_swml("Hi", svc, opts);
  json ai = first_main_verb(json::parse(s), "ai");
  const auto& fns = ai["SWAIG"]["functions"];
  // startup_hook (from url) + my_tool; the duplicate startup_hook is skipped.
  ASSERT_EQ(fns.size(), static_cast<size_t>(2));
  ASSERT_EQ(fns[0]["function"].get<std::string>(), std::string("startup_hook"));
  ASSERT_EQ(fns[1]["function"].get<std::string>(), std::string("my_tool"));
  return true;
}

TEST(renderer_default_webhook_url_adds_defaults) {
  signalwire::swml::Service svc;
  RenderOptions opts;
  opts.default_webhook_url = "https://x/swaig";
  std::string s = SwmlRenderer::render_swml("Hi", svc, opts);
  json ai = first_main_verb(json::parse(s), "ai");
  ASSERT_EQ(ai["SWAIG"]["defaults"]["web_hook_url"].get<std::string>(),
            std::string("https://x/swaig"));
  return true;
}

TEST(renderer_no_swaig_when_no_functions) {
  signalwire::swml::Service svc;
  std::string s = SwmlRenderer::render_swml("Hi", svc);
  json ai = first_main_verb(json::parse(s), "ai");
  ASSERT_FALSE(ai.contains("SWAIG"));
  return true;
}

TEST(renderer_prompt_is_pom) {
  signalwire::swml::Service svc;
  RenderOptions opts;
  opts.prompt_is_pom = true;
  json pom = json::array({{{"title", "Role"}, {"body", "Agent"}}});
  std::string s = SwmlRenderer::render_swml(pom, svc, opts);
  json ai = first_main_verb(json::parse(s), "ai");
  ASSERT_TRUE(ai["prompt"].contains("pom"));
  return true;
}

TEST(renderer_params_merged) {
  signalwire::swml::Service svc;
  RenderOptions opts;
  opts.params = json::object({{"temperature", 0.3}});
  std::string s = SwmlRenderer::render_swml("Hi", svc, opts);
  json ai = first_main_verb(json::parse(s), "ai");
  ASSERT_EQ(ai["temperature"].get<double>(), 0.3);
  return true;
}

// ---- render_function_response_swml ----

TEST(renderer_function_response_play_text) {
  signalwire::swml::Service svc;
  std::string s = SwmlRenderer::render_function_response_swml("All done", svc);
  json p = first_main_verb(json::parse(s), "play");
  ASSERT_EQ(p["text"].get<std::string>(), std::string("All done"));
  return true;
}

TEST(renderer_function_response_actions) {
  signalwire::swml::Service svc;
  std::vector<json> actions = {json::object({{"hangup", json::object()}})};
  std::string s = SwmlRenderer::render_function_response_swml(
      "Bye", svc, std::optional<std::vector<json>>(actions));
  json doc = json::parse(s);
  ASSERT_TRUE(first_main_verb(doc, "play").is_object());
  ASSERT_TRUE(first_main_verb(doc, "hangup").is_object());
  return true;
}

TEST(renderer_function_response_empty_text_no_play) {
  signalwire::swml::Service svc;
  std::string s = SwmlRenderer::render_function_response_swml("", svc);
  json doc = json::parse(s);
  ASSERT_TRUE(first_main_verb(doc, "play").is_null());
  return true;
}

TEST(renderer_yaml_format) {
  signalwire::swml::Service svc;
  RenderOptions opts;
  opts.format = "yaml";
  std::string s = SwmlRenderer::render_swml("Hi", svc, opts);
  // YAML output is not JSON-parseable and contains the version key as text.
  ASSERT_TRUE(s.find("version") != std::string::npos);
  ASSERT_TRUE(s.find('{') == std::string::npos || s.find("sections") != std::string::npos);
  return true;
}
