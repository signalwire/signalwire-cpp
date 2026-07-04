// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
//
// Behavioral tests for core::{SWMLVerbHandler, AIVerbHandler, VerbHandlerRegistry}.
// Python parity: signalwire.core.swml_handler.

#include "signalwire/core/swml_handler.hpp"

using namespace signalwire::core;
using json = nlohmann::json;

// ---- Registry ----

TEST(swml_handler_registry_has_ai_by_default) {
  VerbHandlerRegistry reg;
  ASSERT_TRUE(reg.has_handler("ai"));
  ASSERT_FALSE(reg.has_handler("nope"));
  auto h = reg.get_handler("ai");
  ASSERT_TRUE(h != nullptr);
  ASSERT_EQ(h->get_verb_name(), std::string("ai"));
  ASSERT_TRUE(reg.get_handler("nope") == nullptr);
  return true;
}

namespace {
class DummyHandler : public SWMLVerbHandler {
 public:
  std::string get_verb_name() const override { return "dummy"; }
  VerbValidationResult validate_config(const json&) const override { return {true, {}}; }
  json build_config(const json&) const override { return json::object({{"ok", true}}); }
};
}  // namespace

TEST(swml_handler_register_custom) {
  VerbHandlerRegistry reg;
  ASSERT_FALSE(reg.has_handler("dummy"));
  reg.register_handler(std::make_shared<DummyHandler>());
  ASSERT_TRUE(reg.has_handler("dummy"));
  ASSERT_EQ(reg.get_handler("dummy")->build_config()["ok"].get<bool>(), true);
  return true;
}

// ---- AIVerbHandler::validate_config ----

TEST(ai_verb_validate_ok_text) {
  AIVerbHandler h;
  json cfg = {{"prompt", {{"text", "hello"}}}};
  auto r = h.validate_config(cfg);
  ASSERT_TRUE(r.valid);
  ASSERT_TRUE(r.errors.empty());
  return true;
}

TEST(ai_verb_validate_missing_prompt) {
  AIVerbHandler h;
  auto r = h.validate_config(json::object());
  ASSERT_FALSE(r.valid);
  ASSERT_EQ(r.errors.size(), static_cast<size_t>(1));
  ASSERT_EQ(r.errors[0], std::string("Missing required field 'prompt'"));
  return true;
}

TEST(ai_verb_validate_prompt_not_object) {
  AIVerbHandler h;
  json cfg = {{"prompt", "bare string"}};
  auto r = h.validate_config(cfg);
  ASSERT_FALSE(r.valid);
  ASSERT_EQ(r.errors[0], std::string("'prompt' must be an object"));
  return true;
}

TEST(ai_verb_validate_text_and_pom_mutually_exclusive) {
  AIVerbHandler h;
  json cfg = {{"prompt", {{"text", "hi"}, {"pom", json::array()}}}};
  auto r = h.validate_config(cfg);
  ASSERT_FALSE(r.valid);
  return true;
}

TEST(ai_verb_validate_swaig_must_be_object) {
  AIVerbHandler h;
  json cfg = {{"prompt", {{"text", "hi"}}}, {"SWAIG", "nope"}};
  auto r = h.validate_config(cfg);
  ASSERT_FALSE(r.valid);
  return true;
}

// ---- AIVerbHandler::build_config (typed) ----

TEST(ai_verb_build_config_text) {
  AIVerbHandler h;
  json cfg = h.build_config(std::optional<std::string>("Talk"), std::nullopt, std::nullopt,
                            std::nullopt, std::nullopt, std::nullopt);
  ASSERT_EQ(cfg["prompt"]["text"].get<std::string>(), std::string("Talk"));
  ASSERT_TRUE(cfg.contains("params"));
  ASSERT_TRUE(cfg["params"].is_object());
  return true;
}

TEST(ai_verb_build_config_requires_one_base) {
  AIVerbHandler h;
  ASSERT_THROWS(static_cast<void>(h.build_config(std::nullopt, std::nullopt, std::nullopt,
                                                 std::nullopt, std::nullopt, std::nullopt)));
  // both -> also throws
  ASSERT_THROWS(static_cast<void>(h.build_config(std::optional<std::string>("x"),
                                                 std::optional<json>(json::array()), std::nullopt,
                                                 std::nullopt, std::nullopt, std::nullopt)));
  return true;
}

TEST(ai_verb_build_config_post_prompt_and_swaig) {
  AIVerbHandler h;
  json swaig = {{"functions", json::array()}};
  json cfg = h.build_config(std::optional<std::string>("Talk"), std::nullopt, std::nullopt,
                            std::optional<std::string>("Summarize"),
                            std::optional<std::string>("https://x/pp"), std::optional<json>(swaig));
  ASSERT_EQ(cfg["post_prompt"]["text"].get<std::string>(), std::string("Summarize"));
  ASSERT_EQ(cfg["post_prompt_url"].get<std::string>(), std::string("https://x/pp"));
  ASSERT_TRUE(cfg.contains("SWAIG"));
  return true;
}

TEST(ai_verb_build_config_kwargs_top_level_vs_params) {
  AIVerbHandler h;
  json kwargs = {{"languages", json::array({"en"})}, {"temperature", 0.5}};
  json cfg = h.build_config(std::optional<std::string>("Talk"), std::nullopt, std::nullopt,
                            std::nullopt, std::nullopt, std::nullopt, kwargs);
  // languages is a top-level key
  ASSERT_TRUE(cfg.contains("languages"));
  // temperature falls into params
  ASSERT_EQ(cfg["params"]["temperature"].get<double>(), 0.5);
  return true;
}

// ---- AIVerbHandler::build_config (kwargs catch-all form) ----

TEST(ai_verb_build_config_kwargs_form) {
  AIVerbHandler h;
  json kwargs = {{"prompt_text", "Talk"}, {"post_prompt", "Bye"}, {"top_p", 0.9}};
  json cfg = h.build_config(kwargs);
  ASSERT_EQ(cfg["prompt"]["text"].get<std::string>(), std::string("Talk"));
  ASSERT_EQ(cfg["post_prompt"]["text"].get<std::string>(), std::string("Bye"));
  ASSERT_EQ(cfg["params"]["top_p"].get<double>(), 0.9);
  return true;
}
