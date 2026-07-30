#pragma once

// SignalWire AI Agents SDK for C++
// Main umbrella header — includes all sub-headers.

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "signalwire/agent/agent_base.hpp"
#include "signalwire/ai_chat/ai_chat_client.hpp"
#include "signalwire/contexts/contexts.hpp"
#include "signalwire/datamap/datamap.hpp"
#include "signalwire/logging.hpp"
#include "signalwire/rest/rest_client.hpp"
#include "signalwire/security/session_manager.hpp"
#include "signalwire/server/agent_server.hpp"
#include "signalwire/skills/skill_base.hpp"
#include "signalwire/skills/skill_registry.hpp"
#include "signalwire/swaig/function_result.hpp"
#include "signalwire/swaig/tool_definition.hpp"
#include "signalwire/swml/document.hpp"
#include "signalwire/swml/schema.hpp"
#include "signalwire/swml/service.hpp"

namespace signalwire {

/// Top-level convenience entry points in namespace ``signalwire`` —
/// ``RestClient``, ``register_skill``, ``add_skill_directory``,
/// ``list_skills``, and ``list_skills_with_params``. ``RestClient`` is
/// deliberately PascalCase: it is a factory function, not a type.

/// Construct a ``rest::RestClient`` from positional or keyword
/// credentials.
///
/// A thin factory over ``rest::RestClient``. Supports both positional
/// credentials (``args = {project, token, space}``) and keyword
/// credentials (``kwargs["project"]`` etc.) with environment-variable
/// fallback.
///
/// @throws std::invalid_argument when credentials cannot be derived
///         from either ``args``, ``kwargs``, or the standard
///         environment variables (``SIGNALWIRE_PROJECT_ID``,
///         ``SIGNALWIRE_API_TOKEN``, ``SIGNALWIRE_SPACE``).
[[nodiscard]] rest::RestClient RestClient(const std::vector<std::string>& args = {},
                                          const std::map<std::string, std::string>& kwargs = {});

/// Register a custom skill class with the global skill registry.
///
/// Delegates to ``skills::SkillRegistry::register_skill``. The skill's
/// name comes from the supplied ``skills::SkillBase`` factory (which
/// instantiates a SkillBase to read its ``skill_name()`` accessor).
void register_skill(skills::SkillFactory factory);

/// Add a directory to search for skills.
///
/// Delegates to the singleton ``skills::SkillRegistry`` instance so
/// third-party skill collections can be registered by path.
///
/// @throws std::invalid_argument when the path doesn't exist or
///         isn't a directory.
void add_skill_directory(const std::string& path);

/// Get complete schema for all available skills.
///
/// Returns a map keyed by skill name where each value contains parameter
/// metadata. Useful for GUI configuration tools, API documentation,
/// or programmatic skill discovery.
///
/// There is no runtime parameter introspection in v1, so an entry is the
/// skill name plus an empty parameter map by default; built-in skills that
/// expose ``parameter_schema()`` via ``SkillBase`` get richer detail merged
/// in.
[[nodiscard]] std::map<std::string, std::map<std::string, std::string>> list_skills_with_params();

/// List all available skills with lightweight metadata.
///
/// One record per registered skill (name plus description/version where the
/// factory can be instantiated). The lighter summary counterpart to
/// ``list_skills_with_params()``; both delegate to the singleton
/// ``skills::SkillRegistry``.
[[nodiscard]] std::vector<std::map<std::string, std::string>> list_skills();

}  // namespace signalwire
