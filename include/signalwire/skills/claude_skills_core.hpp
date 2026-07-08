// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#pragma once

// Shared core for the claude_skills skill's SKILL.md discovery + tool building.
//
// Ports Python signalwire/skills/claude_skills/skill.py: each immediate
// subdirectory of skills_path that contains a SKILL.md is discovered, its YAML
// frontmatter (name/description) parsed, and one SWAIG tool declared per skill
// (name = {tool_prefix}{sanitized-name}, description from the frontmatter,
// handler returns the SKILL.md body). NATIVE EXECUTION of skill scripts is
// impossible in this AOT port, so the port discovers + declares the tools and
// serves their instructions; it does not run embedded code.
//
// This header is included by BOTH claude_skills implementations in the tree —
// the registered `ClaudeSkillsSkillR` in skill_registry.cpp and the
// `ClaudeSkillsSkill` in builtin/claude_skills.cpp — so the discovery logic
// lives in exactly one place and the two can never drift. (The builtin's
// REGISTER_SKILL is dead-stripped from the static archive; ClaudeSkillsSkillR
// is what actually runs. Both delegate here.)

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>
#include <vector>

#include "signalwire/swaig/function_result.hpp"
#include "signalwire/swaig/tool_definition.hpp"

namespace signalwire {
namespace skills {
namespace claude_core {

namespace fs = std::filesystem;
using nlohmann::json;

/// A single discovered SKILL.md: the frontmatter name/description plus the
/// markdown body the tool hands back to the model.
struct DiscoveredSkill {
  std::string name;         ///< frontmatter ``name`` (dir name as fallback)
  std::string description;  ///< frontmatter ``description``
  std::string body;         ///< markdown body after the frontmatter
};

inline std::string strip(const std::string& s) {
  size_t b = s.find_first_not_of(" \t\r\n");
  if (b == std::string::npos) {
    return "";
  }
  size_t e = s.find_last_not_of(" \t\r\n");
  return s.substr(b, e - b + 1);
}

/// Unwrap a YAML scalar's surrounding matched quotes, if any.
inline std::string unquote(const std::string& s) {
  if (s.size() >= 2 &&
      ((s.front() == '"' && s.back() == '"') || (s.front() == '\'' && s.back() == '\''))) {
    return s.substr(1, s.size() - 2);
  }
  return s;
}

/// Parse a SKILL.md file: split the leading ``---``-fenced YAML frontmatter
/// from the body and pull the ``name``/``description`` scalars. Frontmatter is
/// flat ``key: value`` (matching the Claude SKILL.md shape), so a line scanner
/// is sufficient — no full YAML engine needed. Returns false if the file can't
/// be read.
inline bool parse_skill_md(const fs::path& path, DiscoveredSkill& out) {
  std::ifstream in(path);
  if (!in) {
    return false;
  }
  std::stringstream ss;
  ss << in.rdbuf();
  std::string content = ss.str();

  std::string trimmed = strip(content);
  if (trimmed.rfind("---", 0) != 0) {
    // No frontmatter — whole file is the body.
    out.body = trimmed;
    return true;
  }

  // Locate the closing fence after the opening "---".
  size_t body_start = 0;
  std::string frontmatter;
  {
    size_t first_nl = content.find('\n');
    size_t search_from = (first_nl == std::string::npos) ? content.size() : first_nl + 1;
    size_t close = content.find("\n---", search_from);
    if (close == std::string::npos) {
      // Malformed frontmatter — treat the whole thing as body.
      out.body = trimmed;
      return true;
    }
    frontmatter = content.substr(search_from, close - search_from);
    size_t after = content.find('\n', close + 1);
    body_start = (after == std::string::npos) ? content.size() : after + 1;
  }
  out.body = strip(content.substr(body_start));

  // Scan flat "key: value" frontmatter lines for name/description.
  std::istringstream fs_in(frontmatter);
  std::string line;
  while (std::getline(fs_in, line)) {
    std::string t = strip(line);
    if (t.empty() || t[0] == '#') {
      continue;
    }
    size_t colon = t.find(':');
    if (colon == std::string::npos) {
      continue;
    }
    std::string key = strip(t.substr(0, colon));
    std::string val = unquote(strip(t.substr(colon + 1)));
    if (key == "name") {
      out.name = val;
    } else if (key == "description") {
      out.description = val;
    }
  }
  return true;
}

/// Sanitize a skill name into a SWAIG-safe tool suffix (lowercase, non
/// [a-z0-9_] -> '_'), mirroring the reference's ``_sanitize_tool_name``.
inline std::string sanitize_tool_name(const std::string& name) {
  std::string out;
  out.reserve(name.size());
  for (char c : name) {
    unsigned char uc = static_cast<unsigned char>(c);
    if (std::isalnum(uc)) {
      out.push_back(static_cast<char>(std::tolower(uc)));
    } else {
      out.push_back('_');
    }
  }
  return out;
}

/// Walk ``path``'s immediate subdirectories; each that contains a SKILL.md is
/// parsed into one DiscoveredSkill (dir name as the fallback skill name). A
/// missing/unreadable directory yields an empty list (no skills discovered).
inline std::vector<DiscoveredSkill> discover_skills(const std::string& path) {
  std::vector<DiscoveredSkill> out;
  std::error_code ec;
  fs::path root(path);
  if (!fs::is_directory(root, ec)) {
    return out;
  }
  for (const auto& entry : fs::directory_iterator(root, ec)) {
    if (ec) {
      break;
    }
    std::error_code ec2;
    if (!entry.is_directory(ec2)) {
      continue;
    }
    fs::path skill_file = entry.path() / "SKILL.md";
    if (!fs::exists(skill_file, ec2)) {
      continue;
    }
    DiscoveredSkill skill;
    if (!parse_skill_md(skill_file, skill)) {
      continue;
    }
    if (skill.name.empty()) {
      skill.name = entry.path().filename().string();  // dir-name fallback
    }
    out.push_back(std::move(skill));
  }
  return out;
}

/// Build one SWAIG ToolDefinition for a discovered skill. tool_prefix defaults
/// to "claude_". The handler returns the SKILL.md body with the caller's
/// ``arguments`` appended (native execution is impossible in AOT).
inline swaig::ToolDefinition build_tool(const DiscoveredSkill& skill,
                                        const std::string& tool_prefix) {
  std::string tool_name = tool_prefix + sanitize_tool_name(skill.name);
  std::string description =
      !skill.description.empty() ? skill.description : ("Use the " + skill.name + " skill");

  json parameters = json::object(
      {{"type", "object"},
       {"properties",
        json::object(
            {{"arguments",
              json::object({{"type", "string"},
                            {"description", "Arguments or context to pass to the skill"}})}})},
       {"required", json::array({"arguments"})}});

  std::string body = skill.body;
  swaig::ToolDefinition td;
  td.name = tool_name;
  td.description = description;
  td.parameters = parameters;
  td.handler = [body](const json& args, const json&) -> swaig::FunctionResult {
    std::string arguments = args.value("arguments", "");
    std::string content = body;
    if (!arguments.empty()) {
      content += "\n\n" + arguments;
    }
    return swaig::FunctionResult(content);
  };
  return td;
}

/// Speech hints from the discovered skill names (whitespace-split words > 2
/// chars).
inline std::vector<std::string> hints_from(const std::vector<DiscoveredSkill>& skills) {
  std::vector<std::string> hints;
  for (const auto& skill : skills) {
    std::istringstream ws(skill.name);
    std::string word;
    while (ws >> word) {
      if (word.size() > 2) {
        hints.push_back(word);
      }
    }
  }
  return hints;
}

}  // namespace claude_core
}  // namespace skills
}  // namespace signalwire
