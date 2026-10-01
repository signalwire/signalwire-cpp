// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
//
// SwmlRenderer — SWML document rendering utilities.
//
// Two static helpers; both build a document on a swml::Service (via
// SWMLBuilder) and return the rendered SWML string.
//
// render_swml has many optional inputs, so they are gathered into a
// RenderOptions struct of named fields with defaults rather than a long
// positional parameter list. A convenience minimal-form overload covers the
// common (prompt, service) call.

#pragma once

#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

#include "signalwire/swml/service.hpp"

namespace signalwire {
namespace core {

using json = nlohmann::json;

/// Named-parameter options for SwmlRenderer::render_swml. The two required
/// inputs (prompt, service) are passed to render_swml directly; every other
/// input lives here with a default.
struct RenderOptions {
  std::optional<std::string> post_prompt;
  std::optional<std::string> post_prompt_url;
  std::optional<std::vector<json>> swaig_functions;
  std::optional<std::string> startup_hook_url;
  std::optional<std::string> hangup_hook_url;
  bool prompt_is_pom = false;
  std::optional<json> params;
  bool add_answer = false;
  bool record_call = false;
  std::string record_format = "mp4";
  bool record_stereo = true;
  std::string format = "json";
  std::optional<std::string> default_webhook_url;
};

/// Renders SWML documents with AI and SWAIG components.
class SwmlRenderer {
 public:
  /// Generate a complete SWML document with an AI configuration.
  ///
  /// `prompt` is either the AI prompt text (a string) or a POM structure (a
  /// JSON array) when `opts.prompt_is_pom` is set. Returns the SWML document as
  /// a string (JSON, or YAML when `opts.format == "yaml"`).
  [[nodiscard]] static std::string render_swml(const json& prompt, swml::Service& service,
                                               const RenderOptions& opts = {});

  /// Generate a SWML document for a function response — a `play` of the
  /// response text followed by any provided actions. The response text is
  /// emitted as `play: {url: "say:<text>"}`: the SWML `play` verb has no
  /// `text` key, so the `say:` URL scheme is how spoken text reaches the wire.
  [[nodiscard]] static std::string render_function_response_swml(
      const std::string& response_text, swml::Service& service,
      const std::optional<std::vector<json>>& actions = std::nullopt,
      const std::string& format = "json");
};

}  // namespace core
}  // namespace signalwire
