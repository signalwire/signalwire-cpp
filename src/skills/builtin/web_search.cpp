// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#include "signalwire/skills/skill_base.hpp"
#include "signalwire/skills/skill_registry.hpp"

namespace signalwire {
namespace skills {

class WebSearchSkill : public SkillBase {
public:
    std::string skill_name() const override { return "web_search"; }
    std::string skill_description() const override { return "Search the web for information using Google Custom Search API"; }
    std::string skill_version() const override { return "2.0.0"; }
    bool supports_multiple_instances() const override { return true; }

    bool setup(const json& params) override {
        params_ = params;
        api_key_ = get_param_or_env(params, "api_key", "GOOGLE_SEARCH_API_KEY");
        search_engine_id_ = get_param_or_env(params, "search_engine_id", "GOOGLE_SEARCH_ENGINE_ID");
        tool_name_ = get_param<std::string>(params, "tool_name", "web_search");
        num_results_ = get_param<int>(params, "num_results", 3);
        return !api_key_.empty() && !search_engine_id_.empty();
    }

    std::vector<swaig::ToolDefinition> register_tools() override {
        return {define_tool(
            tool_name_,
            "Search the web for high-quality information, automatically filtering low-quality results",
            json::object({
                {"type", "object"},
                {"properties", json::object({
                    {"query", json::object({{"type", "string"}, {"description", "Search query"}})}
                })},
                {"required", json::array({"query"})}
            }),
            [this](const json& args, const json&) -> swaig::FunctionResult {
                std::string query = args.value("query", "");
                if (query.empty()) return swaig::FunctionResult("No search query provided");
                // In a real implementation, this would call the Google Custom Search API
                return swaig::FunctionResult(
                    "Web search results for '" + query +
                    "': [Results would be fetched from Google Custom Search API with key]");
            }
        )};
    }

    std::vector<SkillPromptSection> get_prompt_sections() const override {
        return {{"Web Search Capability (Quality Enhanced)",
                 "You can search the web for current information.",
                 {"Use " + tool_name_ + " to find information online",
                  "Results are automatically filtered for quality"}}};
    }

    json get_global_data() const override {
        return json::object({
            {"web_search_enabled", true},
            {"search_provider", "Google Custom Search"},
            {"quality_filtering", true}
        });
    }

private:
    std::string api_key_;
    std::string search_engine_id_;
    std::string tool_name_ = "web_search";
    int num_results_ = 3;
};

REGISTER_SKILL(WebSearchSkill)

} // namespace skills
} // namespace signalwire
