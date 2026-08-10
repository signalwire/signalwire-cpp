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

  /// Construct bound to the agent this manager loads skills into.
  explicit SkillManager(agent::AgentBase& agent) : agent_(&agent) {}

  /// The agent this manager loads skills into. ``nullptr`` for a
  /// default-constructed manager, on which ``load_skill`` fails loud — a
  /// manager is normally agent-bound.
  [[nodiscard]] agent::AgentBase* agent() const { return agent_; }

  /// Load a skill by name and register it with this manager's bound agent.
  ///
  /// BOTH trailing parameters are optional.
  ///
  /// @param skill_class Optional explicit factory for the skill. When absent,
  ///   the skill is looked up in ``SkillRegistry`` by name via
  ///   ``SkillRegistry::get_skill_class(skill_name)``.
  /// @param params Optional parameters handed to the skill's setup.
  ///
  /// Requires a manager constructed with an agent; returns false and logs when
  /// there is none (use the explicit-agent overload in that case).
  [[nodiscard]] bool load_skill(const std::string& skill_name,
                                const std::optional<SkillFactory>& skill_class = std::nullopt,
                                const std::optional<json>& params = std::nullopt);

  /// Unload a skill
  void unload_skill(const std::string& skill_name);

  /// Check if a skill is loaded
  [[nodiscard]] bool is_loaded(const std::string& skill_name) const;

  /// List loaded skills
  [[nodiscard]] std::vector<std::string> list_loaded() const;

  // ---- Public surface ------------------------------------------------

  /// Whether a skill is loaded. Alias of ``is_loaded``.
  [[nodiscard]] bool has_skill(const std::string& skill_name) const {
    return is_loaded(skill_name);
  }

  /// List loaded skill names. Alias of ``list_loaded``.
  [[nodiscard]] std::vector<std::string> list_loaded_skills() const { return list_loaded(); }

  /// Get a loaded skill instance by name, or nullptr if not loaded.
  [[nodiscard]] SkillBase* get_skill(const std::string& skill_name) const;

  /// Cleanup all skills
  void cleanup_all();

 private:
  /// One skill the manager has loaded, and everything needed to unload it.
  ///
  /// `name` is the registry key `load_skill`/`unload_skill`/`is_loaded` match
  /// on. `instance` OWNS the constructed skill — the manager's `unique_ptr` is
  /// what keeps it alive, so `get_skill` hands back a non-owning `SkillBase*`
  /// that is valid only until that skill is unloaded or `cleanup_all` runs.
  /// `params` retains the configuration the skill was loaded with. Entries are
  /// kept in a vector, so `list_loaded` reports load order.
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
