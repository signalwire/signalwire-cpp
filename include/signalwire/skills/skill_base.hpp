// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#pragma once

#include <string>
#include <vector>
#include <map>
#include <functional>
#include <nlohmann/json.hpp>
#include "signalwire/swaig/function_result.hpp"
#include "signalwire/swaig/tool_definition.hpp"

namespace signalwire {
namespace skills {

using json = nlohmann::json;

/// POM section for skill prompts
struct SkillPromptSection {
    std::string title;
    std::string body;
    std::vector<std::string> bullets;
};

/// Abstract base class for all skills
class SkillBase {
public:
    virtual ~SkillBase() = default;

    // ========================================================================
    // Required Overrides
    // ========================================================================

    virtual std::string skill_name() const = 0;
    virtual std::string skill_description() const = 0;
    virtual std::string skill_version() const { return "1.0.0"; }
    virtual bool supports_multiple_instances() const { return false; }
    virtual std::vector<std::string> required_env_vars() const { return {}; }
    virtual std::vector<std::string> required_packages() const { return {}; }

    /// Initialize the skill with given params. Return true on success.
    virtual bool setup(const json& params) = 0;

    /// Register tools with the agent. Returns tool definitions.
    virtual std::vector<swaig::ToolDefinition> register_tools() = 0;

    // ========================================================================
    // Optional Overrides
    // ========================================================================

    /// Get speech recognition hints to merge into agent
    virtual std::vector<std::string> get_hints() const { return {}; }

    /// Get global data to merge into agent
    virtual json get_global_data() const { return json::object(); }

    /// Get prompt sections to inject into agent
    virtual std::vector<SkillPromptSection> get_prompt_sections() const { return {}; }

    /// Get SWAIG DataMap functions (for DataMap-based skills)
    virtual std::vector<json> get_datamap_functions() const { return {}; }

    /// Get parameter schema for GUI tools
    virtual json get_parameter_schema() const { return json::object(); }

    /// Get instance key for multi-instance skills
    virtual std::string get_instance_key() const { return skill_name(); }

    /// Cleanup resources
    virtual void cleanup() {}

    // ========================================================================
    // Helpers
    // ========================================================================

    /// Define a tool (convenience for register_tools implementations)
    swaig::ToolDefinition define_tool(
        const std::string& name,
        const std::string& description,
        const json& parameters,
        swaig::ToolHandler handler,
        bool secure = false) {
        swaig::ToolDefinition td;
        td.name = name;
        td.description = description;
        td.parameters = parameters;
        td.handler = std::move(handler);
        td.secure = secure;
        return td;
    }

    /// Get a parameter value with a default
    template<typename T>
    T get_param(const json& params, const std::string& key, const T& default_val) const {
        if (params.contains(key)) {
            return params[key].get<T>();
        }
        return default_val;
    }

    /// Get a string parameter with env var fallback
    std::string get_param_or_env(const json& params, const std::string& key,
                                  const std::string& env_var,
                                  const std::string& default_val = "") const {
        if (params.contains(key) && params[key].is_string()) {
            return params[key].get<std::string>();
        }
        const char* env = std::getenv(env_var.c_str());
        if (env) return std::string(env);
        return default_val;
    }

protected:
    json params_;
};

/// Factory function type for creating skill instances
using SkillFactory = std::function<std::unique_ptr<SkillBase>()>;

} // namespace skills
} // namespace signalwire
