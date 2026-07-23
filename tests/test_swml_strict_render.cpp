// test_swml_strict_render.cpp — native port of the Wave-2 P#5 SWML
// STRICT-RENDER corpus (porting-sdk/scripts/strict_render_corpus.py).
//
// The strict-render contract: building an SWML document with a MISSHAPEN
// config, an UNKNOWN verb, or a MISSPELLED/unknown key must THROW — not
// silently drop/accept it. A VALID build must still render.
//
// SWMLService cases exercise swml::Service::add_verb(name, config) with schema
// validation ON. AgentBase cases exercise the contexts / tool-registry surface
// (define_tool + define_contexts -> add_context -> add_step -> set_text /
// set_functions / set_valid_contexts, then ContextBuilder::validate()).

#include <string>
#include <vector>

#include "signalwire/agent/agent_base.hpp"
#include "signalwire/contexts/contexts.hpp"
#include "signalwire/swaig/function_result.hpp"
#include "signalwire/swml/service.hpp"

using json = nlohmann::json;

namespace {

// Configure a Service for strict-render. A default Service already has schema
// validation ON (Python SWMLService default), so this only sets name/route.
// Service is non-copyable, so callers construct in place and pass a reference.
void make_strict(signalwire::swml::Service& svc) { svc.set_name("s").set_route("/s"); }

signalwire::swaig::FunctionResult strict_noop_handler(const json&, const json&) {
  return signalwire::swaig::FunctionResult("ok");
}

// Returns true iff building the given add_verb(name, config) does NOT throw.
bool add_verb_ok(const std::string& name, const json& config) {
  try {
    signalwire::swml::Service svc;
    make_strict(svc);
    svc.add_verb(name, config);
    return true;
  } catch (...) {
    return false;
  }
}

}  // namespace

// ============================================================================
// Verb-level strict render (SWMLService, validation ON) — GAP + baseline
// ============================================================================

TEST(strict_render_unknown_verb_raises) {
  signalwire::swml::Service svc;
  make_strict(svc);
  ASSERT_THROWS(svc.add_verb("foobar", json::object()));
  return true;
}

TEST(strict_render_answer_misspelled_key_raises) {
  signalwire::swml::Service svc;
  make_strict(svc);
  ASSERT_THROWS(svc.add_verb("answer", json{{"maxduration", 5}}));
  return true;
}

TEST(strict_render_answer_unknown_key_raises) {
  signalwire::swml::Service svc;
  make_strict(svc);
  ASSERT_THROWS(svc.add_verb("answer", json{{"wibble", 1}}));
  return true;
}

TEST(strict_render_play_misspelled_key_raises) {
  signalwire::swml::Service svc;
  make_strict(svc);
  ASSERT_THROWS(svc.add_verb("play", json{{"urlz", json::array({"say:hi"})}}));
  return true;
}

TEST(strict_render_play_valid_plus_unknown_key_raises) {
  signalwire::swml::Service svc;
  make_strict(svc);
  ASSERT_THROWS(svc.add_verb("play", json{{"url", "say:hi"}, {"foo", 1}}));
  return true;
}

TEST(strict_render_record_misspelled_key_raises) {
  signalwire::swml::Service svc;
  make_strict(svc);
  ASSERT_THROWS(svc.add_verb("record", json{{"formatt", "wav"}}));
  return true;
}

TEST(strict_render_answer_wrong_type_raises) {
  signalwire::swml::Service svc;
  make_strict(svc);
  ASSERT_THROWS(svc.add_verb("answer", json{{"max_duration", "notanumber"}}));
  return true;
}

// ---- the ai (handler) verb: unknown/misspelled TOP-LEVEL keys only ----

TEST(strict_render_ai_misspelled_top_key_raises) {
  signalwire::swml::Service svc;
  make_strict(svc);
  ASSERT_THROWS(svc.add_verb("ai", json{{"prompt", {{"text", "hi"}}}, {"temperatur", 0.5}}));
  return true;
}

TEST(strict_render_ai_unknown_top_key_raises) {
  signalwire::swml::Service svc;
  make_strict(svc);
  ASSERT_THROWS(svc.add_verb("ai", json{{"prompt", {{"text", "hi"}}}, {"zzz", 1}}));
  return true;
}

TEST(strict_render_ai_missing_prompt_raises) {
  signalwire::swml::Service svc;
  make_strict(svc);
  ASSERT_THROWS(svc.add_verb("ai", json{{"post_prompt", {{"text", "bye"}}}}));
  return true;
}

// ---- good documents must still render (regression guard) ----

TEST(strict_render_answer_ok) {
  ASSERT_TRUE(add_verb_ok("answer", json{{"max_duration", 5}}));
  return true;
}

TEST(strict_render_play_ok) {
  ASSERT_TRUE(add_verb_ok("play", json{{"url", "say:hi"}}));
  return true;
}

TEST(strict_render_ai_ok) {
  ASSERT_TRUE(add_verb_ok("ai", json{{"prompt", {{"text", "hi"}}}}));
  return true;
}

TEST(strict_render_ai_params_open_ok) {
  // ai.params is the DELIBERATE open door — a key inside it is not a
  // misspelling and must render.
  ASSERT_TRUE(add_verb_ok(
      "ai", json{{"prompt", {{"text", "hi"}}}, {"params", {{"some_future_param", 1}}}}));
  return true;
}

// ============================================================================
// Contexts-level strict render (AgentBase; dangling refs)
// ============================================================================

TEST(strict_render_dangling_step_function_raises) {
  signalwire::agent::AgentBase a("a", "/a");
  json params = json{{"type", "object"}, {"properties", json::object()}};
  a.define_tool("order_status", "look up an order", params, strict_noop_handler);
  auto& cb = a.define_contexts();
  auto& st = cb.add_context("default").add_step("help");
  st.set_text("help");
  st.set_functions(std::vector<std::string>{"order_status", "get_datetime"});
  ASSERT_THROWS(cb.validate());
  return true;
}

TEST(strict_render_registered_step_function_ok) {
  signalwire::agent::AgentBase a("a", "/a");
  json params = json{{"type", "object"}, {"properties", json::object()}};
  a.define_tool("order_status", "look up an order", params, strict_noop_handler);
  auto& cb = a.define_contexts();
  auto& st = cb.add_context("default").add_step("help");
  st.set_text("help");
  st.set_functions(std::vector<std::string>{"order_status"});
  try {
    cb.validate();
  } catch (...) {
    return false;
  }
  return true;
}

TEST(strict_render_reserved_native_function_ok) {
  signalwire::agent::AgentBase a("a", "/a");
  auto& cb = a.define_contexts();
  auto& st = cb.add_context("default").add_step("help");
  st.set_text("help");
  st.set_functions(std::vector<std::string>{"next_step", "change_context"});
  try {
    cb.validate();
  } catch (...) {
    return false;
  }
  return true;
}

TEST(strict_render_dangling_valid_context_raises) {
  signalwire::agent::AgentBase a("a", "/a");
  auto& cb = a.define_contexts();
  auto& st = cb.add_context("default").add_step("help");
  st.set_text("help");
  st.set_valid_contexts(std::vector<std::string>{"nowhere"});
  ASSERT_THROWS(cb.validate());
  return true;
}
