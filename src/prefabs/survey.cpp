// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#include "signalwire/prefabs/prefabs.hpp"

namespace signalwire {
namespace prefabs {

SurveyAgent::SurveyAgent(const std::string& name, const std::string& route, const std::string& host,
                         int port)
    : AgentBase(name, route, host, port) {
  prompt_add_section("Personality",
                     "You are a professional survey conductor. Ask questions clearly and "
                     "record responses accurately.");
  prompt_add_section("Survey Instructions", "",
                     {"Ask questions one at a time in order",
                      "For rating questions, accept numbers in the valid range",
                      "For multiple choice, only accept listed options",
                      "For yes/no questions, accept yes, no, or equivalent responses",
                      "For open-ended questions, accept any response"});

  define_tool(
      "submit_survey_answer", "Submit an answer to the current survey question",
      json::object(
          {{"type", "object"},
           {"properties",
            json::object({{"answer", json::object({{"type", "string"},
                                                   {"description", "The survey answer"}})},
                          {"question_index", json::object({{"type", "integer"},
                                                           {"description", "Question index"}})}})},
           {"required", json::array({"answer"})}}),
      [](const json& args, const json&) -> swaig::FunctionResult {
        return swaig::FunctionResult("Answer recorded: " + args.value("answer", ""));
      });
}

SurveyAgent& SurveyAgent::set_questions(const std::vector<json>& questions) {
  json q_list = json::array();
  for (const auto& q : questions) { q_list.push_back(q);
}
  update_global_data(json::object({{"survey_questions", q_list}, {"current_index", 0}}));

  // Add question info to prompt
  std::vector<std::string> bullets;
  for (size_t i = 0; i < questions.size(); ++i) {
    std::string type = questions[i].value("type", "open_ended");
    std::string text = questions[i].value("question", "");
    std::string bullet = "Q";
    bullet += std::to_string(i + 1);
    bullet += " (";
    bullet += type;
    bullet += "): ";
    bullet += text;
    bullets.push_back(bullet);
  }
  prompt_add_section("Survey Questions", "", bullets);
  return *this;
}

SurveyAgent& SurveyAgent::set_completion_message(const std::string& msg) {
  prompt_add_to_section("Survey Instructions", "", {"When all questions are answered: " + msg});
  return *this;
}

SurveyAgent& SurveyAgent::set_intro_message(const std::string& msg) {
  prompt_add_to_section("Personality", "\nIntroduction: " + msg);
  return *this;
}

}  // namespace prefabs
}  // namespace signalwire
