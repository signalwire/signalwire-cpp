// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
//
// Behavioral tests for core::SWMLBuilder.
// Python parity: signalwire.core.swml_builder.SWMLBuilder.

#include "signalwire/core/swml_builder.hpp"
#include "signalwire/swml/service.hpp"

using namespace signalwire::core;
using json = nlohmann::json;

namespace {
// Find the first verb object in main that has `verb_name`.
json find_main_verb(const json& doc, const std::string& verb_name) {
  for (const auto& v : doc["sections"]["main"]) {
    if (v.contains(verb_name)) {
      return v[verb_name];
    }
  }
  return json(nullptr);
}
}  // namespace

TEST(swml_builder_answer_and_hangup) {
  signalwire::swml::Service svc;
  SWMLBuilder b(svc);
  b.reset().answer(std::optional<int>(3600)).hangup(std::optional<std::string>("busy"));
  json doc = b.build();
  ASSERT_EQ(find_main_verb(doc, "answer")["max_duration"].get<int>(), 3600);
  ASSERT_EQ(find_main_verb(doc, "hangup")["reason"].get<std::string>(), std::string("busy"));
  return true;
}

TEST(swml_builder_answer_empty_config) {
  signalwire::swml::Service svc;
  SWMLBuilder b(svc);
  b.reset().answer();
  json doc = b.build();
  json a = find_main_verb(doc, "answer");
  ASSERT_TRUE(a.is_object());
  ASSERT_TRUE(a.empty());
  return true;
}

TEST(swml_builder_ai_wraps_prompt_text) {
  signalwire::swml::Service svc;
  SWMLBuilder b(svc);
  b.reset().ai(std::optional<std::string>("Be helpful"));
  json ai = find_main_verb(b.build(), "ai");
  ASSERT_EQ(ai["prompt"]["text"].get<std::string>(), std::string("Be helpful"));
  return true;
}

TEST(swml_builder_ai_pom_and_kwargs) {
  signalwire::swml::Service svc;
  SWMLBuilder b(svc);
  json pom = json::array({{{"title", "Role"}}});
  json kwargs = {{"temperature", 0.7}};
  b.reset().ai(std::nullopt, std::optional<json>(pom), std::nullopt, std::nullopt, std::nullopt,
               kwargs);
  json ai = find_main_verb(b.build(), "ai");
  ASSERT_TRUE(ai["prompt"].contains("pom"));
  // kwargs merged at top level
  ASSERT_EQ(ai["temperature"].get<double>(), 0.7);
  return true;
}

TEST(swml_builder_play_requires_url_or_urls) {
  signalwire::swml::Service svc;
  SWMLBuilder b(svc);
  b.reset();
  ASSERT_THROWS(b.play());
  return true;
}

TEST(swml_builder_play_url) {
  signalwire::swml::Service svc;
  SWMLBuilder b(svc);
  b.reset().play(std::optional<std::string>("https://a/x.mp3"), std::nullopt,
                 std::optional<double>(5.0));
  json p = find_main_verb(b.build(), "play");
  ASSERT_EQ(p["url"].get<std::string>(), std::string("https://a/x.mp3"));
  ASSERT_EQ(p["volume"].get<double>(), 5.0);
  return true;
}

TEST(swml_builder_say_prefixes) {
  signalwire::swml::Service svc;
  SWMLBuilder b(svc);
  b.reset().say("Hello there", std::optional<std::string>("en-US-voice"));
  json p = find_main_verb(b.build(), "play");
  ASSERT_EQ(p["url"].get<std::string>(), std::string("say:Hello there"));
  ASSERT_EQ(p["say_voice"].get<std::string>(), std::string("en-US-voice"));
  return true;
}

TEST(swml_builder_reset_clears) {
  signalwire::swml::Service svc;
  SWMLBuilder b(svc);
  b.answer().hangup();
  b.reset();
  json doc = b.build();
  ASSERT_TRUE(doc["sections"]["main"].empty());
  return true;
}

TEST(swml_builder_add_section) {
  signalwire::swml::Service svc;
  SWMLBuilder b(svc);
  b.reset().add_section("greeting");
  json doc = b.build();
  ASSERT_TRUE(doc["sections"].contains("greeting"));
  return true;
}

TEST(swml_builder_render_is_json_string) {
  signalwire::swml::Service svc;
  SWMLBuilder b(svc);
  b.reset().answer();
  std::string s = b.render();
  json parsed = json::parse(s);
  ASSERT_TRUE(parsed.contains("sections"));
  return true;
}

TEST(swml_builder_fluent_chaining_returns_self) {
  signalwire::swml::Service svc;
  SWMLBuilder b(svc);
  SWMLBuilder& r = b.reset().answer().say("hi").hangup();
  ASSERT_TRUE(&r == &b);
  return true;
}
