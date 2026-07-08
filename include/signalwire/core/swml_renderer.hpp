// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
//
// SwmlRenderer — SWML document rendering utilities.
//
// Mirrors the Python reference signalwire.core.swml_renderer.SwmlRenderer (two
// static helpers) and the Java port com.signalwire.sdk.swml.SwmlRenderer. Both
// helpers are static; they build a document on a swml::Service (via SWMLBuilder)
// and return the rendered SWML string.
//
// render_swml has many optional inputs. The reference passes them as keyword
// args; the C++ idiom for that is a RenderOptions struct (named fields with
// reference defaults), mirroring the Java RenderOptions builder object. A
// convenience minimal-form overload covers the common (prompt, service) call.

#pragma once

#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

#include "signalwire/swml/service.hpp"

namespace signalwire {
namespace core {

using json = nlohmann::json;

/// Named-parameter options for SwmlRenderer::render_swml — the C++ analog of the
/// reference's keyword arguments. The two required inputs (prompt, service) are
/// passed to render_swml directly; the rest live here with reference defaults.
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
  /// response text followed by any provided actions.
  [[nodiscard]] static std::string render_function_response_swml(
      const std::string& response_text, swml::Service& service,
      const std::optional<std::vector<json>>& actions = std::nullopt,
      const std::string& format = "json");
};

}  // namespace core
}  // namespace signalwire
