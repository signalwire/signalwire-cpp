// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#include "signalwire/skills/skill_base.hpp"
#include "signalwire/skills/skill_registry.hpp"

namespace signalwire {
namespace skills {

class ClaudeSkillsSkill : public SkillBase {
public:
    std::string skill_name() const override { return "claude_skills"; }
    std::string skill_description() const override { return "Load Claude SKILL.md files as agent tools"; }
    bool supports_multiple_instances() const override { return true; }

    bool setup(const json& params) override {
        params_ = params;
        skills_path_ = get_param<std::string>(params, "skills_path", "");
        tool_prefix_ = get_param<std::string>(params, "tool_prefix", "claude_");
        return !skills_path_.empty();
    }

    std::vector<swaig::ToolDefinition> register_tools() override {
        // In a full implementation, this would read .md files from skills_path_
        // and create tools from their YAML frontmatter
        return {define_tool(
            tool_prefix_ + "skill",
            "Execute a Claude skill",
            json::object({{"type", "object"}, {"properties", json::object({
                {"arguments", json::object({{"type", "string"}, {"description", "Arguments"}})}
            })}, {"required", json::array({"arguments"})}}),
            [this](const json& args, const json&) -> swaig::FunctionResult {
                return swaig::FunctionResult(
                    "Claude skill executed with: " + args.value("arguments", ""));
            })};
    }

    std::vector<std::string> get_hints() const override {
        // Extract hints from skill names
        return {};
    }

private:
    std::string skills_path_;
    std::string tool_prefix_ = "claude_";
};

REGISTER_SKILL(ClaudeSkillsSkill)

} // namespace skills
} // namespace signalwire
