// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#include "signalwire/agents/bedrock.hpp"

#include <algorithm>
#include <vector>

#include "signalwire/logging.hpp"

namespace signalwire {
namespace agents {

BedrockAgent::BedrockAgent(const std::string& name, const std::string& route,
                           const std::string& system_prompt, const std::string& voice_id,
                           double temperature, double top_p, int max_tokens)
    : AgentBase(name, route),
      voice_id_(voice_id),
      temperature_(temperature),
      top_p_(top_p),
      max_tokens_(max_tokens) {
  if (!system_prompt.empty()) {
    set_prompt_text(system_prompt);
  }
  get_logger().info("BedrockAgent initialized: " + name + " on route " + route);
}

void BedrockAgent::set_voice(const std::string& voice_id) {
  voice_id_ = voice_id;
  get_logger().debug("Voice set to: " + voice_id);
}

void BedrockAgent::set_inference_params(double temperature, double top_p, int max_tokens) {
  if (temperature >= 0.0) {
    temperature_ = temperature;
  }
  if (top_p >= 0.0) {
    top_p_ = top_p;
  }
  if (max_tokens >= 0) {
    max_tokens_ = max_tokens;
  }
}

void BedrockAgent::set_llm_model(const std::string& model) {
  get_logger().warn("set_llm_model('" + model +
                    "') called but Bedrock uses a fixed voice-to-voice model");
}

void BedrockAgent::set_llm_temperature(double temperature) { set_inference_params(temperature); }

void BedrockAgent::set_post_prompt_llm_params(const json&) {
  get_logger().warn(
      "set_post_prompt_llm_params() called but Bedrock post-prompt uses OpenAI in the engine");
}

void BedrockAgent::set_prompt_llm_params(const json&) {
  get_logger().warn("set_prompt_llm_params() called - use set_inference_params() for Bedrock");
}

std::string BedrockAgent::repr() const {
  return "BedrockAgent(name='" + name() + "', route='" + route() + "', voice='" + voice_id_ + "')";
}

json BedrockAgent::add_voice_to_prompt(const json& prompt_config) const {
  json filtered = json::object();
  // Drop text-model-only params that don't apply to the voice-to-voice model.
  static const std::vector<std::string> skip = {"barge_confidence", "presence_penalty",
                                                "frequency_penalty"};
  for (auto it = prompt_config.begin(); it != prompt_config.end(); ++it) {
    if (std::find(skip.begin(), skip.end(), it.key()) != skip.end()) {
      continue;
    }
    filtered[it.key()] = it.value();
  }
  filtered["voice_id"] = voice_id_;
  filtered["temperature"] = temperature_;
  filtered["top_p"] = top_p_;
  return filtered;
}

void BedrockAgent::transform_swml(json& swml) const {
  if (!swml.contains("sections")) {
    return;
  }
  auto& sections = swml["sections"];
  if (!sections.contains("main")) {
    return;
  }
  auto& main = sections["main"];
  for (auto& verb : main) {
    if (!verb.contains("ai")) {
      continue;
    }
    const json& ai_config = verb["ai"];
    json bedrock = json::object();
    bedrock["prompt"] = add_voice_to_prompt(ai_config.value("prompt", json::object()));
    if (ai_config.contains("SWAIG")) {
      bedrock["SWAIG"] = ai_config["SWAIG"];
    }
    if (ai_config.contains("params")) {
      bedrock["params"] = ai_config["params"];
    }
    if (ai_config.contains("global_data")) {
      bedrock["global_data"] = ai_config["global_data"];
    }
    if (ai_config.contains("post_prompt")) {
      bedrock["post_prompt"] = ai_config["post_prompt"];
    }
    if (ai_config.contains("post_prompt_url")) {
      bedrock["post_prompt_url"] = ai_config["post_prompt_url"];
    }
    verb.erase("ai");
    verb["amazon_bedrock"] = bedrock;
    break;
  }
}

}  // namespace agents
}  // namespace signalwire
