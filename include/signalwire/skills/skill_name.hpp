// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#pragma once

#include <string>

namespace signalwire {
namespace skills {

/// Built-in skill names as a typed, compile-time-checked closed set.
///
/// `AgentBase::add_skill()` (and `remove_skill()` / `has_skill()`) accept this
/// `enum class` OR a `std::string`. The enum gives editor autocompletion and
/// makes a typo fail at the call site — a bare string like `"datetiem"` only
/// fails at runtime, on the server. The string overload keeps parity with the
/// Python reference (which uses a bare `str`) and still allows custom /
/// third-party skills that aren't built in.
///
///     agent.add_skill(SkillName::Datetime);   // typed, autocompleted
///     agent.add_skill("datetime");            // string still works (parity)
///     agent.add_skill("my_custom_skill");     // open set: custom skills ok
///
/// Members mirror the 18 built-in skills' registered `skill_name()` values
/// (the canonical wire strings). `skill_name_value()` maps each member to that
/// wire string, so the enum and string overloads load the identical skill.
enum class SkillName {
  ApiNinjasTrivia,
  ClaudeSkills,
  CustomSkills,
  Datasphere,
  DatasphereServerless,
  Datetime,
  GoogleMaps,
  InfoGatherer,
  Joke,
  Math,
  McpGateway,
  NativeVectorSearch,
  PlayBackgroundFile,
  Spider,
  SwmlTransfer,
  WeatherApi,
  WebSearch,
  WikipediaSearch,
};

/// Map a `SkillName` to its canonical wire string (the value a built-in
/// skill reports from `skill_name()`). This is the single normalization
/// point shared by the typed `add_skill`/`remove_skill`/`has_skill`
/// overloads, so their behavior is identical to passing the bare string.
[[nodiscard]] inline std::string skill_name_value(SkillName name) {
  switch (name) {
    case SkillName::ApiNinjasTrivia:
      return "api_ninjas_trivia";
    case SkillName::ClaudeSkills:
      return "claude_skills";
    case SkillName::CustomSkills:
      return "custom_skills";
    case SkillName::Datasphere:
      return "datasphere";
    case SkillName::DatasphereServerless:
      return "datasphere_serverless";
    case SkillName::Datetime:
      return "datetime";
    case SkillName::GoogleMaps:
      return "google_maps";
    case SkillName::InfoGatherer:
      return "info_gatherer";
    case SkillName::Joke:
      return "joke";
    case SkillName::Math:
      return "math";
    case SkillName::McpGateway:
      return "mcp_gateway";
    case SkillName::NativeVectorSearch:
      return "native_vector_search";
    case SkillName::PlayBackgroundFile:
      return "play_background_file";
    case SkillName::Spider:
      return "spider";
    case SkillName::SwmlTransfer:
      return "swml_transfer";
    case SkillName::WeatherApi:
      return "weather_api";
    case SkillName::WebSearch:
      return "web_search";
    case SkillName::WikipediaSearch:
      return "wikipedia_search";
  }
  return "";  // unreachable for a valid enumerator; keeps the compiler quiet
}

/// `to_string` overload so `SkillName` interoperates with ADL-based
/// stringification the same way `skill_name_value()` does.
[[nodiscard]] inline std::string to_string(SkillName name) { return skill_name_value(name); }

}  // namespace skills
}  // namespace signalwire
