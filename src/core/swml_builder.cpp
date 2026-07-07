// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT

#include "signalwire/core/swml_builder.hpp"

#include <stdexcept>
#include <utility>

namespace signalwire {
namespace core {

SWMLBuilder::SWMLBuilder(swml::Service& service) : service_(service) {}

SWMLBuilder& SWMLBuilder::answer(std::optional<int> max_duration,
                                 std::optional<std::string> codecs) {
  json config = json::object();
  if (max_duration.has_value()) {
    config["max_duration"] = *max_duration;
  }
  if (codecs.has_value()) {
    config["codecs"] = *codecs;
  }
  service_.document().add_verb("answer", config);
  return *this;
}

SWMLBuilder& SWMLBuilder::hangup(std::optional<std::string> reason) {
  json config = json::object();
  if (reason.has_value()) {
    config["reason"] = *reason;
  }
  service_.document().add_verb("hangup", config);
  return *this;
}

SWMLBuilder& SWMLBuilder::ai(std::optional<std::string> prompt_text, std::optional<json> prompt_pom,
                             std::optional<std::string> post_prompt,
                             std::optional<std::string> post_prompt_url, std::optional<json> swaig,
                             const json& kwargs) {
  json config = json::object();

  if (prompt_text.has_value()) {
    config["prompt"] = json::object({{"text", *prompt_text}});
  } else if (prompt_pom.has_value()) {
    config["prompt"] = json::object({{"pom", *prompt_pom}});
  }

  if (post_prompt.has_value()) {
    config["post_prompt"] = json::object({{"text", *post_prompt}});
  }
  if (post_prompt_url.has_value()) {
    config["post_prompt_url"] = *post_prompt_url;
  }
  if (swaig.has_value()) {
    config["SWAIG"] = *swaig;
  }

  // Merge any additional kwargs (matches Python's config.update(kwargs)).
  if (kwargs.is_object()) {
    for (auto it = kwargs.begin(); it != kwargs.end(); ++it) {
      config[it.key()] = it.value();
    }
  }

  service_.document().add_verb("ai", config);
  return *this;
}

SWMLBuilder& SWMLBuilder::play(std::optional<std::string> url,
                               std::optional<std::vector<std::string>> urls,
                               std::optional<double> volume, std::optional<std::string> say_voice,
                               std::optional<std::string> say_language,
                               std::optional<std::string> say_gender,
                               std::optional<bool> auto_answer) {
  json config = json::object();

  if (url.has_value()) {
    config["url"] = *url;
  } else if (urls.has_value()) {
    config["urls"] = *urls;
  } else {
    throw std::invalid_argument("Either url or urls must be provided");
  }

  if (volume.has_value()) {
    config["volume"] = *volume;
  }
  if (say_voice.has_value()) {
    config["say_voice"] = *say_voice;
  }
  if (say_language.has_value()) {
    config["say_language"] = *say_language;
  }
  if (say_gender.has_value()) {
    config["say_gender"] = *say_gender;
  }
  if (auto_answer.has_value()) {
    config["auto_answer"] = *auto_answer;
  }

  service_.document().add_verb("play", config);
  return *this;
}

SWMLBuilder& SWMLBuilder::say(const std::string& text, std::optional<std::string> voice,
                              std::optional<std::string> language,
                              std::optional<std::string> gender, std::optional<double> volume) {
  return play("say:" + text, std::nullopt, volume, std::move(voice), std::move(language),
              std::move(gender), std::nullopt);
}

SWMLBuilder& SWMLBuilder::add_section(const std::string& section_name) {
  // Get-or-create the section on the underlying document.
  service_.document().section(section_name);
  return *this;
}

json SWMLBuilder::build() const { return service_.document().to_json(); }

std::string SWMLBuilder::render() const { return service_.document().to_string(); }

SWMLBuilder& SWMLBuilder::reset() {
  // Reset the document to an empty state (fresh main section).
  service_.document() = swml::Document();
  return *this;
}

}  // namespace core
}  // namespace signalwire
