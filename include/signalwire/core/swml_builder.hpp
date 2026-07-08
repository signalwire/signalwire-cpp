// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
//
// SWMLBuilder — fluent builder for SWML documents.
//
// Mirrors the Python reference signalwire.core.swml_builder.SWMLBuilder (which
// wraps an SWMLService) and the Java port com.signalwire.sdk.swml.SWMLBuilder.
// It delegates to an underlying swml::Service instance (the C++ analog of the
// reference's SWMLService) for the actual document construction; each verb
// method appends a verb to the main section and returns *this for chaining.
//
// The reference installs the remaining schema verbs dynamically via __getattr__
// at runtime. C++ has no __getattr__ / method_missing analog, so that dynamic
// dispatch is intentionally NOT ported — the explicit verb helpers below cover
// the reference's named verb methods (answer/hangup/ai/play/say), matching the
// enumerated method surface.

#pragma once

#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

#include "signalwire/swml/service.hpp"

namespace signalwire {
namespace core {

using json = nlohmann::json;

/// Fluent builder for SWML documents, delegating to a swml::Service.
class SWMLBuilder {
 public:
  /// Initialize with a Service instance to delegate to.
  explicit SWMLBuilder(swml::Service& service);

  /// Expose the underlying service (tests / advanced use).
  [[nodiscard]] swml::Service& service() { return service_; }
  [[nodiscard]] const swml::Service& service() const { return service_; }

  /// Add an 'answer' verb to the main section.
  SWMLBuilder& answer(std::optional<int> max_duration = std::nullopt,
                      std::optional<std::string> codecs = std::nullopt);

  /// Add a 'hangup' verb to the main section.
  SWMLBuilder& hangup(std::optional<std::string> reason = std::nullopt);

  /// Add an 'ai' verb to the main section.
  ///
  /// The SWML `ai` verb requires `prompt` to be an OBJECT — {"text": ...} or
  /// {"pom": [...]}; a bare string is a fatal engine error, so text/pom is
  /// wrapped accordingly. `kwargs` (a JSON object) is merged at the top level.
  SWMLBuilder& ai(std::optional<std::string> prompt_text = std::nullopt,
                  std::optional<json> prompt_pom = std::nullopt,
                  std::optional<std::string> post_prompt = std::nullopt,
                  std::optional<std::string> post_prompt_url = std::nullopt,
                  std::optional<json> swaig = std::nullopt, const json& kwargs = json::object());

  /// Add a 'play' verb to the main section. Throws std::invalid_argument if
  /// neither url nor urls is provided.
  SWMLBuilder& play(std::optional<std::string> url = std::nullopt,
                    std::optional<std::vector<std::string>> urls = std::nullopt,
                    std::optional<double> volume = std::nullopt,
                    std::optional<std::string> say_voice = std::nullopt,
                    std::optional<std::string> say_language = std::nullopt,
                    std::optional<std::string> say_gender = std::nullopt,
                    std::optional<bool> auto_answer = std::nullopt);

  /// Add a 'play' verb with a `say:` prefix for text-to-speech.
  SWMLBuilder& say(const std::string& text, std::optional<std::string> voice = std::nullopt,
                   std::optional<std::string> language = std::nullopt,
                   std::optional<std::string> gender = std::nullopt,
                   std::optional<double> volume = std::nullopt);

  /// Add a new section to the document.
  SWMLBuilder& add_section(const std::string& section_name);

  /// Build and return the SWML document as JSON ({version, sections}).
  [[nodiscard]] json build() const;

  /// Build and render the SWML document as a JSON string.
  [[nodiscard]] std::string render() const;

  /// Reset the document to an empty state.
  SWMLBuilder& reset();

 private:
  swml::Service& service_;
};

}  // namespace core
}  // namespace signalwire
