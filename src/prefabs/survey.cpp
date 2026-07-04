// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#include <algorithm>
#include <cctype>

#include "signalwire/prefabs/prefabs.hpp"

namespace signalwire {
namespace prefabs {

namespace {
std::string strip_lower(const std::string& s) {
  size_t b = 0, e = s.size();
  while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) {
    ++b;
  }
  while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) {
    --e;
  }
  std::string out = s.substr(b, e - b);
  std::transform(out.begin(), out.end(), out.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return out;
}

std::string strip(const std::string& s) {
  size_t b = 0, e = s.size();
  while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) {
    ++b;
  }
  while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) {
    --e;
  }
  return s.substr(b, e - b);
}
}  // namespace

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
  survey_questions_.assign(questions.begin(), questions.end());
  json q_list = json::array();
  for (const auto& q : questions) {
    q_list.push_back(q);
  }
  update_global_data(json::object({{"survey_questions", q_list}, {"current_index", 0}}));

  // Register validate_response + log_response tools bound to the member
  // handlers (ported from the Java SurveyAgent).
  define_tool(
      "validate_response", "Validate if a response meets the requirements for a specific question",
      json::object(
          {{"type", "object"},
           {"properties",
            json::object(
                {{"question_id",
                  json::object({{"type", "string"}, {"description", "The ID of the question"}})},
                 {"response",
                  json::object(
                      {{"type", "string"}, {"description", "The user's response to validate"}})}})},
           {"required", json::array({"question_id", "response"})}}),
      [this](const json& args, const json& raw) { return validate_response(args, raw); });

  define_tool(
      "log_response", "Log a validated response to a survey question",
      json::object(
          {{"type", "object"},
           {"properties",
            json::object(
                {{"question_id",
                  json::object({{"type", "string"}, {"description", "The ID of the question"}})},
                 {"response", json::object({{"type", "string"},
                                            {"description", "The user's validated response"}})}})},
           {"required", json::array({"question_id", "response"})}}),
      [this](const json& args, const json& raw) { return log_response(args, raw); });

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

json SurveyAgent::find_question_by_id(const std::string& id) const {
  for (const auto& q : survey_questions_) {
    if (q.value("id", "") == id) {
      return q;
    }
  }
  return json();  // null
}

swaig::FunctionResult SurveyAgent::validate_response(const json& args, const json&) {
  std::string question_id = args.value("question_id", "");
  std::string response = args.value("response", "");

  json question = find_question_by_id(question_id);
  if (question.is_null()) {
    return swaig::FunctionResult("Error: Question with ID '" + question_id + "' not found.");
  }

  std::string type = question.value("type", "open_ended");
  std::string message = "Response to '" + question_id + "' is valid.";

  if (type == "rating") {
    int scale = question.value("scale", 5);
    bool ok = false;
    try {
      int rating = std::stoi(strip(response));
      ok = (rating >= 1 && rating <= scale);
    } catch (...) {
      ok = false;
    }
    if (!ok) {
      message =
          "Invalid rating. Please provide a number between 1 and " + std::to_string(scale) + ".";
    }
  } else if (type == "multiple_choice") {
    std::vector<std::string> options;
    if (question.contains("options") && question["options"].is_array()) {
      for (const auto& o : question["options"]) {
        options.push_back(o.get<std::string>());
      }
    }
    std::string lower = strip_lower(response);
    bool matched = false;
    std::string joined;
    for (size_t i = 0; i < options.size(); ++i) {
      if (i) {
        joined += ", ";
      }
      joined += options[i];
      if (lower == strip_lower(options[i])) {
        matched = true;
      }
    }
    if (!matched) {
      message = "Invalid choice. Please select one of: " + joined + ".";
    }
  } else if (type == "yes_no") {
    std::string lower = strip_lower(response);
    if (lower != "yes" && lower != "no" && lower != "y" && lower != "n") {
      message = "Please answer with 'yes' or 'no'.";
    }
  } else if (type == "open_ended") {
    bool required = question.value("required", true);
    if (strip(response).empty() && required) {
      message = "A response is required for this question.";
    }
  }

  return swaig::FunctionResult(message);
}

swaig::FunctionResult SurveyAgent::log_response(const json& args, const json&) {
  std::string question_id = args.value("question_id", "");
  json question = find_question_by_id(question_id);
  std::string question_text = question.is_null() ? "" : question.value("text", "");
  return swaig::FunctionResult("Response to '" + question_text + "' has been recorded.");
}

SurveyAgent& SurveyAgent::on_summary(agent::SummaryCallback cb) {
  AgentBase::on_summary(std::move(cb));
  return *this;
}

}  // namespace prefabs
}  // namespace signalwire
