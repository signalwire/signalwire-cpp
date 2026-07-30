// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
//
// skills_audit_harness.cpp
//
// Runtime probe for the skills system. Driven by porting-sdk's
// `audit_skills_dispatch.py`. Reads:
//   - SKILL_NAME            e.g. `web_search`, `wikipedia_search`,
//                                `datasphere`, `spider`, `weather_api`,
//                                `api_ninjas_trivia`
//   - SKILL_FIXTURE_URL     `http://127.0.0.1:NNNN`
//   - SKILL_HANDLER_ARGS    JSON dict of args for the skill handler
//   - per-skill upstream env (e.g. WEB_SEARCH_BASE_URL); the audit
//     sets these to point the skill at its loopback fixture
//   - per-skill credential env vars (e.g. GOOGLE_API_KEY)
//
// For handler-based skills (web_search, wikipedia_search, datasphere,
// spider) the harness instantiates the skill, looks up its registered
// tool handler via SkillBase::register_tools(), and invokes the handler
// directly. Each handler issues real HTTP via skills_http (proven by the
// audit's fixture seeing the request).
//
// For DataMap-based skills (api_ninjas_trivia, weather_api) the SignalWire
// platform — not the SDK — would normally fetch the configured webhook
// URL. The harness simulates the platform behavior: it extracts the
// webhook URL from the registered DataMap and issues the HTTP call
// itself, satisfying the audit's contract that "the SDK contacted the
// upstream" via real bytes on the wire. The DataMap webhook URL contains
// `${args.foo}` placeholders that we expand from SKILL_HANDLER_ARGS.

#include <cstdlib>
#include <iostream>
#include <map>
#include <regex>
#include <signalwire/common.hpp>
#include <signalwire/skills/skill_base.hpp>
#include <signalwire/skills/skill_registry.hpp>
#include <signalwire/skills/skills_http.hpp>
#include <signalwire/swaig/function_result.hpp>
#include <sstream>
#include <string>

using namespace signalwire;
using json = nlohmann::json;

namespace {

void die(const std::string& msg) {
  std::cerr << "skills_audit_harness: " << msg << "\n";
  std::exit(1);
}

std::string env_or(const char* name, const std::string& fallback = "") {
  const char* v = std::getenv(name);
  return (v && *v) ? std::string(v) : fallback;
}

/// Expand DataMap webhook templates of the form `${args.foo}` /
/// `${lc:enc:args.foo}` against the args dict. Other placeholders
/// (`${response.foo}`, `${global_data.x}`) are left intact — they're
/// platform-side runtime variables the audit fixture doesn't care about.
std::string expand_template(const std::string& tmpl, const json& args) {
  static const std::regex re(R"(\$\{([^}]+)\})");
  std::string out;
  auto begin = std::sregex_iterator(tmpl.begin(), tmpl.end(), re);
  auto end = std::sregex_iterator();
  size_t last = 0;
  for (auto it = begin; it != end; ++it) {
    out.append(tmpl, last, it->position() - last);
    std::string inner = (*it)[1];

    // Strip optional `lc:` / `enc:` prefixes — we don't transform
    // for the audit; the captured wire path is what the fixture sees.
    std::string body = inner;
    while (true) {
      if (body.compare(0, 3, "lc:") == 0) {
        body = body.substr(3);
      } else if (body.compare(0, 4, "enc:") == 0) {
        body = body.substr(4);
      } else {
        break;
      }
    }

    if (body.compare(0, 5, "args.") == 0) {
      std::string field = body.substr(5);
      if (args.contains(field) && args[field].is_string()) {
        out += url_encode(args[field].get<std::string>());
      } else if (args.contains(field)) {
        out += args[field].dump();
      }
    } else {
      // Leave unknown placeholders in place.
      out += "${" + inner + "}";
    }
    last = it->position() + it->length();
  }
  out.append(tmpl, last, std::string::npos);
  return out;
}

/// Apply SKILL_FIXTURE_URL override to a webhook URL: replace the
/// scheme://host[:port] prefix with the fixture so the audit sees real
/// bytes on its loopback. The path + query is preserved so the audit's
/// path-substring assertion (`trivia`, `current.json`, etc.) still
/// matches.
std::string redirect_to_fixture(const std::string& url, const std::string& fixture) {
  if (fixture.empty()) {
    return url;
  }
  auto scheme_end = url.find("://");
  if (scheme_end == std::string::npos) {
    return url;
  }
  auto path_start = url.find('/', scheme_end + 3);
  std::string fix = fixture;
  while (!fix.empty() && fix.back() == '/') {
    fix.pop_back();
  }
  if (path_start == std::string::npos) {
    return fix;
  }
  return fix + url.substr(path_start);
}

/// Look up a tool handler from a skill's tool list by name.
const swaig::ToolDefinition* find_tool(const std::vector<swaig::ToolDefinition>& tools,
                                       const std::string& name) {
  for (const auto& t : tools) {
    if (t.name == name) {
      return &t;
    }
  }
  return nullptr;
}

/// Build skill construction params for skills that need credentials.
json build_skill_params(const std::string& skill_name) {
  json p = json::object();
  if (skill_name == "web_search") {
    // Audit sets GOOGLE_API_KEY / GOOGLE_CSE_ID + WEB_SEARCH_BASE_URL.
    const std::string k = env_or("GOOGLE_API_KEY");
    if (!k.empty()) {
      p["api_key"] = k;
    }
    const std::string c = env_or("GOOGLE_CSE_ID");
    if (!c.empty()) {
      p["search_engine_id"] = c;
    }
  } else if (skill_name == "datasphere") {
    // Audit sets DATASPHERE_TOKEN + DATASPHERE_BASE_URL. Plug
    // synthetic project_id / space_name / document_id so setup()
    // validates; the actual upstream call uses DATASPHERE_BASE_URL.
    p["space_name"] = "audit-space";
    p["project_id"] = "audit-project";
    p["document_id"] = "audit-doc";
    const std::string t = env_or("DATASPHERE_TOKEN");
    if (!t.empty()) {
      p["token"] = t;
    }
  } else if (skill_name == "weather_api") {
    const std::string k = env_or("WEATHER_API_KEY");
    if (!k.empty()) {
      p["api_key"] = k;
    }
  } else if (skill_name == "api_ninjas_trivia") {
    const std::string k = env_or("API_NINJAS_KEY");
    if (!k.empty()) {
      p["api_key"] = k;
    }
  }
  return p;
}

/// Drive a handler-based skill. The skill's handler issues real HTTP to
/// the upstream pointed at by the per-skill base-URL env var.
int run_handler_skill(skills::SkillBase& skill, const std::string& tool_name, const json& args) {
  auto tools = skill.register_tools();
  const auto* tool = find_tool(tools, tool_name);
  if (!tool) {
    die("skill did not register expected tool: " + tool_name);
  }
  if (!tool->handler) {
    die("tool '" + tool_name + "' has no handler");
  }

  swaig::FunctionResult r = tool->handler(args, json::object());
  // FunctionResult doesn't expose a getter for the response string —
  // serialize and lift it from the JSON. The audit's sentinel check
  // looks at the harness's stdout, so we just need to emit the parsed
  // SWAIG result.
  std::cout << r.to_json().dump() << "\n";
  return 0;
}

/// Drive a DataMap skill. We extract the webhook URL from the skill's
/// registered DataMap, expand `${args.foo}` placeholders, redirect at
/// the fixture, and execute the GET ourselves — this is what the
/// SignalWire platform does in production for DataMap-based tools.
int run_datamap_skill(skills::SkillBase& skill, const std::string& tool_name, const json& args) {
  auto datamaps = skill.get_datamap_functions();
  json dm_func;
  for (const auto& d : datamaps) {
    if (d.value("function", "") == tool_name) {
      dm_func = d;
      break;
    }
  }
  if (dm_func.empty()) {
    die("skill did not register DataMap for tool: " + tool_name);
  }

  if (!dm_func.contains("data_map") || !dm_func["data_map"].contains("webhooks") ||
      !dm_func["data_map"]["webhooks"].is_array() || dm_func["data_map"]["webhooks"].empty()) {
    die("DataMap '" + tool_name + "' has no webhook block");
  }
  const auto& webhook = dm_func["data_map"]["webhooks"][0];
  const std::string url_template = webhook.value("url", "");
  const std::string method = webhook.value("method", "GET");

  std::map<std::string, std::string> headers;
  if (webhook.contains("headers") && webhook["headers"].is_object()) {
    for (auto it = webhook["headers"].begin(); it != webhook["headers"].end(); ++it) {
      if (it.value().is_string()) {
        headers[it.key()] = it.value().get<std::string>();
      }
    }
  }

  std::string url = expand_template(url_template, args);
  std::string fixture = env_or("SKILL_FIXTURE_URL");
  url = redirect_to_fixture(url, fixture);

  skills::SkillHttpResponse resp;
  if (method == "POST" || method == "PUT") {
    resp = skills::http_post(url, "", "application/json", headers);
  } else {
    resp = skills::http_get(url, headers);
  }
  if (resp.status == 0) {
    die("DataMap HTTP failed: " + resp.error);
  }

  json parsed;
  try {
    parsed = json::parse(resp.body);
  } catch (...) {
    parsed = resp.body;
  }
  json out = json::object({
      {"status", resp.status},
      {"url", url},
      {"body", parsed},
  });
  std::cout << out.dump() << "\n";
  return 0;
}

}  // namespace

int main() {
  if (!std::getenv("SIGNALWIRE_LOG_MODE")) {
    ::setenv("SIGNALWIRE_LOG_MODE", "off", 1);
  }

  const std::string skill_name = env_or("SKILL_NAME");
  if (skill_name.empty()) {
    die("SKILL_NAME required");
  }

  const std::string args_raw = env_or("SKILL_HANDLER_ARGS", "{}");
  json args;
  try {
    args = json::parse(args_raw);
  } catch (const json::parse_error& e) {
    die(std::string("SKILL_HANDLER_ARGS not JSON: ") + e.what());
  }

  skills::ensure_builtin_skills_registered();
  auto& reg = skills::SkillRegistry::instance();
  if (!reg.has_skill(skill_name)) {
    die("skill not registered: " + skill_name);
  }

  auto skill = reg.create(skill_name);
  if (!skill) {
    die("failed to create skill: " + skill_name);
  }

  json params = build_skill_params(skill_name);
  if (!skill->setup(params)) {
    die("skill setup() returned false");
  }

  if (skill_name == "web_search") {
    return run_handler_skill(*skill, "web_search", args);
  }
  if (skill_name == "wikipedia_search") {
    return run_handler_skill(*skill, "search_wiki", args);
  }
  if (skill_name == "datasphere") {
    return run_handler_skill(*skill, "search_knowledge", args);
  }
  if (skill_name == "spider") {
    return run_handler_skill(*skill, "scrape_url", args);
  }
  if (skill_name == "weather_api") {
    return run_datamap_skill(*skill, "get_weather", args);
  }
  if (skill_name == "api_ninjas_trivia") {
    // The audit doesn't pass `category`; the upstream endpoint
    // accepts a wildcard request without one. Synthesize a default
    // so the URL template still expands cleanly.
    json effective = args;
    if (!effective.is_object()) {
      effective = json::object();
    }
    if (!effective.contains("category")) {
      effective["category"] = "general";
    }
    return run_datamap_skill(*skill, "get_trivia", effective);
  }

  die("unsupported skill: " + skill_name);
  return 1;  // unreachable
}
