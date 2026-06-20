// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
//
// Top-level convenience entry points — implementation. Mirrors Python's
// ``signalwire/__init__.py`` package-level helpers (``RestClient``,
// ``register_skill``, ``add_skill_directory``,
// ``list_skills_with_params``).

#include "signalwire/signalwire.hpp"

#include <cstdlib>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "signalwire/rest/rest_client.hpp"
#include "signalwire/skills/skill_base.hpp"
#include "signalwire/skills/skill_registry.hpp"

namespace signalwire {

namespace {

// Helper: get an env var or empty string.
std::string env_or_empty(const char* name) {
  const char* v = std::getenv(name);
  return v ? std::string(v) : std::string();
}

// Helper: lookup in kwargs with a list of candidate keys; first hit wins.
std::string lookup(const std::map<std::string, std::string>& kwargs,
                   std::initializer_list<const char*> keys) {
  for (const char* k : keys) {
    auto it = kwargs.find(k);
    if (it != kwargs.end() && !it->second.empty()) {
      return it->second;
    }
  }
  return "";
}

}  // namespace

rest::RestClient RestClient(const std::vector<std::string>& args,
                            const std::map<std::string, std::string>& kwargs) {
  std::string project, token, space;

  if (!args.empty()) {
    project = args[0];
  } else {
    project = lookup(kwargs, {"project", "project_id"});
    if (project.empty()) { project = env_or_empty("SIGNALWIRE_PROJECT_ID");
}
  }
  if (args.size() > 1) {
    token = args[1];
  } else {
    token = lookup(kwargs, {"token"});
    if (token.empty()) { token = env_or_empty("SIGNALWIRE_API_TOKEN");
}
  }
  if (args.size() > 2) {
    space = args[2];
  } else {
    space = lookup(kwargs, {"space", "host"});
    if (space.empty()) { space = env_or_empty("SIGNALWIRE_SPACE");
}
  }

  if (project.empty() || token.empty() || space.empty()) {
    throw std::invalid_argument(
        "project, token, and space are required. "
        "Provide them as args/kwargs or set SIGNALWIRE_PROJECT_ID, "
        "SIGNALWIRE_API_TOKEN, and SIGNALWIRE_SPACE environment variables.");
  }
  return rest::RestClient(space, project, token);
}

void register_skill(skills::SkillFactory factory) {
  if (!factory) {
    throw std::invalid_argument("factory is required");
  }
  // Construct a temporary instance to read its skill_name(). Mirrors
  // the Python adapter's pattern of instantiating once to derive the
  // registration key.
  std::unique_ptr<skills::SkillBase> probe = factory();
  if (!probe) {
    throw std::invalid_argument("factory returned a null skill instance");
  }
  std::string name = probe->skill_name();
  skills::SkillRegistry::instance().register_skill(name, std::move(factory));
}

void add_skill_directory(const std::string& path) {
  skills::SkillRegistry::instance().add_skill_directory(path);
}

std::map<std::string, std::map<std::string, std::string>> list_skills_with_params() {
  std::map<std::string, std::map<std::string, std::string>> out;
  auto& reg = skills::SkillRegistry::instance();
  for (const auto& name : reg.list_skills()) {
    std::map<std::string, std::string> entry;
    entry["name"] = name;
    // C++ skills don't expose a uniform parameter_schema() accessor;
    // surface the description / version when the factory can be
    // instantiated successfully.
    try {
      auto skill = reg.create(name);
      if (skill) {
        entry["description"] = skill->skill_description();
        entry["version"] = skill->skill_version();
      }
    } catch (const std::exception& e) {
      // Fall back to the minimal entry on construction failure.
      static_cast<void>(e);
    }
    out[name] = entry;
  }
  return out;
}

}  // namespace signalwire
