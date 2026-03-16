// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include "signalwire/skills/skill_base.hpp"
#include "signalwire/skills/skill_registry.hpp"

namespace signalwire {

namespace agent { class AgentBase; }

namespace skills {

using json = nlohmann::json;

/// Manages skill lifecycle: creation, setup, registration with agents
class SkillManager {
public:
    SkillManager() = default;

    /// Load a skill by name with params and register it with the agent
    bool load_skill(const std::string& skill_name, const json& params,
                    agent::AgentBase& agent);

    /// Unload a skill
    void unload_skill(const std::string& skill_name);

    /// Check if a skill is loaded
    bool is_loaded(const std::string& skill_name) const;

    /// List loaded skills
    std::vector<std::string> list_loaded() const;

    /// Cleanup all skills
    void cleanup_all();

private:
    struct LoadedSkill {
        std::string name;
        std::unique_ptr<SkillBase> instance;
        json params;
    };

    std::vector<LoadedSkill> loaded_skills_;
};

} // namespace skills
} // namespace signalwire
