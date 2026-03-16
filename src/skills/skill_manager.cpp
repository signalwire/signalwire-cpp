// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#include "signalwire/skills/skill_manager.hpp"
#include "signalwire/agent/agent_base.hpp"
#include "signalwire/logging.hpp"
#include <algorithm>

namespace signalwire {
namespace skills {

bool SkillManager::load_skill(const std::string& skill_name, const json& params,
                               agent::AgentBase& agent) {
    auto& registry = SkillRegistry::instance();

    if (!registry.has_skill(skill_name)) {
        get_logger().error("Unknown skill: " + skill_name);
        return false;
    }

    auto skill = registry.create(skill_name);
    if (!skill) {
        get_logger().error("Failed to create skill: " + skill_name);
        return false;
    }

    // Check for duplicates if skill doesn't support multiple instances
    if (!skill->supports_multiple_instances() && is_loaded(skill_name)) {
        get_logger().warn("Skill already loaded and doesn't support multiple instances: " + skill_name);
        return false;
    }

    // Validate env vars
    for (const auto& env : skill->required_env_vars()) {
        if (!std::getenv(env.c_str())) {
            get_logger().error("Missing required env var for skill " + skill_name + ": " + env);
            return false;
        }
    }

    // Setup
    if (!skill->setup(params)) {
        get_logger().error("Skill setup failed: " + skill_name);
        return false;
    }

    // Register tools
    auto tools = skill->register_tools();
    for (auto& tool : tools) {
        agent.define_tool(tool);
    }

    // Register DataMap functions
    auto dm_funcs = skill->get_datamap_functions();
    for (const auto& dm : dm_funcs) {
        agent.register_swaig_function(dm);
    }

    // Merge hints
    auto hints = skill->get_hints();
    if (!hints.empty()) {
        agent.add_hints(hints);
    }

    // Merge global data
    auto gdata = skill->get_global_data();
    if (!gdata.is_null() && !gdata.empty()) {
        agent.update_global_data(gdata);
    }

    // Inject prompt sections
    auto sections = skill->get_prompt_sections();
    for (const auto& sec : sections) {
        agent.prompt_add_section(sec.title, sec.body, sec.bullets);
    }

    LoadedSkill ls;
    ls.name = skill_name;
    ls.instance = std::move(skill);
    ls.params = params;
    loaded_skills_.push_back(std::move(ls));

    get_logger().info("Loaded skill: " + skill_name);
    return true;
}

void SkillManager::unload_skill(const std::string& skill_name) {
    for (auto it = loaded_skills_.begin(); it != loaded_skills_.end(); ++it) {
        if (it->name == skill_name) {
            it->instance->cleanup();
            loaded_skills_.erase(it);
            get_logger().info("Unloaded skill: " + skill_name);
            return;
        }
    }
}

bool SkillManager::is_loaded(const std::string& skill_name) const {
    for (const auto& ls : loaded_skills_) {
        if (ls.name == skill_name) return true;
    }
    return false;
}

std::vector<std::string> SkillManager::list_loaded() const {
    std::vector<std::string> names;
    for (const auto& ls : loaded_skills_) {
        names.push_back(ls.name);
    }
    return names;
}

void SkillManager::cleanup_all() {
    for (auto& ls : loaded_skills_) {
        ls.instance->cleanup();
    }
    loaded_skills_.clear();
}

} // namespace skills
} // namespace signalwire
