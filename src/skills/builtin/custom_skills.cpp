// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#include "signalwire/skills/skill_base.hpp"
#include "signalwire/skills/skill_registry.hpp"

namespace signalwire {
namespace skills {

class CustomSkillsSkill : public SkillBase {
public:
    std::string skill_name() const override { return "custom_skills"; }
    std::string skill_description() const override { return "Register user-defined custom tools"; }
    bool supports_multiple_instances() const override { return true; }

    bool setup(const json& params) override {
        params_ = params;
        return params.contains("tools");
    }

    std::vector<swaig::ToolDefinition> register_tools() override {
        std::vector<swaig::ToolDefinition> tools;

        if (!params_.contains("tools") || !params_["tools"].is_array()) return tools;

        for (const auto& tool_def : params_["tools"]) {
            swaig::ToolDefinition td;
            td.name = tool_def.value("name", "custom_tool");
            td.description = tool_def.value("description", "Custom tool");
            td.parameters = tool_def.value("parameters", json::object({
                {"type", "object"}, {"properties", json::object()}
            }));

            // Custom tools use a generic handler that returns the tool config's response
            std::string response = tool_def.value("response", "Tool executed");
            td.handler = [response](const json& args, const json&) -> swaig::FunctionResult {
                return swaig::FunctionResult(response);
            };

            tools.push_back(std::move(td));
        }

        return tools;
    }
};

REGISTER_SKILL(CustomSkillsSkill)

} // namespace skills
} // namespace signalwire
