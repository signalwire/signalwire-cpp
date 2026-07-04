// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT

#include "signalwire/core/config_loader.hpp"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <regex>
#include <sstream>

#include "signalwire/common.hpp"
#include "signalwire/logging.hpp"

// POSIX environ, for merge_with_env's "iterate all env vars" behavior.
extern char** environ;

namespace signalwire {
namespace core {

namespace {

bool file_exists(const std::string& path) {
  std::ifstream f(path);
  return f.good();
}

std::string to_lower(const std::string& s) {
  std::string out = s;
  for (char& c : out) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return out;
}

// Pattern matching ${VAR} or ${VAR|default}. Mirrors the Python
// r"\$\{([^}|]+)(?:\|([^}]*))?\}".
const std::regex& var_pattern() {
  static const std::regex re(R"(\$\{([^}|]+)(?:\|([^}]*))?\})");
  return re;
}

bool is_all_digits(const std::string& s) {
  if (s.empty()) {
    return false;
  }
  for (char c : s) {
    if (!std::isdigit(static_cast<unsigned char>(c))) {
      return false;
    }
  }
  return true;
}

// Python: result.replace(".", "", 1).isdigit() — exactly one '.' removed, and
// what remains is all digits (and non-empty).
bool is_single_dot_float(const std::string& s) {
  auto dot = s.find('.');
  if (dot == std::string::npos) {
    return false;
  }
  std::string stripped = s.substr(0, dot) + s.substr(dot + 1);
  return is_all_digits(stripped);
}

}  // namespace

ConfigLoader::ConfigLoader(const std::optional<std::vector<std::string>>& config_paths) {
  config_paths_ = config_paths.has_value() ? *config_paths : default_paths();
  load_config();
}

std::vector<std::string> ConfigLoader::default_paths() {
  return {
      "config.json",
      "agent_config.json",
      "swml_config.json",
      ".swml/config.json",
      expand_home("~/.swml/config.json"),
      "/etc/swml/config.json",
  };
}

std::string ConfigLoader::expand_home(const std::string& path) {
  if (!path.empty() && path[0] == '~') {
    std::string home = get_env("HOME");
    return home + path.substr(1);
  }
  return path;
}

void ConfigLoader::load_config() {
  for (const auto& path : config_paths_) {
    if (!file_exists(path)) {
      continue;
    }
    try {
      std::ifstream f(path);
      json parsed = json::parse(f);
      config_ = parsed;
      config_file_ = path;
      Logger::instance().info("config_loaded path=" + path);
      break;
    } catch (const std::exception& e) {
      Logger::instance().error("config_load_error path=" + path + " error=" + e.what());
    }
  }
}

bool ConfigLoader::has_config() const { return config_.has_value(); }

std::optional<std::string> ConfigLoader::get_config_file() const { return config_file_; }

json ConfigLoader::get_config() const { return config_.has_value() ? *config_ : json::object(); }

json ConfigLoader::substitute_string(const std::string& value) const {
  const std::regex& re = var_pattern();
  std::string result;
  auto begin = std::sregex_iterator(value.begin(), value.end(), re);
  auto end = std::sregex_iterator();
  std::size_t last = 0;
  for (auto it = begin; it != end; ++it) {
    const std::smatch& m = *it;
    result.append(value, last, static_cast<std::size_t>(m.position()) - last);
    std::string var_name = m[1].str();
    std::string def = m[2].matched ? m[2].str() : "";
    const char* env = std::getenv(var_name.c_str());
    result.append(env != nullptr ? std::string(env) : def);
    last = static_cast<std::size_t>(m.position()) + static_cast<std::size_t>(m.length());
  }
  result.append(value, last, value.size() - last);

  // Coerce to native JSON types (Python parity).
  std::string lowered = to_lower(result);
  if (lowered == "true") {
    return json(true);
  }
  if (lowered == "false") {
    return json(false);
  }
  if (is_all_digits(result)) {
    try {
      return json(static_cast<long long>(std::stoll(result)));
    } catch (const std::exception&) {
      return json(result);
    }
  }
  if (is_single_dot_float(result)) {
    try {
      return json(std::stod(result));
    } catch (const std::exception&) {
      return json(result);
    }
  }
  return json(result);
}

json ConfigLoader::substitute_vars(const json& value, int max_depth) const {
  if (max_depth <= 0) {
    throw std::invalid_argument("Maximum variable substitution depth exceeded");
  }
  if (value.is_string()) {
    return substitute_string(value.get<std::string>());
  }
  if (value.is_object()) {
    json out = json::object();
    for (auto it = value.begin(); it != value.end(); ++it) {
      out[it.key()] = substitute_vars(it.value(), max_depth - 1);
    }
    return out;
  }
  if (value.is_array()) {
    json out = json::array();
    for (const auto& item : value) {
      out.push_back(substitute_vars(item, max_depth - 1));
    }
    return out;
  }
  return value;
}

json ConfigLoader::get(const std::string& key_path, const json& default_value) const {
  if (!config_.has_value()) {
    return default_value;
  }
  const json* value = &(*config_);
  std::stringstream ss(key_path);
  std::string key;
  while (std::getline(ss, key, '.')) {
    if (value->is_object() && value->contains(key)) {
      value = &((*value)[key]);
    } else {
      return default_value;
    }
  }
  return substitute_vars(*value);
}

json ConfigLoader::get_section(const std::string& section) const {
  if (!config_.has_value() || !config_->is_object() || !config_->contains(section)) {
    return json::object();
  }
  json substituted = substitute_vars((*config_)[section]);
  if (substituted.is_object()) {
    return substituted;
  }
  return json::object();
}

json ConfigLoader::merge_with_env(const std::string& env_prefix) const {
  json result = config_.has_value() ? substitute_vars(*config_) : json::object();
  if (!result.is_object()) {
    result = json::object();
  }
  for (char** e = environ; e != nullptr && *e != nullptr; ++e) {
    std::string entry(*e);
    auto eq = entry.find('=');
    if (eq == std::string::npos) {
      continue;
    }
    std::string key = entry.substr(0, eq);
    std::string value = entry.substr(eq + 1);
    if (key.rfind(env_prefix, 0) != 0) {
      continue;
    }
    std::string config_key = to_lower(key.substr(env_prefix.size()));
    if (!has_nested_key(result, config_key)) {
      set_nested_key(result, config_key, json(value));
    }
  }
  return result;
}

bool ConfigLoader::has_nested_key(const json& data, const std::string& key_path) {
  const json* current = &data;
  std::stringstream ss(key_path);
  std::string key;
  while (std::getline(ss, key, '_')) {
    if (current->is_object() && current->contains(key)) {
      current = &((*current)[key]);
    } else {
      return false;
    }
  }
  return true;
}

void ConfigLoader::set_nested_key(json& data, const std::string& key_path, const json& value) {
  std::vector<std::string> keys;
  std::stringstream ss(key_path);
  std::string key;
  while (std::getline(ss, key, '_')) {
    keys.push_back(key);
  }
  if (keys.empty()) {
    return;
  }
  json* current = &data;
  for (std::size_t i = 0; i + 1 < keys.size(); ++i) {
    if (!current->is_object() || !current->contains(keys[i]) || !(*current)[keys[i]].is_object()) {
      (*current)[keys[i]] = json::object();
    }
    current = &((*current)[keys[i]]);
  }
  (*current)[keys.back()] = value;
}

std::optional<std::string> ConfigLoader::find_config_file(
    const std::optional<std::string>& service_name,
    const std::optional<std::vector<std::string>>& additional_paths) {
  std::vector<std::string> paths;
  if (service_name.has_value()) {
    paths.push_back(*service_name + "_config.json");
    paths.push_back(".swml/" + *service_name + "_config.json");
  }
  if (additional_paths.has_value()) {
    for (const auto& p : *additional_paths) {
      paths.push_back(p);
    }
  }
  paths.push_back("config.json");
  paths.push_back("agent_config.json");
  paths.push_back(".swml/config.json");
  paths.push_back(expand_home("~/.swml/config.json"));
  paths.push_back("/etc/swml/config.json");
  for (const auto& path : paths) {
    if (file_exists(path)) {
      return path;
    }
  }
  return std::nullopt;
}

}  // namespace core
}  // namespace signalwire
