// strict_render_dump.cpp — the C++ port's SWML STRICT-RENDER dump program for
// the cross-port strict-render differ
// (porting-sdk/scripts/diff_port_strict_render.py).
//
// The strict-render contract: building/rendering an SWML document with a
// MISSHAPEN config, an UNKNOWN verb, or a MISSPELLED/unknown key must RAISE
// (throw) — not silently drop or accept it. A VALID build must still render.
//
// For each strict_render_corpus case this program builds the document in C++
// idiom and reports the observed OUTCOME:
//
//     "raised" — the build threw an exception (the contract's teeth)
//     "ok"     — the build completed cleanly
//
// It emits ONE JSON object mapping case-id -> "raised"|"ok" to stdout (JSON
// only). The differ compares each outcome against the python oracle.
//
// The corpus targets two objects:
//   - SWMLService cases exercise swml::Service::add_verb(name, config) on a
//     schema-validation-ON service (add_verb in the python reference).
//   - AgentBase cases exercise the contexts / tool-registry surface:
//     define_tool + define_contexts -> add_context -> add_step -> set_text /
//     set_functions / set_valid_contexts, then ContextBuilder::validate()
//     (the python _ctx_validate via to_dict()).
//
// Build: a CMake target `strict_render_dump` (see CMakeLists.txt). Mirrors the
// Go dump signalwire-go/cmd/strict-render-dump and the swml_dump tool.

#include <functional>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>

#include "signalwire/agent/agent_base.hpp"
#include "signalwire/contexts/contexts.hpp"
#include "signalwire/swaig/function_result.hpp"
#include "signalwire/swml/service.hpp"

using json = nlohmann::json;
using signalwire::agent::AgentBase;
using signalwire::swml::Service;

namespace {

// outcome runs build and classifies the result: any exception is "raised"; a
// clean return is "ok". Mirrors the python differ's try/except -> raised.
std::string outcome(const std::function<void()>& build) {
  try {
    build();
  } catch (...) {
    return "raised";
  }
  return "ok";
}

// add_verb is the SWMLService corpus verb: add_verb(name, config). A default
// Service has schema validation ON (Python SWMLService default), so the 2-arg
// add_verb strict-checks. Service is non-copyable, hence built in place in the
// closure.
std::function<void()> add_verb(const std::string& name, const json& config) {
  return [name, config]() {
    Service svc;
    svc.set_name("s").set_route("/s");
    svc.add_verb(name, config);
  };
}

// noop_handler is a define_tool handler stand-in (the corpus only needs the
// tool registered, never invoked).
signalwire::swaig::FunctionResult noop_handler(const json&, const json&) {
  return signalwire::swaig::FunctionResult("ok");
}

}  // namespace

int main() {
  json out = json::object();

  // ================================================================
  // Verb-level strict render (SWMLService, validation ON)
  // ================================================================
  out["strict_unknown_verb"] = outcome(add_verb("foobar", json::object()));
  out["strict_answer_misspelled_key"] = outcome(add_verb("answer", json{{"maxduration", 5}}));
  out["strict_answer_unknown_key"] = outcome(add_verb("answer", json{{"wibble", 1}}));
  out["strict_play_misspelled_key"] =
      outcome(add_verb("play", json{{"urlz", json::array({"say:hi"})}}));
  out["strict_play_valid_plus_unknown_key"] =
      outcome(add_verb("play", json{{"url", "say:hi"}, {"foo", 1}}));
  out["strict_record_misspelled_key"] = outcome(add_verb("record", json{{"formatt", "wav"}}));
  out["strict_answer_wrong_type"] =
      outcome(add_verb("answer", json{{"max_duration", "notanumber"}}));
  out["strict_ai_misspelled_top_key"] =
      outcome(add_verb("ai", json{{"prompt", {{"text", "hi"}}}, {"temperatur", 0.5}}));
  out["strict_ai_unknown_top_key"] =
      outcome(add_verb("ai", json{{"prompt", {{"text", "hi"}}}, {"zzz", 1}}));
  out["strict_ai_missing_prompt"] =
      outcome(add_verb("ai", json{{"post_prompt", {{"text", "bye"}}}}));

  // good documents must still render
  out["strict_answer_ok"] = outcome(add_verb("answer", json{{"max_duration", 5}}));
  out["strict_play_ok"] = outcome(add_verb("play", json{{"url", "say:hi"}}));
  out["strict_ai_ok"] = outcome(add_verb("ai", json{{"prompt", {{"text", "hi"}}}}));
  out["strict_ai_params_open_ok"] = outcome(
      add_verb("ai", json{{"prompt", {{"text", "hi"}}}, {"params", {{"some_future_param", 1}}}}));

  // ================================================================
  // Contexts-level strict render (AgentBase; dangling refs)
  // ================================================================

  // strict_dangling_step_function: order_status registered, step whitelists an
  // unregistered non-native 'get_datetime' -> dangling -> raise.
  out["strict_dangling_step_function"] = outcome([]() {
    AgentBase a("a", "/a");
    json params = json{{"type", "object"}, {"properties", json::object()}};
    a.define_tool("order_status", "look up an order", params, noop_handler);
    auto& cb = a.define_contexts();
    auto& st = cb.add_context("default").add_step("help");
    st.set_text("help");
    st.set_functions(std::vector<std::string>{"order_status", "get_datetime"});
    cb.validate();
  });

  // strict_registered_step_function_ok: step whitelists a registered tool.
  out["strict_registered_step_function_ok"] = outcome([]() {
    AgentBase a("a", "/a");
    json params = json{{"type", "object"}, {"properties", json::object()}};
    a.define_tool("order_status", "look up an order", params, noop_handler);
    auto& cb = a.define_contexts();
    auto& st = cb.add_context("default").add_step("help");
    st.set_text("help");
    st.set_functions(std::vector<std::string>{"order_status"});
    cb.validate();
  });

  // strict_reserved_native_function_ok: reserved natives are not dangling.
  out["strict_reserved_native_function_ok"] = outcome([]() {
    AgentBase a("a", "/a");
    auto& cb = a.define_contexts();
    auto& st = cb.add_context("default").add_step("help");
    st.set_text("help");
    st.set_functions(std::vector<std::string>{"next_step", "change_context"});
    cb.validate();
  });

  // strict_dangling_valid_context: valid_contexts references an undefined context.
  out["strict_dangling_valid_context"] = outcome([]() {
    AgentBase a("a", "/a");
    auto& cb = a.define_contexts();
    auto& st = cb.add_context("default").add_step("help");
    st.set_text("help");
    st.set_valid_contexts(std::vector<std::string>{"nowhere"});
    cb.validate();
  });

  std::cout << out.dump() << "\n";
  return 0;
}
