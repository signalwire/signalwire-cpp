// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#pragma once

#include <string>
#include <map>
#include <functional>
#include <memory>
#include <mutex>
#include <sys/stat.h>
#include <stdexcept>
#include "signalwire/skills/skill_base.hpp"

namespace signalwire {
namespace skills {

/// Global registry of skill factories
class SkillRegistry {
public:
    static SkillRegistry& instance() {
        static SkillRegistry registry;
        return registry;
    }

    /// Register a skill factory
    void register_skill(const std::string& name, SkillFactory factory) {
        std::lock_guard<std::mutex> lock(mutex_);
        factories_[name] = std::move(factory);
    }

    /// Create a skill instance by name
    std::unique_ptr<SkillBase> create(const std::string& name) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = factories_.find(name);
        if (it != factories_.end()) {
            return it->second();
        }
        return nullptr;
    }

    /// Check if a skill is registered
    bool has_skill(const std::string& name) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return factories_.find(name) != factories_.end();
    }

    /// List all registered skill names
    std::vector<std::string> list_skills() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::string> names;
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
            if (existing == path) return; // already present, dedup
        }
        external_paths_.push_back(path);
    }

    /// Returns the registered external skill directories.
    /// Mirrors Python's ``SkillRegistry._external_paths`` (private list,
    /// exposed here as a public accessor for parity-test inspection — C++
    /// has no convention for protected attributes that tests can poke).
    std::vector<std::string> external_paths() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return external_paths_;
    }

private:
    SkillRegistry() = default;
    SkillRegistry(const SkillRegistry&) = delete;
    SkillRegistry& operator=(const SkillRegistry&) = delete;

    mutable std::mutex mutex_;
    std::map<std::string, SkillFactory> factories_;
    std::vector<std::string> external_paths_;
};

/// Helper macro for skill auto-registration
#define REGISTER_SKILL(ClassName) \
    namespace { \
        static bool _skill_##ClassName##_registered = []() { \
            signalwire::skills::SkillRegistry::instance().register_skill( \
                ClassName().skill_name(), \
                []() -> std::unique_ptr<signalwire::skills::SkillBase> { \
                    return std::make_unique<ClassName>(); \
                }); \
            return true; \
        }(); \
    }

/// Ensure all built-in skills are registered. Called automatically but
/// can be called explicitly to force linkage.
void ensure_builtin_skills_registered();

} // namespace skills
} // namespace signalwire
