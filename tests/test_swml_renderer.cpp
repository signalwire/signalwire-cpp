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
  // RenderOptions::params spreads at the ai TOP level (reference:
  // `**(params or {})`), so it must carry a key `$defs/AIObject` declares — the
  // object is closed over nine keys. `temperature` is in neither AIObject nor
  // `$defs/AIParams`; it rode the raw path into a schema-invalid document.
  opts.params = json::object({{"hints", json::array({"acme"})}});
  std::string s = SwmlRenderer::render_swml("Hi", svc, opts);
  json ai = first_main_verb(json::parse(s), "ai");
  ASSERT_EQ(ai["hints"][0].get<std::string>(), std::string("acme"));
  return true;
}

// ---- render_function_response_swml ----

// Spoken text goes through the `say:` URL scheme. The SWML `play` verb has NO
// `text` key: schema.json defines it as oneOf[PlayWithURL, PlayWithURLS] with
// `unevaluatedProperties: {"not": {}}`, so `{"play": {"text": ...}}` is a
// document the schema rejects. The reference fixed exactly this
// (swml_renderer.py: `service.add_verb("play", {"url": f"say:{response_text}"})`)
// and SWMLBuilder::play next door only ever emits url/urls.
//
// Asserts on PARSED keys + value kinds, not a substring of the rendered blob:
// a `blob.find("All done") != npos` check passes for `text`, `url`, and any
// other shape, which is how this class of defect survives elsewhere.
TEST(renderer_function_response_play_uses_say_url_not_text) {
  signalwire::swml::Service svc;
  std::string s = SwmlRenderer::render_function_response_swml("All done", svc);
  json p = first_main_verb(json::parse(s), "play");
  ASSERT_TRUE(p.is_object());
  // The canonical key, carrying the say: scheme.
  ASSERT_TRUE(p.contains("url"));
  ASSERT_TRUE(p["url"].is_string());
  ASSERT_EQ(p["url"].get<std::string>(), std::string("say:All done"));
  // The key the SWML play verb does not have, and that mod_infrastructure's
  // schema rejects.
  ASSERT_FALSE(p.contains("text"));
  // Exactly one key -- nothing else smuggled in alongside.
  ASSERT_EQ(p.size(), static_cast<size_t>(1));
  return true;
}

// A caller-supplied `play` action is passed through verbatim (the reference
// does `service.add_verb("play", action["play"])`), so a caller that already
// built a url/urls config is not rewritten.
TEST(renderer_function_response_play_action_passthrough) {
  signalwire::swml::Service svc;
  std::vector<json> actions = {
      json::object({{"play", json::object({{"url", "https://example.com/a.mp3"}})}})};
  std::string s = SwmlRenderer::render_function_response_swml(
      "Spoken", svc, std::optional<std::vector<json>>(actions));
  json doc = json::parse(s);
  // Two play verbs: the response text (as say:) then the action's URL.
  std::vector<json> plays;
  for (const auto& v : doc["sections"]["main"]) {
    if (v.contains("play")) {
      plays.push_back(v["play"]);
    }
  }
  ASSERT_EQ(plays.size(), static_cast<size_t>(2));
  ASSERT_EQ(plays[0]["url"].get<std::string>(), std::string("say:Spoken"));
  ASSERT_FALSE(plays[0].contains("text"));
  ASSERT_EQ(plays[1]["url"].get<std::string>(), std::string("https://example.com/a.mp3"));
  ASSERT_FALSE(plays[1].contains("text"));
  return true;
}

// The `ai` verb from the renderer must route through SWMLBuilder::ai, so
// prompt/post_prompt reach the wire as OBJECTS. mod_openai's app_config.c does
// `!cJSON_IsObject(prompt)` -> calling.error -> ABORTS THE CALL, so a bare
// string here is fatal on the wire, not cosmetic.
TEST(renderer_ai_prompt_and_post_prompt_are_objects_not_bare_strings) {
  signalwire::swml::Service svc;
  RenderOptions opts;
  opts.post_prompt = "Summarize the call";
  std::string s = SwmlRenderer::render_swml("Be nice", svc, opts);
  json ai = first_main_verb(json::parse(s), "ai");
  ASSERT_TRUE(ai.is_object());

  ASSERT_TRUE(ai.contains("prompt"));
  ASSERT_TRUE(ai["prompt"].is_object());
  ASSERT_FALSE(ai["prompt"].is_string());
  ASSERT_EQ(ai["prompt"]["text"].get<std::string>(), std::string("Be nice"));

  ASSERT_TRUE(ai.contains("post_prompt"));
  ASSERT_TRUE(ai["post_prompt"].is_object());
  ASSERT_FALSE(ai["post_prompt"].is_string());
  ASSERT_EQ(ai["post_prompt"]["text"].get<std::string>(), std::string("Summarize the call"));
  return true;
}

// Same guarantee for the POM prompt shape: an ARRAY under `prompt.pom`, with
// `prompt` itself still an object.
TEST(renderer_ai_pom_prompt_is_object_wrapping_array) {
  signalwire::swml::Service svc;
  RenderOptions opts;
  opts.prompt_is_pom = true;
  json pom = json::array({{{"title", "Role"}, {"body", "Agent"}}});
  std::string s = SwmlRenderer::render_swml(pom, svc, opts);
  json ai = first_main_verb(json::parse(s), "ai");
  ASSERT_TRUE(ai["prompt"].is_object());
  ASSERT_FALSE(ai["prompt"].is_string());
  ASSERT_TRUE(ai["prompt"]["pom"].is_array());
  ASSERT_FALSE(ai["prompt"].contains("text"));
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
