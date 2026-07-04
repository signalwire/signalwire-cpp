// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
//
// Configuration loader with environment-variable substitution.
//
// C++ port of the Python reference
// ``signalwire.core.config_loader.ConfigLoader`` (cross-checked against the
// Java ``com.signalwire.sdk.core.ConfigLoader``). Supports ``${VAR|default}``
// syntax for referencing environment variables within configuration files. The
// first existing, parseable file in the search paths wins.
//
// Idiom mapping: the C++ port parses JSON only — the vendored ``nlohmann::json``
// is a JSON library and the port carries no YAML dependency, so ``.yaml``/
// ``.yml`` files are NOT supported here (the Python/Java ports also default to
// JSON config files; the default search paths are all ``*.json``). After
// substitution, string values that look like booleans/integers/floats are
// coerced to those native JSON types (Python parity).
#pragma once

#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace signalwire {
namespace core {

using json = nlohmann::json;

class ConfigLoader {
 public:
  /// Initialize the config loader.
  /// @param config_paths Optional list of config file paths to check. When
  ///   absent the default search paths are used. The first existing,
  ///   parseable file wins.
  explicit ConfigLoader(const std::optional<std::vector<std::string>>& config_paths = std::nullopt);

  /// Check if a configuration was loaded.
  [[nodiscard]] bool has_config() const;

  /// Get the path of the loaded config file (empty optional if none).
  [[nodiscard]] std::optional<std::string> get_config_file() const;

  /// Get the raw configuration (before substitution). Returns an empty object
  /// when nothing loaded.
  [[nodiscard]] json get_config() const;

  /// Recursively substitute environment variables in configuration values.
  /// Supports ``${VAR|default}`` syntax. Throws ``std::invalid_argument`` when
  /// ``max_depth`` is exhausted.
  [[nodiscard]] json substitute_vars(const json& value, int max_depth = 10) const;

  /// Get a configuration value by dot-notation path (e.g.
  /// ``"security.ssl_enabled"``), with variables substituted. Returns
  /// ``default_value`` when the path is not found.
  [[nodiscard]] json get(const std::string& key_path,
                         const json& default_value = json(nullptr)) const;

  /// Get an entire configuration section with all variables substituted.
  /// Returns an empty object when the section is absent.
  [[nodiscard]] json get_section(const std::string& section) const;

  /// Merge configuration with environment variables. The config file takes
  /// precedence. Env vars beginning with ``env_prefix`` (default ``"SWML_"``)
  /// are lowercased, the prefix stripped, and folded into the result on
  /// underscore boundaries — only when not already present.
  [[nodiscard]] json merge_with_env(const std::string& env_prefix = "SWML_") const;

  /// Find a config file for a service. Returns the first existing file, or an
  /// empty optional.
  /// @param service_name    Optional service name seeding service-specific
  ///                         config file names.
  /// @param additional_paths Optional paths checked after the service-specific
  ///                         names.
  [[nodiscard]] static std::optional<std::string> find_config_file(
      const std::optional<std::string>& service_name = std::nullopt,
      const std::optional<std::vector<std::string>>& additional_paths = std::nullopt);

 private:
  static std::vector<std::string> default_paths();
  static std::string expand_home(const std::string& path);
  void load_config();
  json substitute_string(const std::string& value) const;
  static bool has_nested_key(const json& data, const std::string& key_path);
  static void set_nested_key(json& data, const std::string& key_path, const json& value);

  std::vector<std::string> config_paths_;
  std::optional<json> config_;
  std::optional<std::string> config_file_;
};

}  // namespace core
}  // namespace signalwire
