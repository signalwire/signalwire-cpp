// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#include "signalwire/skills/skill_base.hpp"
#include "signalwire/skills/skill_registry.hpp"

namespace signalwire {
namespace skills {

class WikipediaSearchSkill : public SkillBase {
public:
    std::string skill_name() const override { return "wikipedia_search"; }
    std::string skill_description() const override {
        return "Search Wikipedia for information about a topic and get article summaries";
    }

    bool setup(const json& params) override {
        params_ = params;
        num_results_ = get_param<int>(params, "num_results", 1);
        no_results_msg_ = get_param<std::string>(params, "no_results_message",
            "No Wikipedia articles found for that topic.");
        return true;
    }

    std::vector<swaig::ToolDefinition> register_tools() override {
        return {define_tool(
            "search_wiki",
            "Search Wikipedia for information about a topic and get article summaries",
            json::object({
                {"type", "object"},
                {"properties", json::object({
                    {"query", json::object({{"type", "string"}, {"description", "Topic to search"}})}
                })},
                {"required", json::array({"query"})}
            }),
            [this](const json& args, const json&) -> swaig::FunctionResult {
                std::string query = args.value("query", "");
                if (query.empty()) return swaig::FunctionResult(no_results_msg_);
                return swaig::FunctionResult(
                    "Wikipedia search for '" + query + "': "
                    "[Results would be fetched from Wikipedia REST API]");
            }
        )};
    }

    std::vector<SkillPromptSection> get_prompt_sections() const override {
        return {{"Wikipedia Search",
                 "You can search Wikipedia for factual information.",
                 {"Use search_wiki to find information on Wikipedia"}}};
    }

private:
    int num_results_ = 1;
    std::string no_results_msg_;
};

REGISTER_SKILL(WikipediaSearchSkill)

} // namespace skills
} // namespace signalwire
