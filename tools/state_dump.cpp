// state_dump.cpp — the C++ port's STATE dump program for the cross-port state
// differ (porting-sdk/scripts/diff_port_state.py).
//
// For each state_corpus case it builds the target object, applies the mutation
// chain via the C++ SDK's native API, reads the observable state through the
// public accessor / rendered representation, and prints ONE JSON object mapping
//
//     case-id -> observed-state
//
// to stdout. The differ canonicalizes both sides and byte-compares against the
// python oracle. Only stdout carries JSON; logs go to stderr.
//
// Build: a CMake target `state_dump` (see CMakeLists.txt). Mirrors the Go dump
// signalwire-go/cmd/state-dump.

#include <algorithm>
#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <vector>

#include "signalwire/agent/agent_base.hpp"
#include "signalwire/contexts/contexts.hpp"
#include "signalwire/core/swml_handler.hpp"
#include "signalwire/logging.hpp"
#include "signalwire/prefabs/prefabs.hpp"
#include "signalwire/server/agent_server.hpp"
#include "signalwire/skills/skill_registry.hpp"
#include "signalwire/swml/service.hpp"

using json = nlohmann::json;
using signalwire::agent::AgentBase;

namespace {

// A minimal custom verb handler — the C++ analog of the corpus's throwaway
// __register_verb__ handler.
class GreetVerbHandler : public signalwire::core::SWMLVerbHandler {
 public:
  explicit GreetVerbHandler(std::string name) : name_(std::move(name)) {}
  std::string get_verb_name() const override { return name_; }
  signalwire::core::VerbValidationResult validate_config(const json&) const override {
    return {true, {}};
  }
  json build_config(const json& kwargs) const override { return kwargs; }

 private:
  std::string name_;
};

AgentBase demo_agent() { return AgentBase("demo", "/demo"); }

// submit_answer_delta drives InfoGatherer.submit_answer and reduces the result
// to the observable delta (mirrors diff_port_state._observe "submit_answer_delta"):
// the set_global_data action's question_index + answers, plus a `done` flag
// derived from the completion message.
json submit_answer_delta(const json& args, const json& raw_data) {
  signalwire::prefabs::InfoGathererAgent ig("demo", "/demo");
  auto res = ig.submit_answer(args, raw_data);
  json m = res.to_json();
  json gd = json::object();
  if (m.contains("action") && m["action"].is_array()) {
    for (const auto& act : m["action"]) {
      if (act.is_object() && act.contains("set_global_data")) {
        gd = act["set_global_data"];
        break;
      }
    }
  }
  std::string resp = m.value("response", "");
  json out = json::object();
  out["question_index"] = gd.contains("question_index") ? gd["question_index"] : json(nullptr);
  out["answers"] = gd.contains("answers") ? gd["answers"] : json(nullptr);
  out["done"] = resp.find("All questions have been answered") != std::string::npos;
  return out;
}

}  // namespace

int main() {
  // Keep stdout pure JSON — suppress the SDK's INFO logs (which otherwise go to
  // stdout) so the differ reads only the JSON object.
  signalwire::Logger::instance().suppress();

  json out = json::object();

  // ---- global_data: set MERGES into the accumulated global data ----
  {
    AgentBase a = demo_agent();
    a.set_global_data(json{{"company", "SignalWire"}, {"tier", "gold"}});
    out["state_set_global_data"] = a.get_global_data();
  }
  {
    AgentBase a = demo_agent();
    a.update_global_data(json{{"k1", "v1"}});
    a.update_global_data(json{{"k2", "v2"}});
    out["state_update_global_data"] = a.get_global_data();
  }
  {
    // MERGE semantics: overlapping key wins, sibling survives.
    AgentBase a = demo_agent();
    a.set_global_data(json{{"a", 1}, {"b", 2}});
    a.set_global_data(json{{"b", 99}, {"c", 3}});
    out["state_global_data_merge"] = a.get_global_data();
  }

  // ---- sip-username registration on AgentBase (lowercased set) ----
  {
    AgentBase a = demo_agent();
    a.register_sip_username("Bob");
    a.register_sip_username("alice");
    // The oracle observes sorted(_sip_usernames), so sort the set.
    std::vector<std::string> u = a.get_sip_usernames();
    std::sort(u.begin(), u.end());
    out["state_register_sip_username"] = u;
  }
  {
    // dedup + case-fold: "Bob","BOB","bob" collapse to one.
    AgentBase a = demo_agent();
    a.register_sip_username("Bob");
    a.register_sip_username("BOB");
    a.register_sip_username("bob");
    std::vector<std::string> u = a.get_sip_usernames();
    std::sort(u.begin(), u.end());
    out["state_register_sip_username_dedup"] = u;
  }

  // ---- AgentServer sip-username mapping (username -> route) + lookup ----
  {
    signalwire::server::AgentServer s;
    s.setup_sip_routing("/sip", false);
    s.register_sip_username("Bob", "/agent");
    s.register_sip_username("sales", "/sales");
    json lookup_missing = json(nullptr);
    std::string missing = s.lookup_sip_route("nope");
    if (!missing.empty()) lookup_missing = missing;
    json mapping = json::object();
    for (const auto& [k, v] : s.get_sip_username_mapping()) mapping[k] = v;
    out["server_sip_username_mapping"] = json{{"mapping", mapping},
                                              {"lookup_bob", s.lookup_sip_route("bob")},
                                              {"lookup_BOB", s.lookup_sip_route("BOB")},
                                              {"lookup_missing", lookup_missing}};
  }
  {
    // unregister removes the agent route from the registry.
    signalwire::server::AgentServer s;
    s.register_(std::make_shared<AgentBase>("agent", "/agent"), "/agent");
    s.register_(std::make_shared<AgentBase>("other", "/other"), "/other");
    s.unregister("/agent");
    std::vector<std::string> routes = s.list_routes();
    std::sort(routes.begin(), routes.end());
    out["server_unregister"] = routes;
  }

  // ---- routing-callback registration on SWMLService (path-normalized) ----
  {
    signalwire::swml::Service svc;
    svc.set_name("svc");
    svc.set_route("/svc");
    auto noop = [](const json&, const std::map<std::string, std::string>&) -> std::string {
      return "";
    };
    svc.register_routing_callback(noop, "/sip/");
    svc.register_routing_callback(noop, "voice");
    out["state_register_routing_callback"] = svc.get_routing_callback_paths();
  }

  // ---- verb-handler registration (VerbHandlerRegistry: ai preloaded) ----
  {
    signalwire::core::VerbHandlerRegistry reg;
    reg.register_handler(std::make_shared<GreetVerbHandler>("greet"));
    out["state_register_verb_handler"] = json{{"verbs", reg.get_verb_names()},
                                              {"has_greet", reg.has_handler("greet")},
                                              {"has_ai", reg.has_handler("ai")},
                                              {"has_missing", reg.has_handler("nope")}};
  }

  // ---- skill registration (SkillRegistry: name -> factory, idempotent) ----
  {
    // The C++ SkillRegistry is a global singleton pre-populated with the
    // built-in skills, so observe the DELTA: the names this chain adds over the
    // pre-existing set (mirrors the oracle's fresh-registry ["custom_alpha",
    // "custom_beta"]). Registration is idempotent (a duplicate name is a no-op).
    auto& reg = signalwire::skills::SkillRegistry::instance();
    std::set<std::string> before;
    for (const auto& n : reg.list_skills()) before.insert(n);
    auto noop_factory = []() -> std::unique_ptr<signalwire::skills::SkillBase> { return nullptr; };
    reg.register_skill("custom_alpha", noop_factory);
    reg.register_skill("custom_beta", noop_factory);
    reg.register_skill("custom_alpha", noop_factory);  // idempotent
    std::vector<std::string> added;
    for (const auto& n : reg.list_skills()) {
      if (before.find(n) == before.end()) added.push_back(n);
    }
    std::sort(added.begin(), added.end());
    out["state_register_skill"] = added;
  }

  // ---- InfoGatherer.submit_answer: records answer + advances index ----
  out["infogatherer_submit_answer_first"] = submit_answer_delta(
      json{{"answer", "Alice"}},
      json{{"global_data",
            {{"questions",
              json::array({json{{"key_name", "name"}, {"question_text", "What is your name?"}},
                           json{{"key_name", "email"}, {"question_text", "What is your email?"}}})},
             {"question_index", 0},
             {"answers", json::array()}}}});
  out["infogatherer_submit_answer_last"] = submit_answer_delta(
      json{{"answer", "a@b.com"}},
      json{{"global_data",
            {{"questions",
              json::array({json{{"key_name", "name"}, {"question_text", "What is your name?"}},
                           json{{"key_name", "email"}, {"question_text", "What is your email?"}}})},
             {"question_index", 1},
             {"answers", json::array({json{{"key_name", "name"}, {"answer", "Alice"}}})}}}});

  // ---- contexts/steps navigation (valid_steps rendered per step) ----
  {
    AgentBase a = demo_agent();
    auto& cb = a.define_contexts();
    auto& ctx = cb.add_context("default");
    ctx.add_step("greet", "Greet the caller.", {}, "", std::nullopt, {"collect"});
    ctx.add_step("collect", "Collect their info.", {}, "", std::nullopt, {"greet"});
    json rendered = cb.to_json();
    json nav = json::object();
    for (auto it = rendered.begin(); it != rendered.end(); ++it) {
      json steps = json::array();
      if (it.value().contains("steps") && it.value()["steps"].is_array()) {
        for (const auto& s : it.value()["steps"]) {
          json reduced = json::object();
          reduced["name"] = s.contains("name") ? s["name"] : json(nullptr);
          reduced["valid_steps"] = s.contains("valid_steps") ? s["valid_steps"] : json(nullptr);
          steps.push_back(reduced);
        }
      }
      nav[it.key()] = steps;
    }
    out["state_contexts_navigation"] = nav;
  }

  std::cout << out.dump() << "\n";
  return 0;
}
