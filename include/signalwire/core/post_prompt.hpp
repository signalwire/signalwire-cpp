// Copyright (c) 2026 SignalWire
// SPDX-License-Identifier: MIT
//
// Post-prompt normalization.
//
// One conversation can run over voice and over text chat, and both engines
// produce "the post-prompt" -- in different shapes:
//
//   field              voice                       chat
//   app_name           "swml app"                  "ai_chat"
//   conversation_id    absent                      present at top level
//   full log           raw_call_log                raw_messages
//   summary arrives    summarize_conversation      a bare role: assistant turn
//                      tool call                   inside call_log
//   post_prompt_data   parsed object               {"raw": "```json ...```"}
//
// plus a third post_prompt_data shape from the voice engine,
// {"parsed": [ {...} ], "raw": "..."}. These functions absorb the divergence so
// an application sees one artifact whichever engine finished the conversation.
// They never throw: the conversation is already over, so a malformed summary
// degrades rather than failing the request that delivered it. Parsing is
// schema-agnostic -- the summary is returned as found.
#pragma once

#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace signalwire {
namespace core {
namespace post_prompt {

using json = nlohmann::json;

/// Roles that are actual dialogue. Everything else in a call log is machinery.
inline const std::vector<std::string>& dialogue_roles() {
  static const std::vector<std::string> roles = {"user", "assistant"};
  return roles;
}

/// One finished conversation leg, in a shape that does not vary by engine.
struct NormalizedPostPrompt {
  /// ``conversation_type`` as reported (e.g. "voice" or "chat"); empty when the
  /// engine did not say.
  std::string medium;
  /// Present on chat, absent on voice.
  std::optional<std::string> conversation_id;
  /// The parsed ``post_prompt_data``, whatever keys the application's
  /// post-prompt asked for; ``{}`` when there was none. Prose instead of JSON
  /// yields ``{"summary": "<the prose>"}``.
  json summary = json::object();
  /// ``user``/``assistant`` turns only, tool calls and the chat engine's summary
  /// echo removed: ``[{"role": ..., "content": ...}, ...]``.
  std::vector<std::map<std::string, std::string>> dialogue;
  /// The platform call id, when present.
  std::optional<std::string> call_id;
  /// The complete request body, untouched.
  json raw = json::object();
};

/// Unwrap ```` ```json ... ``` ```` fencing (the chat engine hands the model's
/// answer back verbatim, fence and all).
[[nodiscard]] std::string strip_json_fence(const std::string& text);

/// Return ``post_prompt_data`` as a plain object, whichever shape it arrived in
/// (flat keys, fenced ``raw``, or the ``{"parsed": [...]}`` wrapper); ``{}`` when
/// there is nothing usable. Never throws.
[[nodiscard]] json parse_post_prompt_data(const json& data);

/// Extract the real dialogue from a call log: keep ``roles``, drop entries
/// carrying ``tool_calls``, empty content, and -- when ``drop_echo`` is given --
/// the entry whose content is exactly that text (the chat engine's summary echo).
[[nodiscard]] std::vector<std::map<std::string, std::string>> dialogue_turns(
    const json& call_log, const std::vector<std::string>& roles = dialogue_roles(),
    const std::optional<std::string>& drop_echo = std::nullopt);

/// Normalize a post-prompt body from either engine. Never throws; a body this
/// cannot make sense of yields one with empty fields.
[[nodiscard]] NormalizedPostPrompt normalize_post_prompt(const json& body);

}  // namespace post_prompt
}  // namespace core
}  // namespace signalwire
