// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#include "signalwire/skills/skill_base.hpp"
#include "signalwire/skills/skill_registry.hpp"

namespace signalwire {
namespace skills {

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
        doc_id_ = get_param<std::string>(params, "document_id", "");
        tool_name_ = get_param<std::string>(params, "tool_name", "search_knowledge");
        count_ = get_param<int>(params, "count", 1);
        return !space_.empty() && !project_id_.empty() && !token_.empty();
    }

    std::vector<swaig::ToolDefinition> register_tools() override {
        return {define_tool(tool_name_,
            "Search the knowledge base for information on any topic and return relevant results",
            json::object({{"type", "object"}, {"properties", json::object({
                {"query", json::object({{"type", "string"}, {"description", "Search query"}})}
            })}, {"required", json::array({"query"})}}),
            [this](const json& args, const json&) -> swaig::FunctionResult {
                std::string query = args.value("query", "");
                return swaig::FunctionResult(
                    "DataSphere search for '" + query + "' in document " + doc_id_ +
                    ": [Would POST to DataSphere API]");
            })};
    }

    std::vector<SkillPromptSection> get_prompt_sections() const override {
        return {{"Knowledge Search Capability",
                 "You can search a knowledge base for information.",
                 {"Use " + tool_name_ + " to search the knowledge base",
                  "Always search before saying you don't know something"}}};
    }

    json get_global_data() const override {
        return json::object({
            {"datasphere_enabled", true},
            {"document_id", doc_id_},
            {"knowledge_provider", "SignalWire DataSphere"}
        });
    }

private:
    std::string space_, project_id_, token_, doc_id_, tool_name_;
    int count_ = 1;
};

REGISTER_SKILL(DatasphereSkill)

} // namespace skills
} // namespace signalwire
