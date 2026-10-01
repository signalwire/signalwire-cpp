// swml_dump.cpp — the C++ port's SWML dump program for the cross-port SWML
// differ (porting-sdk/scripts/diff_port_swml.py).
//
// For each swml_corpus case it builds an AgentBase, applies the setter chain,
// renders the SWML document, and extracts the observed dotted path (e.g.
// "ai.prompt.pom") — emitting ONE JSON object mapping
//
//     case-id -> extracted-fragment
//
// to stdout. The differ canonicalizes both sides and byte-compares against the
// python oracle. Only stdout carries JSON; logs go to stderr.
//
// Build: a CMake target `swml_dump` (see CMakeLists.txt). Mirrors the Go dump
// signalwire-go/cmd/swml-dump.

#include <iostream>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "signalwire/agent/agent_base.hpp"
#include "signalwire/agents/bedrock.hpp"
#include "signalwire/swaig/function_result.hpp"
#include "signalwire/swaig/tool_definition.hpp"

using json = nlohmann::json;
using signalwire::agent::AgentBase;
using signalwire::agent::LanguageConfig;

namespace {

// new_agent constructs a demo AgentBase (name "demo", route "/demo") with POM
// enabled so prompt_add_section renders into ai.prompt.pom, matching the oracle.
AgentBase new_agent() {
  AgentBase a("demo", "/demo");
  a.set_use_pom(true);
  return a;
}

std::vector<std::string> split_dots(const std::string& s) {
  std::vector<std::string> out;
  std::string cur;
  for (char c : s) {
    if (c == '.') {
      out.push_back(cur);
      cur.clear();
    } else {
      cur.push_back(c);
    }
  }
  out.push_back(cur);
  return out;
}

// extract walks a dotted path into a rendered SWML doc. "ai.prompt" means: find
// the ai verb in sections.main, then index into it — the mirror of
// diff_port_swml._extract and the Go dump's extract.
json extract(const json& doc, const std::string& path) {
  json ai;
  bool found_ai = false;
  if (doc.contains("sections") && doc["sections"].contains("main") &&
      doc["sections"]["main"].is_array()) {
    for (const auto& sec : doc["sections"]["main"]) {
      if (sec.is_object() && sec.contains("ai")) {
        ai = sec["ai"];
        found_ai = true;
        break;
      }
    }
  }
  json node = found_ai ? json{{"ai", ai}} : doc;
  for (const auto& part : split_dots(path)) {
    if (!node.is_object() || !node.contains(part)) {
      return json(nullptr);
    }
    node = node[part];
  }
  return node;
}

// pick reduces a map fragment to the listed keys (mirrors the oracle's `pick`).
json pick(const json& frag, const std::vector<std::string>& keys) {
  if (!frag.is_object()) {
    return frag;
  }
  json out = json::object();
  // The oracle's pick is ``{k: frag.get(k)}``: an absent key is null, not omitted.
  for (const auto& k : keys) {
    out[k] = frag.contains(k) ? frag[k] : json(nullptr);
  }
  return out;
}

json render(const AgentBase& a) { return a.render_swml(); }

}  // namespace

int main() {
  // exception-escape guard: main() must not let an exception escape
  // (that is std::terminate, with no message). Report and exit nonzero.
  try {
    // The dump's stdout is the JSON document; keep SDK log lines off it.
    signalwire::Logger::instance().suppress();
    json out = json::object();

    // swml_set_prompt_llm_params: two set_prompt_llm_params calls MERGE.
    {
      AgentBase a = new_agent();
      a.set_prompt_llm_params(json{{"temperature", 0.5}});
      a.set_prompt_llm_params(json{{"top_p", 0.9}});
      out["swml_set_prompt_llm_params"] =
          pick(extract(render(a), "ai.prompt"), {"temperature", "top_p"});
    }

    // swml_set_post_prompt_llm_params: establish a post-prompt, then merge params.
    {
      AgentBase a = new_agent();
      a.set_post_prompt("Summarize the call.");
      a.set_post_prompt_llm_params(json{{"temperature", 0.3}});
      a.set_post_prompt_llm_params(json{{"top_p", 0.8}});
      out["swml_set_post_prompt_llm_params"] =
          pick(extract(render(a), "ai.post_prompt"), {"temperature", "top_p"});
    }

    // swml_add_language: engine/model/voice carried into ai.languages.
    {
      AgentBase a = new_agent();
      LanguageConfig lang;
      lang.name = "English";
      lang.code = "en-US";
      lang.voice = "rime.spore";
      lang.engine = "rime";
      lang.model = "mistv2";
      a.add_language(lang);
      out["swml_add_language"] = extract(render(a), "ai.languages");
    }

    // swml_add_pattern_hint: structured hint into ai.hints.
    {
      AgentBase a = new_agent();
      a.add_pattern_hint("SignalWire", "signal wire", "SignalWire", true);
      out["swml_add_pattern_hint"] = extract(render(a), "ai.hints");
    }

    // swml_add_hint: a plain string hint.
    {
      AgentBase a = new_agent();
      a.add_hint("SignalWire");
      out["swml_add_hint"] = extract(render(a), "ai.hints");
    }

    // swml_prompt_add_section: POM sections render into ai.prompt.pom.
    {
      AgentBase a = new_agent();
      a.prompt_add_section("Role", "You are a helpful assistant.", {});
      a.prompt_add_section("Rules", "", {"Be concise", "Be accurate"});
      out["swml_prompt_add_section"] = extract(render(a), "ai.prompt.pom");
    }

    // swml_add_pronunciation: renders into ai.pronounce.
    {
      AgentBase a = new_agent();
      a.add_pronunciation("SW", "SignalWire", true);
      out["swml_add_pronunciation"] = extract(render(a), "ai.pronounce");
    }

    // swml_define_tool_complete_schema: define_tool with a COMPLETE
    // {type,properties,required} schema must render ai.SWAIG.functions[?lookup]
    // .parameters as that schema FLAT (pass-through), NOT double-wrapped.
    {
      AgentBase a = new_agent();
      json schema = json{{"type", "object"},
                         {"properties", {{"q", {{"type", "string"}}}}},
                         {"required", json::array({"q"})}};
      a.define_tool("lookup", "Look up a thing", schema, [](const json&, const json&) {
        return signalwire::swaig::FunctionResult("ok");
      });
      json funcs = extract(render(a), "ai.SWAIG.functions");
      json params = json(nullptr);
      if (funcs.is_array()) {
        for (const auto& f : funcs) {
          if (f.is_object() && f.value("function", "") == "lookup" && f.contains("parameters")) {
            params = f["parameters"];
            break;
          }
        }
      }
      out["swml_define_tool_complete_schema"] = params;
    }

    // swml_contexts_in_prompt: contexts render inside ai.prompt, not ai.contexts.
    {
      AgentBase a("demo", "/demo");
      a.prompt_add_section("Role", "You take orders.", {});
      auto& ctx = a.define_contexts().add_context("default");
      ctx.add_step("greet").set_text("Greet the caller.");
      out["swml_contexts_in_prompt"] = pick(extract(render(a), "ai.prompt"), {"contexts"});
    }

    // swml_on_call_end_hook / _post_conversation: on_call_end registers the
    // reserved hangup_hook and turns on ai.params.swaig_post_conversation.
    {
      AgentBase a("demo", "/demo");
      a.on_call_end([](const json&, const json&) {});
      json doc = render(a);
      json funcs = extract(doc, "ai.SWAIG.functions");
      json desc = json(nullptr);
      if (funcs.is_array()) {
        for (const auto& f : funcs) {
          if (f.is_object() && f.value("function", "") == "hangup_hook" &&
              f.contains("description")) {
            desc = f["description"];
            break;
          }
        }
      }
      out["swml_on_call_end_hook"] = desc;
      out["swml_on_call_end_post_conversation"] =
          pick(extract(doc, "ai.params"), {"swaig_post_conversation"});
    }

    // swml_bedrock_prompt: BedrockAgent's amazon_bedrock prompt.
    {
      signalwire::agents::BedrockAgent b("bedrock", "/bedrock", "", "tiffany", 0.7, 0.9, 512);
      b.set_prompt_text("You are a helpful assistant.");
      b.set_prompt_llm_params(json{{"presence_penalty", 0.3},
                                   {"frequency_penalty", 0.2},
                                   {"confidence", 0.5},
                                   {"barge_confidence", 0.4}});
      json doc = b.render_swml();
      json prompt = json(nullptr);
      if (doc.contains("sections") && doc["sections"].contains("main")) {
        for (const auto& verb : doc["sections"]["main"]) {
          if (verb.is_object() && verb.contains("amazon_bedrock") &&
              verb["amazon_bedrock"].contains("prompt")) {
            prompt = verb["amazon_bedrock"]["prompt"];
            break;
          }
        }
      }
      out["swml_bedrock_prompt"] =
          pick(prompt, {"voice_id", "max_tokens", "temperature", "top_p", "presence_penalty",
                        "frequency_penalty", "confidence", "barge_confidence"});
    }

    std::cout << out.dump() << "\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "fatal: " << e.what() << "\n";
    return 1;
  }
}
