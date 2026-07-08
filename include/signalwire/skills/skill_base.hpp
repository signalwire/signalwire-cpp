// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#pragma once

#include <functional>
#include <map>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

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

  [[nodiscard]] virtual std::string skill_name() const = 0;
  [[nodiscard]] virtual std::string skill_description() const = 0;
  [[nodiscard]] virtual std::string skill_version() const { return "1.0.0"; }
  [[nodiscard]] virtual bool supports_multiple_instances() const { return false; }
  [[nodiscard]] virtual std::vector<std::string> required_env_vars() const { return {}; }
  [[nodiscard]] virtual std::vector<std::string> required_packages() const { return {}; }

  /// Initialize the skill with given params. Return true on success.
  [[nodiscard]] virtual bool setup(const json& params) = 0;

  /// Register tools with the agent. Returns tool definitions.
  [[nodiscard]] virtual std::vector<swaig::ToolDefinition> register_tools() = 0;

  // ========================================================================
  // Optional Overrides
  // ========================================================================

  /// Get speech recognition hints to merge into agent
  [[nodiscard]] virtual std::vector<std::string> get_hints() const { return {}; }

  /// Get global data to merge into agent
  [[nodiscard]] virtual json get_global_data() const { return json::object(); }

  /// Get prompt sections to inject into agent
  [[nodiscard]] virtual std::vector<SkillPromptSection> get_prompt_sections() const { return {}; }

  /// Get SWAIG DataMap functions (for DataMap-based skills)
  [[nodiscard]] virtual std::vector<json> get_datamap_functions() const { return {}; }

  /// Get parameter schema for GUI tools
  [[nodiscard]] virtual json get_parameter_schema() const { return json::object(); }

  /// Get instance key for multi-instance skills
  [[nodiscard]] virtual std::string get_instance_key() const { return skill_name(); }

  /// Cleanup resources
  virtual void cleanup() {}

  // ========================================================================
  // Public surface (signalwire.core.skill_base.SkillBase)
  // ========================================================================

  /// Check that every required env var (required_env_vars()) is set.
  /// Corresponds to ``SkillBase.validate_env_vars``.
  [[nodiscard]] bool validate_env_vars() const {
    for (const auto& var : required_env_vars()) {
      const char* v = std::getenv(var.c_str());
      if (v == nullptr || v[0] == '\0') {
        return false;
      }
    }
    return true;
  }

  /// Check that every required package is available. C++ links its deps at
  /// build time (there is no runtime import), so a compiled skill's packages
  /// are inherently present — return true. Corresponds to ``validate_packages``.
  [[nodiscard]] bool validate_packages() const { return true; }

  /// Read this skill instance's namespaced state from a SWAIG handler's raw
  /// global_data. Corresponds to ``get_skill_data``.
  [[nodiscard]] json get_skill_data(const json& raw_data) const {
    const std::string ns = skill_namespace();
    json global_data = raw_data.value("global_data", json::object());
    return global_data.value(ns, json::object());
  }

  /// Write this skill instance's namespaced state into a FunctionResult (under
  /// the skill's namespace key). Corresponds to ``update_skill_data``.
  swaig::FunctionResult& update_skill_data(swaig::FunctionResult& result, const json& data) const {
    result.update_global_data(json::object({{skill_namespace(), data}}));
    return result;
  }

  // ========================================================================
  // Helpers
  // ========================================================================

  /// Define a tool (convenience for register_tools implementations)
  [[nodiscard]] swaig::ToolDefinition define_tool(const std::string& name,
                                                  const std::string& description,
                                                  const json& parameters,
                                                  swaig::ToolHandler handler, bool secure = false) {
    swaig::ToolDefinition td;
    td.name = name;
    td.description = description;
    td.parameters = parameters;
    td.handler = std::move(handler);
    td.secure = secure;
    return td;
  }

  /// Get a parameter value with a default
  template <typename T>
  [[nodiscard]] T get_param(const json& params, const std::string& key,
                            const T& default_val) const {
    if (params.contains(key)) {
      return params[key].get<T>();
    }
    return default_val;
  }

  /// Get a string parameter with env var fallback
  [[nodiscard]] std::string get_param_or_env(const json& params, const std::string& key,
                                             const std::string& env_var,
                                             const std::string& default_val = "") const {
    if (params.contains(key) && params[key].is_string()) {
      return params[key].get<std::string>();
    }
    const char* env = std::getenv(env_var.c_str());
    if (env) {
      return std::string(env);
    }
    return default_val;
  }

 protected:
  /// The global_data namespace for this skill instance: ``skill:<prefix>`` when
  /// a ``prefix`` param is set, else ``skill:<instance_key>``. Protected — mirrors
  /// Python's private ``_get_skill_namespace`` (off the public surface).
  [[nodiscard]] std::string skill_namespace() const {
    if (params_.contains("prefix") && params_["prefix"].is_string()) {
      return "skill:" + params_["prefix"].get<std::string>();
    }
    return "skill:" + get_instance_key();
  }

  json params_;
};

/// Factory function type for creating skill instances
using SkillFactory = std::function<std::unique_ptr<SkillBase>()>;

}  // namespace skills
}  // namespace signalwire
