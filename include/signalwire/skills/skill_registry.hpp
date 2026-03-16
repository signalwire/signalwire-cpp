// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#pragma once

#include <string>
#include <map>
#include <functional>
#include <memory>
#include <mutex>
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

private:
    SkillRegistry() = default;
    SkillRegistry(const SkillRegistry&) = delete;
    SkillRegistry& operator=(const SkillRegistry&) = delete;

    mutable std::mutex mutex_;
    std::map<std::string, SkillFactory> factories_;
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

} // namespace skills
} // namespace signalwire
