// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#include <sstream>

#include "signalwire/common.hpp"
#include "signalwire/skills/skill_base.hpp"
#include "signalwire/skills/skill_registry.hpp"
#include "signalwire/skills/skills_http.hpp"

namespace signalwire {
namespace skills {

/// SignalWire DataSphere RAG search skill — issues a real POST against
/// the DataSphere `/api/datasphere/documents/{document_id}/search` endpoint
/// with the user query in the JSON body, parses the `results[]` array,
/// and returns a flattened text summary. Matches the Python
/// `DatasphereSkill` upstream-call shape.
///
/// `DATASPHERE_BASE_URL` env var overrides the upstream URL (used by
/// `audit_skills_dispatch.py`); when unset, the real upstream is built
/// from `space_name` (`https://{space}.signalwire.com`).
class DatasphereSkill : public SkillBase {
 public:
  std::string skill_name() const override { return "datasphere"; }
  std::string skill_description() const override {
    return "Search knowledge using SignalWire DataSphere RAG stack";
  }
  bool supports_multiple_instances() const override { return true; }

  bool setup(const json& params) override {
    params_ = params;
    space_ = get_param_or_env(params, "space_name", "SIGNALWIRE_SPACE_NAME");
    project_id_ = get_param_or_env(params, "project_id", "SIGNALWIRE_PROJECT_ID");
    token_ = get_param_or_env(params, "token", "SIGNALWIRE_TOKEN");
    if (token_.empty()) token_ = get_env("DATASPHERE_TOKEN");
    doc_id_ = get_param<std::string>(params, "document_id", "");
    tool_name_ = get_param<std::string>(params, "tool_name", "search_knowledge");
    count_ = get_param<int>(params, "count", 1);
    return !space_.empty() && !project_id_.empty() && !token_.empty();
  }

  std::vector<swaig::ToolDefinition> register_tools() override {
    return {define_tool(
        tool_name_,
        "Search the knowledge base for information on any topic and return relevant results",
        json::object({{"type", "object"},
                      {"properties",
                       json::object({{"query", json::object({{"type", "string"},
                                                             {"description", "Search query"}})}})},
                      {"required", json::array({"query"})}}),
        [this](const json& args, const json&) -> swaig::FunctionResult {
          std::string query = args.value("query", "");
          if (query.empty()) return swaig::FunctionResult("No search query provided");

          // Build the upstream URL. Tests/audits override the base
          // via DATASPHERE_BASE_URL; production uses the per-space
          // host pattern matching Python.
          std::string base = get_env("DATASPHERE_BASE_URL");
          if (base.empty()) {
            base = "https://" + space_ + ".signalwire.com";
          }
          std::string url = base + "/api/datasphere/documents/" + url_encode(doc_id_) + "/search";

          json body = json::object({
              {"query_string", query},
              {"count", count_},
          });

          std::map<std::string, std::string> headers;
          std::string basic = base64_encode(project_id_ + ":" + token_);
          headers["Authorization"] = "Basic " + basic;

          auto resp = http_post(url, body.dump(), "application/json", headers);
          if (resp.status == 0) {
            return swaig::FunctionResult("DataSphere transport error: " + resp.error);
          }
          if (resp.status < 200 || resp.status >= 300) {
            return swaig::FunctionResult("DataSphere HTTP " + std::to_string(resp.status) + ": " +
                                         resp.body);
          }

          json parsed;
          try {
            parsed = json::parse(resp.body);
          } catch (const json::parse_error& e) {
            return swaig::FunctionResult(std::string("DataSphere parse error: ") + e.what());
          }

          std::ostringstream out;
          out << "DataSphere results for '" << query << "':\n";
          if (parsed.contains("results") && parsed["results"].is_array()) {
            for (const auto& r : parsed["results"]) {
              out << "- " << r.value("text", "") << "\n";
            }
          }
          return swaig::FunctionResult(out.str());
        })};
  }

  std::vector<SkillPromptSection> get_prompt_sections() const override {
    return {{"Knowledge Search Capability",
             "You can search a knowledge base for information.",
             {"Use " + tool_name_ + " to search the knowledge base",
              "Always search before saying you don't know something"}}};
  }

  json get_global_data() const override {
    return json::object({{"datasphere_enabled", true},
                         {"document_id", doc_id_},
                         {"knowledge_provider", "SignalWire DataSphere"}});
  }

 private:
  std::string space_, project_id_, token_, doc_id_, tool_name_;
  int count_ = 1;
};

REGISTER_SKILL(DatasphereSkill)

}  // namespace skills
}  // namespace signalwire
