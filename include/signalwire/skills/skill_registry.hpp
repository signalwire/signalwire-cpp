// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#pragma once

#include <sys/stat.h>

#include <cstdlib>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "signalwire/skills/skill_base.hpp"

namespace signalwire {
namespace skills {

/// Global registry of skill factories
class SkillRegistry {
 public:
  [[nodiscard]] static SkillRegistry& instance() {
    static SkillRegistry registry;
    return registry;
  }

  /// Register a skill factory
  void register_skill(const std::string& name, SkillFactory factory) {
    std::lock_guard<std::mutex> lock(mutex_);
    factories_[name] = std::move(factory);
  }

  /// Create a skill instance by name
  [[nodiscard]] std::unique_ptr<SkillBase> create(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = factories_.find(name);
    if (it != factories_.end()) {
      return it->second();
    }
    return nullptr;
  }

  /// Check if a skill is registered
  [[nodiscard]] bool has_skill(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return factories_.find(name) != factories_.end();
  }

  /// List all registered skill names
  [[nodiscard]] std::vector<std::string> list_skills() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> names;
    names.reserve(factories_.size());
    for (const auto& [k, v] : factories_) {
      names.push_back(k);
    }
    return names;
  }

  /// Add a directory to search for skills.
  ///
  /// Mirrors Python's
  /// ``signalwire.skills.registry.SkillRegistry.add_skill_directory``:
  /// validate that the path exists and is a directory, then append it
  /// (de-duplicated) to ``external_paths_``. Throws
  /// ``std::invalid_argument`` (the C++ analog of Python's ``ValueError``)
  /// for invalid input — the path doesn't exist or isn't a directory.
  void add_skill_directory(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    struct stat st;
    if (::stat(path.c_str(), &st) != 0) {
      throw std::invalid_argument("Skill directory does not exist: " + path);
    }
    if (!S_ISDIR(st.st_mode)) {
      throw std::invalid_argument("Path is not a directory: " + path);
    }
    for (const auto& existing : external_paths_) {
      if (existing == path) {
        return;  // already present, dedup
      }
    }
    external_paths_.push_back(path);
  }

  /// Look up a skill factory by name (Python:
  /// ``SkillRegistry.get_skill_class`` — returns the skill *type*). C++ has no
  /// first-class ``type`` object, so this returns whether the skill is known
  /// (the factory exists); use ``create`` to instantiate. Mirrors the
  /// discovery-by-name contract.
  [[nodiscard]] bool get_skill_class(const std::string& name) const { return has_skill(name); }

  /// Discover all registered skills as ``{name, ...}`` records (Python:
  /// ``SkillRegistry.discover_skills`` -> list of dicts). Each record carries
  /// the skill name and its instance-level schema where available.
  [[nodiscard]] nlohmann::json discover_skills() const {
    std::lock_guard<std::mutex> lock(mutex_);
    nlohmann::json out = nlohmann::json::array();
    for (const auto& [name, factory] : factories_) {
      nlohmann::json rec;
      rec["name"] = name;
      out.push_back(rec);
    }
    return out;
  }

  /// Return every registered skill's parameter schema keyed by skill name
  /// (Python: ``SkillRegistry.get_all_skills_schema`` -> dict[name -> schema]).
  [[nodiscard]] nlohmann::json get_all_skills_schema() const {
    std::lock_guard<std::mutex> lock(mutex_);
    nlohmann::json out = nlohmann::json::object();
    for (const auto& [name, factory] : factories_) {
      auto skill = factory();
      out[name] = skill ? skill->get_parameter_schema() : nlohmann::json::object();
    }
    return out;
  }

  /// Return the source (built-in vs external directory) each skill was loaded
  /// from (Python: ``SkillRegistry.list_all_skill_sources`` -> dict[source ->
  /// list of names]). Built-in factories are grouped under ``"builtin"``; the
  /// registered external directories are listed under ``"external"``.
  [[nodiscard]] nlohmann::json list_all_skill_sources() const {
    std::lock_guard<std::mutex> lock(mutex_);
    nlohmann::json out = nlohmann::json::object();
    nlohmann::json builtin = nlohmann::json::array();
    for (const auto& [name, factory] : factories_) {
      builtin.push_back(name);
    }
    out["builtin"] = builtin;
    // Effective external directories = registered ∪ SIGNALWIRE_SKILL_PATHS.
    std::vector<std::string> ext = external_paths_;
    for (const auto& env_path : env_skill_paths_locked()) {
      bool dup = false;
      for (const auto& existing : ext) {
        if (existing == env_path) {
          dup = true;
          break;
        }
      }
      if (!dup) {
        ext.push_back(env_path);
      }
    }
    out["external"] = ext;
    return out;
  }

  /// Returns the effective external skill directories: the ones registered via
  /// ``add_skill_directory`` PLUS any supplied through the
  /// ``SIGNALWIRE_SKILL_PATHS`` environment variable (colon-separated, deduped,
  /// registered paths first). Mirrors Python's ``SkillRegistry`` search order,
  /// which appends ``os.environ["SIGNALWIRE_SKILL_PATHS"]`` (split on
  /// ``os.pathsep``) to the registered ``_external_paths`` when resolving a
  /// skill by name.
  [[nodiscard]] std::vector<std::string> external_paths() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> paths = external_paths_;
    for (const auto& env_path : env_skill_paths_locked()) {
      bool dup = false;
      for (const auto& existing : paths) {
        if (existing == env_path) {
          dup = true;
          break;
        }
      }
      if (!dup) {
        paths.push_back(env_path);
      }
    }
    return paths;
  }

 private:
  /// Parse the ``SIGNALWIRE_SKILL_PATHS`` env var into a list of directories
  /// (colon-separated, empty entries dropped). Read on every call so a var set
  /// after construction still takes effect, matching Python's search-time read.
  [[nodiscard]] static std::vector<std::string> env_skill_paths_locked() {
    std::vector<std::string> out;
    const char* raw = std::getenv("SIGNALWIRE_SKILL_PATHS");
    if (raw == nullptr || *raw == '\0') {
      return out;
    }
    std::stringstream ss(raw);
    std::string item;
    while (std::getline(ss, item, ':')) {
      if (!item.empty()) {
        out.push_back(item);
      }
    }
    return out;
  }

  SkillRegistry() = default;
  SkillRegistry(const SkillRegistry&) = delete;
  SkillRegistry& operator=(const SkillRegistry&) = delete;

  mutable std::mutex mutex_;
  std::map<std::string, SkillFactory> factories_;
  std::vector<std::string> external_paths_;
};

/// Helper macro for skill auto-registration
#define REGISTER_SKILL(ClassName)                                                          \
  namespace {                                                                              \
  static bool _skill_##ClassName##_registered = []() {                                     \
    signalwire::skills::SkillRegistry::instance().register_skill(                          \
        ClassName().skill_name(), []() -> std::unique_ptr<signalwire::skills::SkillBase> { \
          return std::make_unique<ClassName>();                                            \
        });                                                                                \
    return true;                                                                           \
  }();                                                                                     \
  }

/// Ensure all built-in skills are registered. Called automatically but
/// can be called explicitly to force linkage.
void ensure_builtin_skills_registered();

}  // namespace skills
}  // namespace signalwire
