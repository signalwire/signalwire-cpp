// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
//
// SWML verb handlers — the pluggable verb-handler registry.
//
// SWMLVerbHandler is the abstract base, AIVerbHandler the concrete handler for
// the complex "ai" verb, and VerbHandlerRegistry maps verb-name -> handler.
//
// A verb handler provides specialized logic for complex SWML verbs that cannot
// be handled generically: it names its verb, validates a config, and builds a
// verb config (nlohmann::json object) from typed inputs.

#pragma once

#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace signalwire {
namespace core {

using json = nlohmann::json;

/// Result of validate_config: (is_valid, error_messages).
///
/// `valid` is redundant with `errors.empty()` but kept as an explicit field so
/// the (bool, list) pair shape is available directly.
struct VerbValidationResult {
  bool valid = false;
  std::vector<std::string> errors;
};

/// Base interface for SWML verb handlers.
///
/// Every method is pure-virtual, so a subclass that forgets to override one
/// fails to compile.
class SWMLVerbHandler {
 public:
  virtual ~SWMLVerbHandler() = default;

  /// Get the name of the verb this handler handles.
  [[nodiscard]] virtual std::string get_verb_name() const = 0;

  /// Validate the configuration for this verb.
  [[nodiscard]] virtual VerbValidationResult validate_config(const json& config) const = 0;

  /// Build a configuration for this verb from the provided named arguments,
  /// passed as a JSON object. Returns the verb config object.
  [[nodiscard]] virtual json build_config(const json& kwargs = json::object()) const = 0;
};

/// Handler for the SWML 'ai' verb.
///
/// The 'ai' verb is complex and requires specialized handling — managing
/// prompts (text vs pom, mutually exclusive), contexts, post-prompt, and SWAIG.
class AIVerbHandler : public SWMLVerbHandler {
 public:
  [[nodiscard]] std::string get_verb_name() const override;

  /// Validate the AI verb config: `prompt` present and an object with exactly
  /// one of `text` / `pom`; `prompt.contexts` (if present) an object; `SWAIG`
  /// (if present) an object.
  [[nodiscard]] VerbValidationResult validate_config(const json& config) const override;

  /// Catch-all kwargs form — extracts the recognized keys (prompt_text,
  /// prompt_pom, contexts, post_prompt, post_prompt_url, swaig) from the JSON
  /// object and treats the rest as extra AI params. Prefer the typed overload
  /// below.
  [[nodiscard]] json build_config(const json& kwargs = json::object()) const override;

  /// Typed overload. Requires exactly one of
  /// prompt_text / prompt_pom (mutually exclusive, else throws
  /// std::invalid_argument). `languages`, `hints`, `pronounce`, `global_data`
  /// go at the top level; every other extra kwarg lands in config["params"].
  [[nodiscard]] json build_config(const std::optional<std::string>& prompt_text,
                                  const std::optional<json>& prompt_pom,
                                  const std::optional<json>& contexts,
                                  const std::optional<std::string>& post_prompt,
                                  const std::optional<std::string>& post_prompt_url,
                                  const std::optional<json>& swaig,
                                  const json& kwargs = json::object()) const;
};

/// Registry for SWML verb handlers.
///
/// Maps verb name -> handler. The "ai" handler is registered automatically on
/// construction.
class VerbHandlerRegistry {
 public:
  /// Initialize with the default handlers (AIVerbHandler).
  VerbHandlerRegistry();

  /// Register a verb handler, replacing any existing handler for the same verb.
  void register_handler(std::shared_ptr<SWMLVerbHandler> handler);

  /// Get the handler for a verb, or nullptr when none is registered.
  [[nodiscard]] std::shared_ptr<SWMLVerbHandler> get_handler(const std::string& verb_name) const;

  /// Whether a handler exists for a verb.
  [[nodiscard]] bool has_handler(const std::string& verb_name) const;

  /// The registered verb names, sorted.
  [[nodiscard]] std::vector<std::string> get_verb_names() const {
    std::vector<std::string> names;
    names.reserve(handlers_.size());
    for (const auto& [verb, handler] : handlers_) {
      names.push_back(verb);
    }
    return names;  // std::map iterates in sorted key order
  }

 private:
  std::map<std::string, std::shared_ptr<SWMLVerbHandler>> handlers_;
};

}  // namespace core
}  // namespace signalwire
