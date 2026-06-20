// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#include "signalwire/prefabs/prefabs.hpp"

namespace signalwire {
namespace prefabs {

InfoGathererAgent::InfoGathererAgent(const std::string& name, const std::string& route,
                                     const std::string& host, int port)
    : AgentBase(name, route, host, port) {
  prompt_add_section("Personality",
                     "You are a friendly information gatherer. Your job is to ask questions "
                     "one at a time and collect answers from the user.");
  prompt_add_section("Instructions", "",
                     {"Start by calling start_questions to get the first question",
                      "Ask each question clearly and wait for the user's response",
                      "Submit each answer using submit_answer",
                      "Confirm answers when required before submitting"});
}

InfoGathererAgent& InfoGathererAgent::set_questions(const std::vector<json>& questions) {
  add_skill("info_gatherer", json::object({{"questions", questions}}));
  return *this;
}

InfoGathererAgent& InfoGathererAgent::set_completion_message(const std::string& msg) {
  // Re-add skill with updated params if needed
  prompt_add_to_section("Instructions", "", {"When all questions are answered, say: " + msg});
  return *this;
}

InfoGathererAgent& InfoGathererAgent::set_prefix(const std::string& prefix) {
  // Would be set in add_skill params
  return *this;
}

}  // namespace prefabs
}  // namespace signalwire
