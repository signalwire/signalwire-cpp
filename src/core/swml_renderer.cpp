// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT

#include "signalwire/core/swml_renderer.hpp"

#include <algorithm>
#include <cctype>

#include "signalwire/core/swml_builder.hpp"
#include "signalwire/pom/pom.hpp"

namespace signalwire {
namespace core {

namespace {

std::string to_lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

bool is_hook_function(const json& func) {
  if (!func.is_object()) {
    return false;
  }
  auto it = func.find("function");
  if (it == func.end() || !it->is_string()) {
    return false;
  }
  const std::string name = it->get<std::string>();
  return name == "startup_hook" || name == "hangup_hook";
}

json hook_function(const std::string& name, const std::string& description,
                   const std::string& url) {
  return json::object(
      {{"function", name},
       {"description", description},
       {"parameters", json::object({{"type", "object"}, {"properties", json::object()}})},
       {"web_hook_url", url}});
}

}  // namespace

std::string SwmlRenderer::render_swml(const json& prompt, swml::Service& service,
                                      const RenderOptions& opts) {
  SWMLBuilder builder(service);
  builder.reset();

  if (opts.add_answer) {
    builder.answer();
  }

  if (opts.record_call) {
    service.document().add_verb("record_call", json::object({{"format", opts.record_format},
                                                             {"stereo", opts.record_stereo}}));
  }

  // Assemble the SWAIG function list: startup/hangup hooks first, then the
  // caller's functions (deduping the special hooks).
  std::vector<json> functions;
  const bool has_startup = opts.startup_hook_url.has_value() && !opts.startup_hook_url->empty();
  const bool has_hangup = opts.hangup_hook_url.has_value() && !opts.hangup_hook_url->empty();
  if (has_startup) {
    functions.push_back(
        hook_function("startup_hook", "Called when the call starts", *opts.startup_hook_url));
  }
  if (has_hangup) {
    functions.push_back(
        hook_function("hangup_hook", "Called when the call ends", *opts.hangup_hook_url));
  }
  if (opts.swaig_functions.has_value()) {
    for (const auto& func : *opts.swaig_functions) {
      if (!is_hook_function(func)) {
        functions.push_back(func);
      }
    }
  }

  // Build the SWAIG config from the functions + default webhook URL.
  json swaig_config = json::object();
  const bool has_default =
      opts.default_webhook_url.has_value() && !opts.default_webhook_url->empty();
  if (!functions.empty() || has_default) {
    if (has_default) {
      swaig_config["defaults"] = json::object({{"web_hook_url", *opts.default_webhook_url}});
    }
    if (!functions.empty()) {
      swaig_config["functions"] = functions;
    }
  }

  std::optional<std::string> prompt_text;
  std::optional<json> prompt_pom;
  if (opts.prompt_is_pom) {
    prompt_pom = prompt;
  } else {
    prompt_text = prompt.is_string() ? prompt.get<std::string>() : prompt.dump();
  }

  builder.ai(
      prompt_text, prompt_pom, opts.post_prompt, opts.post_prompt_url,
      swaig_config.empty() ? std::optional<json>(std::nullopt) : std::optional<json>(swaig_config),
      opts.params.has_value() ? *opts.params : json::object());

  if (to_lower(opts.format) == "yaml") {
    return signalwire::pom::yaml_dump(builder.build());
  }
  return builder.render();
}

std::string SwmlRenderer::render_function_response_swml(
    const std::string& response_text, swml::Service& service,
    const std::optional<std::vector<json>>& actions, const std::string& format) {
  // Reset the document to start fresh.
  service.document() = swml::Document();

  if (!response_text.empty()) {
    service.document().add_verb("play", json::object({{"text", response_text}}));
  }

  if (actions.has_value()) {
    for (const auto& action : *actions) {
      if (!action.is_object()) {
        continue;
      }
      // First recognized action verb wins (precedence order, Python parity).
      if (action.contains("play")) {
        service.document().add_verb("play", action.at("play"));
      } else if (action.contains("hangup")) {
        service.document().add_verb("hangup", action.at("hangup"));
      } else if (action.contains("transfer")) {
        service.document().add_verb("transfer", action.at("transfer"));
      } else if (action.contains("ai")) {
        service.document().add_verb("ai", action.at("ai"));
      }
    }
  }

  if (to_lower(format) == "yaml") {
    return signalwire::pom::yaml_dump(service.document().to_json());
  }
  return service.document().to_string();
}

}  // namespace core
}  // namespace signalwire
