// test_swml_validating_path.cpp — ITEM 4 (task #194): every SWML verb the SDK
// emits on a caller's behalf must go through the VALIDATING entry point.
//
// The C++ port has four ways to put a verb into a document, and only ONE of
// them consults the schema:
//
//   VALIDATING   swml::Service::add_verb(verb_name, config)      service.cpp:289
//   RAW          swml::Service::add_verb(section, verb, params)  service.cpp:283
//   RAW          swml::Service::add_verb_to_section(...)         service.cpp:397
//   RAW          swml::Document::add_verb / Section::add_verb    document.hpp:33/73
//
// The 3-arg Service::add_verb SHARES ITS NAME with the validating 2-arg form
// and delegates straight to document_.add_verb_to_section with no schema check
// at all, so `service.add_verb(...)` is validating or not depending purely on
// arity. That naming trap is why `play {"text": ...}` (task #180) shipped in
// five ports: the validating path rejects it, and nothing ever went through the
// validating path.
//
// These tests assert THROUGH the validator rather than against a literal blob,
// so the next wrong key is caught too.

#include <string>
#include <vector>

#include "signalwire/agent/agent_base.hpp"
#include "signalwire/core/swml_builder.hpp"
#include "signalwire/core/swml_renderer.hpp"
#include "signalwire/swml/service.hpp"
#include "signalwire/utils/schema_utils.hpp"

using json = nlohmann::json;

namespace {

// True iff pushing (name, config) through the VALIDATING Service::add_verb
// does NOT throw. This is the proof-by-execution the brief asks for: we do not
// reason about whether a shape survives the schema, we run it.
bool vp_survives_validator(const std::string& name, const json& config) {
  try {
    signalwire::swml::Service svc;
    svc.set_name("s").set_route("/s");
    svc.add_verb(name, config);
    return true;
  } catch (...) {
    return false;
  }
}

// Walk every verb in every section of a rendered SWML document back through the
// validating entry point. A document produced by any SDK-owned emitter must be
// re-constructible on the validating path; if it is not, the emitter shipped a
// shape the schema rejects.
bool vp_document_survives_validator(const json& doc) {
  if (!doc.is_object() || !doc.contains("sections")) {
    return false;
  }
  const json& sections = doc.at("sections");
  for (auto sec = sections.begin(); sec != sections.end(); ++sec) {
    if (!sec.value().is_array()) {
      continue;
    }
    for (const auto& verb : sec.value()) {
      if (!verb.is_object()) {
        return false;
      }
      for (auto it = verb.begin(); it != verb.end(); ++it) {
        if (!vp_survives_validator(it.key(), it.value())) {
          return false;
        }
      }
    }
  }
  return true;
}

signalwire::swaig::FunctionResult vp_noop_handler(const json&, const json&) {
  return signalwire::swaig::FunctionResult("ok");
}

}  // namespace

// ============================================================================
// The naming trap: 3-arg Service::add_verb must validate like the 2-arg form
// ============================================================================

TEST(validating_path_service_three_arg_add_verb_rejects_unknown_verb) {
  // service.add_verb("main", "foobar", {}) previously wrote straight into the
  // document. The caller cannot tell from the name that they lost validation.
  signalwire::swml::Service svc;
  svc.set_name("s").set_route("/s");
  ASSERT_THROWS(svc.add_verb("main", "foobar", json::object()));
  return true;
}

TEST(validating_path_service_three_arg_add_verb_rejects_bad_config) {
  signalwire::swml::Service svc;
  svc.set_name("s").set_route("/s");
  // `play` has no `text` key (task #180) — the schema rejects it.
  ASSERT_THROWS(svc.add_verb("main", "play", json::object({{"text", "hi"}})));
  return true;
}

TEST(validating_path_service_three_arg_add_verb_accepts_valid) {
  signalwire::swml::Service svc;
  svc.set_name("s").set_route("/s");
  try {
    svc.add_verb("main", "play", json::object({{"url", "say:hello"}}));
  } catch (...) {
    return false;
  }
  json doc = svc.render_swml();
  ASSERT_TRUE(doc.at("sections").contains("main"));
  return true;
}

TEST(validating_path_service_add_verb_to_section_rejects_unknown_verb) {
  signalwire::swml::Service svc;
  svc.set_name("s").set_route("/s");
  ASSERT_THROWS(svc.add_verb_to_section("main", "foobar", json::object()));
  return true;
}

TEST(validating_path_service_add_verb_to_section_rejects_bad_config) {
  signalwire::swml::Service svc;
  svc.set_name("s").set_route("/s");
  ASSERT_THROWS(svc.add_verb_to_section("main", "play", json::object({{"text", "hi"}})));
  return true;
}

// ============================================================================
// SWMLBuilder — every builder method must land on the validating path
// ============================================================================

TEST(validating_path_builder_answer_survives_validator) {
  signalwire::swml::Service svc;
  svc.set_name("s").set_route("/s");
  signalwire::core::SWMLBuilder b(svc);
  b.answer();
  ASSERT_TRUE(vp_document_survives_validator(svc.render_swml()));
  return true;
}

TEST(validating_path_builder_answer_with_options_survives_validator) {
  signalwire::swml::Service svc;
  svc.set_name("s").set_route("/s");
  signalwire::core::SWMLBuilder b(svc);
  b.answer(3600, std::string("PCMU"));
  ASSERT_TRUE(vp_document_survives_validator(svc.render_swml()));
  return true;
}

TEST(validating_path_builder_hangup_survives_validator) {
  signalwire::swml::Service svc;
  svc.set_name("s").set_route("/s");
  signalwire::core::SWMLBuilder b(svc);
  b.hangup(std::string("busy"));
  ASSERT_TRUE(vp_document_survives_validator(svc.render_swml()));
  return true;
}

TEST(validating_path_builder_hangup_unknown_key_rejected) {
  // The builder now reaches the validator, so a misspelled hangup key throws
  // where it previously rode the raw path into the document.
  //
  // NOTE — a SEPARATE, PRE-EXISTING gap, deliberately not asserted here:
  // ``$defs/Hangup.reason`` is a CLOSED enum of the six values the engine
  // accepts (``relay_apis.c:1105``: hangup,cancel,busy,noAnswer,decline,error),
  // so ``hangup {reason: "done"}`` is invalid on the wire — but this port's
  // ``validate_verb_full`` does not enforce ``enum``/``const`` VALUES, only key
  // names and coarse types. That is a validator-depth gap, not a bypass gap;
  // asserting it here would red on a defect this commit does not claim to fix.
  signalwire::swml::Service svc;
  svc.set_name("s").set_route("/s");
  ASSERT_THROWS(svc.add_verb("hangup", json::object({{"raeson", "busy"}})));
  return true;
}

TEST(validating_path_builder_play_survives_validator) {
  signalwire::swml::Service svc;
  svc.set_name("s").set_route("/s");
  signalwire::core::SWMLBuilder b(svc);
  b.play(std::string("https://example.com/a.wav"));
  ASSERT_TRUE(vp_document_survives_validator(svc.render_swml()));
  return true;
}

TEST(validating_path_builder_say_survives_validator) {
  signalwire::swml::Service svc;
  svc.set_name("s").set_route("/s");
  signalwire::core::SWMLBuilder b(svc);
  b.say("hello there");
  ASSERT_TRUE(vp_document_survives_validator(svc.render_swml()));
  return true;
}

TEST(validating_path_builder_ai_survives_validator) {
  signalwire::swml::Service svc;
  svc.set_name("s").set_route("/s");
  signalwire::core::SWMLBuilder b(svc);
  b.ai(std::string("you are a helpful assistant"));
  ASSERT_TRUE(vp_document_survives_validator(svc.render_swml()));
  return true;
}

// ============================================================================
// SwmlRenderer — the record_call / play / action-verb emissions
// ============================================================================

TEST(validating_path_renderer_record_call_survives_validator) {
  signalwire::swml::Service svc;
  svc.set_name("s").set_route("/s");
  signalwire::core::RenderOptions opts;
  opts.record_call = true;
  std::string s =
      signalwire::core::SwmlRenderer::render_swml(json::object({{"text", "hi"}}), svc, opts);
  ASSERT_TRUE(vp_document_survives_validator(json::parse(s)));
  return true;
}

TEST(validating_path_renderer_function_response_play_survives_validator) {
  signalwire::swml::Service svc;
  svc.set_name("s").set_route("/s");
  std::string s = signalwire::core::SwmlRenderer::render_function_response_swml(
      "spoken response", svc, std::nullopt, "json");
  json doc = json::parse(s);
  ASSERT_TRUE(vp_document_survives_validator(doc));
  return true;
}

TEST(validating_path_renderer_function_response_rejects_invalid_action) {
  // An action verb handed in by a caller now goes through the validator, so a
  // schema-forbidden shape fails loud instead of shipping.
  signalwire::swml::Service svc;
  svc.set_name("s").set_route("/s");
  std::vector<json> actions = {json::object({{"play", json::object({{"text", "nope"}})}})};
  ASSERT_THROWS(
      signalwire::core::SwmlRenderer::render_function_response_swml("", svc, actions, "json"));
  return true;
}

TEST(validating_path_renderer_function_response_valid_action_ok) {
  signalwire::swml::Service svc;
  svc.set_name("s").set_route("/s");
  std::vector<json> actions = {json::object({{"hangup", json::object()}})};
  std::string s =
      signalwire::core::SwmlRenderer::render_function_response_swml("", svc, actions, "json");
  ASSERT_TRUE(vp_document_survives_validator(json::parse(s)));
  return true;
}

// ============================================================================
// AgentBase::render_swml_internal — the document it hand-builds had NO owner
// Service at all, so nothing could ever have validated it.
// ============================================================================

TEST(validating_path_agent_rendered_document_survives_validator) {
  signalwire::agent::AgentBase a("a", "/a");
  a.set_prompt_text("you are a helpful assistant");
  json doc = a.render_swml();
  ASSERT_TRUE(vp_document_survives_validator(doc));
  return true;
}

TEST(validating_path_agent_with_record_call_survives_validator) {
  // record_call is a constructor argument, not a setter.
  signalwire::agent::AgentBase a("a", "/a", "0.0.0.0", std::nullopt, std::nullopt, true, 3600,
                                 /*auto_answer=*/true, /*record_call=*/true);
  a.set_prompt_text("hello");
  json doc = a.render_swml();
  ASSERT_TRUE(vp_document_survives_validator(doc));
  return true;
}

TEST(validating_path_agent_rejects_invalid_extra_verb) {
  // A caller-supplied pre-answer verb is now validated at render time rather
  // than being copied verbatim into the document.
  signalwire::agent::AgentBase a("a", "/a");
  a.set_prompt_text("hello");
  a.add_pre_answer_verb("play", json::object({{"text", "nope"}}));
  ASSERT_THROWS(a.render_swml());
  return true;
}

TEST(validating_path_agent_accepts_valid_extra_verb) {
  signalwire::agent::AgentBase a("a", "/a");
  a.set_prompt_text("hello");
  a.add_pre_answer_verb("play", json::object({{"url", "say:one moment"}}));
  json doc = a.render_swml();
  ASSERT_TRUE(vp_document_survives_validator(doc));
  return true;
}
