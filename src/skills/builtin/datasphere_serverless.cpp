// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#include "signalwire/skills/skill_base.hpp"
#include "signalwire/skills/skill_registry.hpp"
#include "signalwire/datamap/datamap.hpp"

namespace signalwire {
namespace skills {

class DatasphereServerlessSkill : public SkillBase {
public:
    std::string skill_name() const override { return "datasphere_serverless"; }
    std::string skill_description() const override {
        return "Search knowledge using SignalWire DataSphere with serverless DataMap execution";
    }
    bool supports_multiple_instances() const override { return true; }

    bool setup(const json& params) override {
        params_ = params;
        space_ = get_param_or_env(params, "space_name", "SIGNALWIRE_SPACE_NAME");
        project_id_ = get_param_or_env(params, "project_id", "SIGNALWIRE_PROJECT_ID");
        token_ = get_param_or_env(params, "token", "SIGNALWIRE_TOKEN");
        doc_id_ = get_param<std::string>(params, "document_id", "");
        tool_name_ = get_param<std::string>(params, "tool_name", "search_knowledge");
        return !space_.empty() && !project_id_.empty() && !token_.empty();
    }

    std::vector<swaig::ToolDefinition> register_tools() override { return {}; }

    std::vector<json> get_datamap_functions() const override {
        std::string url = "https://" + space_ + "/api/datasphere/documents/search";
        std::string auth = signalwire::base64_encode(project_id_ + ":" + token_);

        datamap::DataMap dm(tool_name_);
        dm.purpose("Search the knowledge base for information on any topic and return relevant results")
          .parameter("query", "string", "Search query", true)
          .webhook("POST", url, json::object({
              {"Content-Type", "application/json"},
              {"Authorization", "Basic " + auth}
          }))
          .body(json::object({
              {"query", "${args.query}"},
              {"document_id", doc_id_},
              {"count", 1}
          }))
          .output(swaig::FunctionResult("I found results for \"${args.query}\":\n\n${formatted_results}"));

        return {dm.to_swaig_function()};
    }

    std::vector<SkillPromptSection> get_prompt_sections() const override {
        return {{"Knowledge Search Capability (Serverless)",
                 "You can search a knowledge base for information using server-side execution.",
                 {"Use " + tool_name_ + " to search the knowledge base"}}};
    }

    json get_global_data() const override {
        return json::object({
            {"datasphere_serverless_enabled", true},
            {"document_id", doc_id_},
            {"knowledge_provider", "SignalWire DataSphere (Serverless)"}
        });
    }

private:
    std::string space_, project_id_, token_, doc_id_, tool_name_;
};

REGISTER_SKILL(DatasphereServerlessSkill)

} // namespace skills
} // namespace signalwire
