// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#pragma once

#include <string>

#include "signalwire/agent/agent_base.hpp"

namespace signalwire {
namespace agents {

using json = nlohmann::json;

/// Amazon Bedrock voice-to-voice agent.
///
/// Extends AgentBase so the full agent ecosystem (prompt/POM, skills, SWAIG
/// functions, post-prompt, dynamic config) applies, but renders SWML with the
/// ``amazon_bedrock`` verb instead of ``ai``: at render time the ``ai`` verb is
/// transformed into ``amazon_bedrock`` and voice/inference params are folded
/// into the prompt object (Bedrock carries voice + inference inside ``prompt``,
/// not as sibling fields).
///
/// Python parity: ``signalwire.agents.bedrock.BedrockAgent``.
class BedrockAgent : public agent::AgentBase {
 public:
  explicit BedrockAgent(const std::string& name = "bedrock_agent",
                        const std::string& route = "/bedrock",
                        const std::string& system_prompt = "",
                        const std::string& voice_id = "matthew", double temperature = 0.7,
                        double top_p = 0.9, int max_tokens = 1024);

  /// Set the Bedrock voice ID (e.g. "matthew", "joanna").
  void set_voice(const std::string& voice_id);

  /// Update Bedrock inference params. A negative value leaves that param
  /// unchanged (mirrors Python's ``None`` = "don't update").
  void set_inference_params(double temperature = -1.0, double top_p = -1.0, int max_tokens = -1);

  /// Not applicable for Bedrock (fixed voice-to-voice model) — logs a warning.
  void set_llm_model(const std::string& model);

  /// Redirects to set_inference_params(temperature).
  void set_llm_temperature(double temperature);

  /// Not applicable for Bedrock — logs a warning. (Post-prompt uses OpenAI
  /// configured in the engine.)
  void set_post_prompt_llm_params(const json& params = json::object());

  /// Not applicable for Bedrock — use set_inference_params() instead; logs a warning.
  void set_prompt_llm_params(const json& params = json::object());

  /// String representation (Python: ``__repr__``).
  [[nodiscard]] std::string repr() const;

 protected:
  void transform_swml(json& swml) const override;

 private:
  [[nodiscard]] json add_voice_to_prompt(const json& prompt_config) const;

  std::string voice_id_;
  double temperature_;
  double top_p_;
  int max_tokens_;
};

}  // namespace agents
}  // namespace signalwire
