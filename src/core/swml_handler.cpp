// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT

#include "signalwire/core/swml_handler.hpp"

#include <stdexcept>

namespace signalwire {
namespace core {

namespace {
// Top-level AI keys that live outside the params object.
bool is_top_level_ai_key(const std::string& key) {
  return key == "languages" || key == "hints" || key == "pronounce" || key == "global_data";
}
}  // namespace

// ---------------------------------------------------------------------------
// AIVerbHandler
// ---------------------------------------------------------------------------

std::string AIVerbHandler::get_verb_name() const { return "ai"; }

VerbValidationResult AIVerbHandler::validate_config(const json& config) const {
  VerbValidationResult result;

  if (!config.is_object() || !config.contains("prompt")) {
    result.errors.emplace_back("Missing required field 'prompt'");
    result.valid = false;
    return result;
  }

  const json& prompt = config.at("prompt");
  if (!prompt.is_object()) {
    result.errors.emplace_back("'prompt' must be an object");
    result.valid = false;
    return result;
  }

  const bool has_text = prompt.contains("text");
  const bool has_pom = prompt.contains("pom");
  const int base_prompt_count = (has_text ? 1 : 0) + (has_pom ? 1 : 0);
  if (base_prompt_count == 0) {
    result.errors.emplace_back("'prompt' must contain either 'text' or 'pom' as base prompt");
  } else if (base_prompt_count > 1) {
    result.errors.emplace_back(
        "'prompt' can only contain one of: 'text' or 'pom' (mutually exclusive)");
  }

  if (prompt.contains("contexts") && !prompt.at("contexts").is_object()) {
    result.errors.emplace_back("'prompt.contexts' must be an object");
  }

  if (config.contains("SWAIG") && !config.at("SWAIG").is_object()) {
    result.errors.emplace_back("'SWAIG' must be an object");
  }

  result.valid = result.errors.empty();
  return result;
}

json AIVerbHandler::build_config(const json& kwargs) const {
  json args = kwargs.is_object() ? kwargs : json::object();

  auto take = [&args](const char* key) -> std::optional<json> {
    auto it = args.find(key);
    if (it == args.end() || it->is_null()) {
      return std::nullopt;
    }
    json v = *it;
    args.erase(key);
    return v;
  };

  std::optional<std::string> prompt_text;
  if (auto v = take("prompt_text")) {
    prompt_text = v->get<std::string>();
  }
  std::optional<json> prompt_pom = take("prompt_pom");
  std::optional<json> contexts = take("contexts");
  std::optional<std::string> post_prompt;
  if (auto v = take("post_prompt")) {
    post_prompt = v->get<std::string>();
  }
  std::optional<std::string> post_prompt_url;
  if (auto v = take("post_prompt_url")) {
    post_prompt_url = v->get<std::string>();
  }
  std::optional<json> swaig = take("swaig");

  return build_config(prompt_text, prompt_pom, contexts, post_prompt, post_prompt_url, swaig, args);
}

json AIVerbHandler::build_config(const std::optional<std::string>& prompt_text,
                                 const std::optional<json>& prompt_pom,
                                 const std::optional<json>& contexts,
                                 const std::optional<std::string>& post_prompt,
                                 const std::optional<std::string>& post_prompt_url,
                                 const std::optional<json>& swaig, const json& kwargs) const {
  const int base_prompt_count =
      (prompt_text.has_value() ? 1 : 0) + (prompt_pom.has_value() ? 1 : 0);
  if (base_prompt_count == 0) {
    throw std::invalid_argument("Either prompt_text or prompt_pom must be provided as base prompt");
  }
  if (base_prompt_count > 1) {
    throw std::invalid_argument("prompt_text and prompt_pom are mutually exclusive");
  }

  json config = json::object();

  json prompt_config = json::object();
  if (prompt_text.has_value()) {
    prompt_config["text"] = *prompt_text;
  } else if (prompt_pom.has_value()) {
    prompt_config["pom"] = *prompt_pom;
  }
  if (contexts.has_value()) {
    prompt_config["contexts"] = *contexts;
  }
  config["prompt"] = prompt_config;

  if (post_prompt.has_value()) {
    config["post_prompt"] = json::object({{"text", *post_prompt}});
  }
  if (post_prompt_url.has_value()) {
    config["post_prompt_url"] = *post_prompt_url;
  }
  if (swaig.has_value()) {
    config["SWAIG"] = *swaig;
  }

  // Match Python behaviour: always initialize the params map.
  config["params"] = json::object();

  if (kwargs.is_object()) {
    for (auto it = kwargs.begin(); it != kwargs.end(); ++it) {
      if (is_top_level_ai_key(it.key())) {
        config[it.key()] = it.value();
      } else {
        config["params"][it.key()] = it.value();
      }
    }
  }

  return config;
}

// ---------------------------------------------------------------------------
// VerbHandlerRegistry
// ---------------------------------------------------------------------------

VerbHandlerRegistry::VerbHandlerRegistry() { register_handler(std::make_shared<AIVerbHandler>()); }

void VerbHandlerRegistry::register_handler(std::shared_ptr<SWMLVerbHandler> handler) {
  handlers_[handler->get_verb_name()] = std::move(handler);
}

std::shared_ptr<SWMLVerbHandler> VerbHandlerRegistry::get_handler(
    const std::string& verb_name) const {
  auto it = handlers_.find(verb_name);
  return it != handlers_.end() ? it->second : nullptr;
}

bool VerbHandlerRegistry::has_handler(const std::string& verb_name) const {
  return handlers_.find(verb_name) != handlers_.end();
}

}  // namespace core
}  // namespace signalwire
