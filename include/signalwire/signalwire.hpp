#pragma once

// SignalWire AI Agents SDK for C++
// Main umbrella header — includes all sub-headers.

#include "signalwire/logging.hpp"
#include "signalwire/swml/document.hpp"
#include "signalwire/swml/schema.hpp"
#include "signalwire/swml/service.hpp"
#include "signalwire/swaig/function_result.hpp"
#include "signalwire/swaig/tool_definition.hpp"
#include "signalwire/security/session_manager.hpp"
#include "signalwire/datamap/datamap.hpp"
#include "signalwire/contexts/contexts.hpp"
#include "signalwire/rest/rest_client.hpp"
#include "signalwire/skills/skill_base.hpp"
#include "signalwire/skills/skill_registry.hpp"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace signalwire {

/// Top-level convenience entry points — mirror Python's
/// ``signalwire/__init__.py`` package-level helpers (``RestClient``,
/// ``register_skill``, ``add_skill_directory``,
/// ``list_skills_with_params``).
///
/// The audit projects each free function onto the canonical Python
/// ``signalwire.<name>`` path. ``RestClient`` preserves PascalCase to
/// match Python's same-cased factory function name.

/// Construct a ``rest::RestClient`` from positional or keyword
/// credentials.
///
/// Mirrors Python's top-level ``signalwire.RestClient(*args, **kwargs)``
/// factory — a thin wrapper that lazy-imports
/// ``signalwire.rest.RestClient`` and instantiates it. Supports both
/// positional credentials (``args = {project, token, space}``) and
/// keyword credentials (``kwargs["project"]`` etc.) with
/// environment-variable fallback.
///
/// @throws std::invalid_argument when credentials cannot be derived
///         from either ``args``, ``kwargs``, or the standard
///         environment variables (``SIGNALWIRE_PROJECT_ID``,
///         ``SIGNALWIRE_API_TOKEN``, ``SIGNALWIRE_SPACE``).
[[nodiscard]] rest::RestClient RestClient(
    const std::vector<std::string>& args = {},
    const std::map<std::string, std::string>& kwargs = {});

/// Register a custom skill class with the global skill registry.
///
/// Mirrors Python's ``signalwire.register_skill(skill_class)``.
/// Delegates to ``skills::SkillRegistry::register_skill``. The skill's
/// name comes from the supplied ``skills::SkillBase`` factory (which
/// instantiates a SkillBase to read its ``skill_name()`` accessor).
void register_skill(skills::SkillFactory factory);

/// Add a directory to search for skills.
///
/// Mirrors Python's ``signalwire.add_skill_directory(path)`` —
/// delegates to the singleton ``skills::SkillRegistry`` instance so
/// third-party skill collections can be registered by path.
///
/// @throws std::invalid_argument when the path doesn't exist or
///         isn't a directory.
void add_skill_directory(const std::string& path);

/// Get complete schema for all available skills.
///
/// Mirrors Python's ``signalwire.list_skills_with_params()``. Returns
/// a map keyed by skill name where each value contains parameter
/// metadata. Useful for GUI configuration tools, API documentation,
/// or programmatic skill discovery.
///
/// C++ skills don't carry rich Python-style parameter introspection
/// in v1, so each entry contains the skill name and an empty parameter
/// map; built-in skills that expose ``parameter_schema()`` via
/// ``SkillBase`` get richer detail merged in.
[[nodiscard]] std::map<std::string, std::map<std::string, std::string>>
list_skills_with_params();

} // namespace signalwire
