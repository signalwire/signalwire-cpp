// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#include "signalwire/skills/claude_skills_core.hpp"
#include "signalwire/skills/skill_base.hpp"
#include "signalwire/skills/skill_registry.hpp"

namespace signalwire {
namespace skills {

/// Loads Claude-style SKILL.md files from a directory as SWAIG tools.
///
/// Discovery is real (see claude_skills_core.hpp): each immediate subdirectory
/// of ``skills_path`` that contains a ``SKILL.md`` becomes one declared tool
/// (``{tool_prefix}{sanitized-name}``) whose description comes from the
/// frontmatter and whose handler returns the SKILL.md body to the model.
/// NATIVE EXECUTION of skill scripts is impossible in this AOT port.
///
/// NOTE: this builtin's REGISTER_SKILL is dead-stripped from the static
/// archive; the registered impl is ``ClaudeSkillsSkillR`` in
/// skill_registry.cpp. Both delegate to claude_skills_core.hpp so they can
/// never drift.
class ClaudeSkillsSkill : public SkillBase {
 public:
  std::string skill_name() const override { return "claude_skills"; }
  std::string skill_description() const override {
    return "Load Claude SKILL.md files as agent tools";
  }
  bool supports_multiple_instances() const override { return true; }

  bool setup(const json& params) override {
    params_ = params;
    skills_path_ = get_param<std::string>(params, "skills_path", "");
    tool_prefix_ = get_param<std::string>(params, "tool_prefix", "claude_");
    if (skills_path_.empty()) {
      return false;
    }
    discovered_ = claude_core::discover_skills(skills_path_);
    return true;
  }

  std::vector<swaig::ToolDefinition> register_tools() override {
    std::vector<swaig::ToolDefinition> tools;
    tools.reserve(discovered_.size());
    for (const auto& skill : discovered_) {
      tools.push_back(claude_core::build_tool(skill, tool_prefix_));
    }
    return tools;
  }

  std::vector<std::string> get_hints() const override {
    return claude_core::hints_from(discovered_);
  }

 private:
  std::string skills_path_;
  std::string tool_prefix_ = "claude_";
  std::vector<claude_core::DiscoveredSkill> discovered_;
};

REGISTER_SKILL(ClaudeSkillsSkill)

}  // namespace skills
}  // namespace signalwire
