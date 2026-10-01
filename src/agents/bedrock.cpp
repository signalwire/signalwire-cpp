// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#include "signalwire/agents/bedrock.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>
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

namespace {

// A prompt setting as a number: a JSON number, or a string holding one.
double bedrock_number(const std::string& name, const json& value, bool integer) {
  double out = 0.0;
  if (value.is_number()) {
    out = value.get<double>();
  } else if (value.is_string()) {
    const std::string text = value.get<std::string>();
    size_t used = 0;
    try {
      out = std::stod(text, &used);
    } catch (const std::exception&) {
      used = 0;
    }
    if (used == 0 || used != text.size()) {
      throw std::invalid_argument(name + " must be a number, got '" + text + "'");
    }
  } else {
    throw std::invalid_argument(name + " must be a number");
  }
  if (integer && out != static_cast<double>(static_cast<long long>(out))) {
    throw std::invalid_argument(name + " must be an integer");
  }
  return out;
}

}  // namespace

BedrockAgent& BedrockAgent::set_prompt_llm_params(const json& params) {
  if (!params.is_object()) {
    return *this;
  }
  // Convert all three before changing any, so a refused value leaves the
  // settings as they were.
  double temperature = -1.0;
  double top_p = -1.0;
  int max_tokens = -1;
  if (params.contains("temperature") && !params["temperature"].is_null()) {
    temperature = bedrock_number("temperature", params["temperature"], false);
  }
  if (params.contains("top_p") && !params["top_p"].is_null()) {
    top_p = bedrock_number("top_p", params["top_p"], false);
  }
  if (params.contains("max_tokens") && !params["max_tokens"].is_null()) {
    max_tokens = static_cast<int>(bedrock_number("max_tokens", params["max_tokens"], true));
  }
  set_inference_params(temperature, top_p, max_tokens);

  std::vector<std::string> ignored;
  for (auto it = params.begin(); it != params.end(); ++it) {
    if (it.key() != "temperature" && it.key() != "top_p" && it.key() != "max_tokens") {
      ignored.push_back(it.key());
    }
  }
  if (!ignored.empty()) {
    std::sort(ignored.begin(), ignored.end());
    std::string names;
    for (size_t i = 0; i < ignored.size(); ++i) {
      names += (i == 0 ? "" : ", ") + ignored[i];
    }
    get_logger().warn("set_prompt_llm_params(): the platform's Bedrock session doesn't use " +
                      names + (ignored.size() == 1 ? ", so it's ignored" : ", so they're ignored"));
  }
  return *this;
}

std::string BedrockAgent::repr() const {
  return "BedrockAgent(name='" + name() + "', route='" + route() + "', voice='" + voice_id_ + "')";
}

json BedrockAgent::add_voice_to_prompt(const json& prompt_config) const {
  // Copy the prompt text only (``text`` / ``pom``). Anything else, such as
  // confidence or contexts, is left out: the platform's Bedrock session reads
  // only these, voice_id and the inference settings, which come from the
  // agent's own settings below.
  static const std::vector<std::string> keep = {"text", "pom"};
  json filtered = json::object();
  if (prompt_config.is_object()) {
    for (auto it = prompt_config.begin(); it != prompt_config.end(); ++it) {
      if (std::find(keep.begin(), keep.end(), it.key()) != keep.end()) {
        filtered[it.key()] = it.value();
      }
    }
  }
  filtered["voice_id"] = voice_id_;
  filtered["temperature"] = temperature_;
  filtered["top_p"] = top_p_;
  filtered["max_tokens"] = max_tokens_;
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
    bedrock["SWAIG"] = ai_config.value("SWAIG", json::object());
    bedrock["params"] = ai_config.value("params", json::object());
    bedrock["global_data"] = ai_config.value("global_data", json::object());
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
