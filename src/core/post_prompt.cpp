// Copyright (c) 2026 SignalWire
// SPDX-License-Identifier: MIT
#include "signalwire/core/post_prompt.hpp"

#include <algorithm>
#include <cctype>

namespace signalwire {
namespace core {
namespace post_prompt {

namespace {

std::string trim(const std::string& s) {
  size_t b = 0;
  size_t e = s.size();
  while (b < e && std::isspace(static_cast<unsigned char>(s[b])) != 0) {
    ++b;
  }
  while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1])) != 0) {
    --e;
  }
  return s.substr(b, e - b);
}

// Python truthiness of a JSON value.
bool truthy(const json& v) {
  if (v.is_null()) {
    return false;
  }
  if (v.is_boolean()) {
    return v.get<bool>();
  }
  if (v.is_number_integer() || v.is_number_unsigned()) {
    return v.get<long long>() != 0;
  }
  if (v.is_number_float()) {
    return v.get<double>() != 0.0;
  }
  if (v.is_string()) {
    return !v.get_ref<const std::string&>().empty();
  }
  return !v.empty();
}

// The object inside a {"parsed": ...} wrapper: a bare object, or the first
// non-empty object in a list. Null when there is none.
json unwrap_parsed(const json& data) {
  auto it = data.find("parsed");
  if (it == data.end()) {
    return json();
  }
  if (it->is_object()) {
    return *it;
  }
  if (it->is_array()) {
    for (const auto& item : *it) {
      if (item.is_object() && !item.empty()) {
        return item;
      }
    }
  }
  return json();
}

// Python's str() of a JSON-decoded non-object value.
std::string py_str(const json& v) {
  if (v.is_string()) {
    return v.get<std::string>();
  }
  if (v.is_boolean()) {
    return v.get<bool>() ? "True" : "False";
  }
  if (v.is_null()) {
    return "None";
  }
  return v.dump();
}

}  // namespace

std::string strip_json_fence(const std::string& text) {
  std::string stripped = trim(text);
  if (stripped.rfind("```", 0) == 0) {
    // ^```[a-zA-Z]*\s*
    size_t i = 3;
    while (i < stripped.size() && std::isalpha(static_cast<unsigned char>(stripped[i])) != 0) {
      ++i;
    }
    while (i < stripped.size() && std::isspace(static_cast<unsigned char>(stripped[i])) != 0) {
      ++i;
    }
    stripped = stripped.substr(i);
    // \s*```$
    if (stripped.size() >= 3 && stripped.compare(stripped.size() - 3, 3, "```") == 0) {
      size_t end = stripped.size() - 3;
      while (end > 0 && std::isspace(static_cast<unsigned char>(stripped[end - 1])) != 0) {
        --end;
      }
      stripped = stripped.substr(0, end);
    }
  }
  return trim(stripped);
}

json parse_post_prompt_data(const json& data) {
  if (!data.is_object()) {
    return json::object();
  }
  json unwrapped = unwrap_parsed(data);
  if (unwrapped.is_object() && !unwrapped.empty()) {
    return unwrapped;
  }
  // Flat shape: real keys already present (anything but raw/parsed).
  json flat = json::object();
  for (auto it = data.begin(); it != data.end(); ++it) {
    if (it.key() != "raw" && it.key() != "parsed") {
      flat[it.key()] = it.value();
    }
  }
  if (!flat.empty()) {
    return flat;
  }
  auto rit = data.find("raw");
  if (rit == data.end() || !rit->is_string() || trim(rit->get<std::string>()).empty()) {
    return json::object();
  }
  std::string unfenced = strip_json_fence(rit->get<std::string>());
  json loaded = json::parse(unfenced, nullptr, /*allow_exceptions=*/false);
  if (loaded.is_discarded()) {
    // Prose instead of JSON. Still a summary.
    return json{{"summary", unfenced}};
  }
  if (loaded.is_object()) {
    return loaded;
  }
  return json{{"summary", py_str(loaded)}};
}

std::vector<std::map<std::string, std::string>> dialogue_turns(
    const json& call_log, const std::vector<std::string>& roles,
    const std::optional<std::string>& drop_echo) {
  std::vector<std::map<std::string, std::string>> out;
  if (!call_log.is_array()) {
    return out;
  }
  const std::string echo = drop_echo.has_value() ? trim(*drop_echo) : std::string();
  for (const auto& entry : call_log) {
    if (!entry.is_object()) {
      continue;
    }
    auto role_it = entry.find("role");
    if (role_it == entry.end() || !role_it->is_string()) {
      continue;
    }
    const std::string role = role_it->get<std::string>();
    if (std::find(roles.begin(), roles.end(), role) == roles.end()) {
      continue;
    }
    auto tc = entry.find("tool_calls");
    if (tc != entry.end() && truthy(*tc)) {
      continue;
    }
    auto cit = entry.find("content");
    if (cit == entry.end() || !cit->is_string()) {
      continue;
    }
    const std::string content = cit->get<std::string>();
    if (trim(content).empty()) {
      continue;
    }
    if (!echo.empty() && trim(content) == echo) {
      continue;
    }
    out.push_back({{"role", role}, {"content", content}});
  }
  return out;
}

NormalizedPostPrompt normalize_post_prompt(const json& body) {
  NormalizedPostPrompt result;
  if (!body.is_object()) {
    return result;
  }
  const json ppd = body.contains("post_prompt_data") ? body["post_prompt_data"] : json();
  result.summary = parse_post_prompt_data(ppd);
  // The echo is compared against the RAW string the engine returned, not the
  // parsed summary -- the assistant turn carries the fence too.
  std::optional<std::string> raw_summary;
  if (ppd.is_object() && ppd.contains("raw") && ppd["raw"].is_string() &&
      !ppd["raw"].get<std::string>().empty()) {
    raw_summary = ppd["raw"].get<std::string>();
  }
  json log = json::array();
  for (const char* key : {"call_log", "raw_call_log", "raw_messages"}) {
    if (body.contains(key) && truthy(body[key])) {
      log = body[key];
      break;
    }
  }
  if (body.contains("conversation_type") && truthy(body["conversation_type"])) {
    result.medium = py_str(body["conversation_type"]);
  }
  if (body.contains("conversation_id") && truthy(body["conversation_id"])) {
    result.conversation_id = py_str(body["conversation_id"]);
  }
  result.dialogue = dialogue_turns(log, dialogue_roles(), raw_summary);
  if (body.contains("call_id") && truthy(body["call_id"])) {
    result.call_id = py_str(body["call_id"]);
  }
  result.raw = body;
  return result;
}

}  // namespace post_prompt
}  // namespace core
}  // namespace signalwire
