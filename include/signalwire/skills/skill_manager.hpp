// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#pragma once

#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "signalwire/skills/skill_base.hpp"
#include "signalwire/skills/skill_registry.hpp"

namespace signalwire {

namespace agent {
class AgentBase;
}

namespace skills {

using json = nlohmann::json;

/// Manages skill lifecycle: creation, setup, registration with agents
class SkillManager {
 public:
  SkillManager() = default;

  /// Construct bound to the agent this manager loads skills into. Mirrors the
  /// reference's ``SkillManager(agent)``.
  explicit SkillManager(agent::AgentBase& agent) : agent_(&agent) {}

  /// The agent this manager loads skills into (reference: ``self.agent``).
  /// ``nullptr`` for a default-constructed manager, which takes the agent
  /// per ``load_skill`` call instead.
  [[nodiscard]] agent::AgentBase* agent() const { return agent_; }

  /// Load a skill by name with params and register it with the agent
  [[nodiscard]] bool load_skill(const std::string& skill_name, const json& params,
                                agent::AgentBase& agent);

  /// Unload a skill
  void unload_skill(const std::string& skill_name);

  /// Check if a skill is loaded
  [[nodiscard]] bool is_loaded(const std::string& skill_name) const;

  /// List loaded skills
  [[nodiscard]] std::vector<std::string> list_loaded() const;

  // ---- Public surface (signalwire.core.skill_manager.SkillManager) --

  /// Whether a skill is loaded (Python: ``has_skill``). Alias of is_loaded.
  [[nodiscard]] bool has_skill(const std::string& skill_name) const {
    return is_loaded(skill_name);
  }

  /// List loaded skill names (Python: ``list_loaded_skills``). Alias of list_loaded.
  [[nodiscard]] std::vector<std::string> list_loaded_skills() const { return list_loaded(); }

  /// Get a loaded skill instance by name, or nullptr if not loaded
  /// (Python: ``get_skill``).
  [[nodiscard]] SkillBase* get_skill(const std::string& skill_name) const;

  /// Cleanup all skills
  void cleanup_all();

 private:
  struct LoadedSkill {
    std::string name;
    std::unique_ptr<SkillBase> instance;
    json params;
  };

  std::vector<LoadedSkill> loaded_skills_;
  /// The bound agent — non-owning; the agent owns this manager.
  agent::AgentBase* agent_ = nullptr;
};

}  // namespace skills
}  // namespace signalwire
