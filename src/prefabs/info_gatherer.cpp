// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#include "signalwire/prefabs/prefabs.hpp"

namespace signalwire {
namespace prefabs {

namespace {
// Build the "ask this question" instruction (ported from the Java
// InfoGathererAgent.generateQuestionInstruction).
std::string generate_question_instruction(const std::string& question_text, bool needs_confirmation,
                                          bool is_first_question) {
  std::string out;
  if (is_first_question) {
    out += "Ask the user to answer the following question: " + question_text + "\n\n";
  } else {
    out += "Previous Answer recorded. Now ask the user to answer the following question: " +
           question_text + "\n\n";
  }
  out += "Make sure the answer fits the scope and context of the question before submitting it. ";
  if (needs_confirmation) {
    out +=
        "Insist that the user confirms the answer as many times as needed until they say it is "
        "correct.";
  } else {
    out += "You don't need the user to confirm the answer to this question.";
  }
  return out;
}

std::vector<json> fallback_questions() {
  return {json{{"key_name", "name"}, {"question_text", "What is your name?"}},
          json{{"key_name", "message"}, {"question_text", "How can I help you today?"}}};
}

json global_data_override(const std::vector<json>& questions) {
  json q = json::array();
  for (const auto& item : questions) {
    q.push_back(item);
  }
  return json{
      {"global_data", json{{"questions", q}, {"question_index", 0}, {"answers", json::array()}}}};
}
}  // namespace

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

  // start_questions + submit_answer SWAIG tools bound to the member handlers
  // (ported from the Java InfoGathererAgent).
  define_tool("start_questions", "Start the question sequence with the first question",
              json::object({{"type", "object"}, {"properties", json::object()}}),
              [this](const json& args, const json& raw) { return start_questions(args, raw); });

  define_tool(
      "submit_answer", "Submit an answer to the current question and move to the next one",
      json::object(
          {{"type", "object"},
           {"properties",
            json::object(
                {{"answer",
                  json::object({{"type", "string"},
                                {"description", "The user's answer to the current question"}})}})},
           {"required", json::array({"answer"})}}),
      [this](const json& args, const json& raw) { return submit_answer(args, raw); });
}

InfoGathererAgent& InfoGathererAgent::set_questions(const std::vector<json>& questions) {
  static_questions_.assign(questions.begin(), questions.end());
  has_static_questions_ = true;
  add_skill("info_gatherer", json::object({{"questions", questions}}));
  json q = json::array();
  for (const auto& item : questions) {
    q.push_back(item);
  }
  update_global_data(json{{"questions", q}, {"question_index", 0}, {"answers", json::array()}});
  return *this;
}

InfoGathererAgent& InfoGathererAgent::set_question_callback(QuestionCallback cb) {
  question_callback_ = std::move(cb);
  return *this;
}

json InfoGathererAgent::on_swml_request(const json& request_data, const json& query_params,
                                        const json& headers) {
  // Static mode: no dynamic override.
  if (has_static_questions_) {
    return json();  // null
  }
  if (!question_callback_) {
    return global_data_override(fallback_questions());
  }
  json qp = query_params.is_object() ? query_params : json::object();
  json bp = request_data.is_object() ? request_data : json::object();
  json hd = headers.is_object() ? headers : json::object();
  try {
    std::vector<json> questions = question_callback_(qp, bp, hd);
    if (questions.empty()) {
      return global_data_override(fallback_questions());
    }
    for (const auto& q : questions) {
      if (!q.contains("key_name") || !q.contains("question_text")) {
        return global_data_override(fallback_questions());
      }
    }
    return global_data_override(questions);
  } catch (...) {
    return global_data_override(fallback_questions());
  }
}

swaig::FunctionResult InfoGathererAgent::start_questions(const json&, const json& raw_data) {
  json global_data = raw_data.is_object() && raw_data.contains("global_data")
                         ? raw_data["global_data"]
                         : json::object();
  json questions = global_data.value("questions", json::array());
  int question_index = global_data.value("question_index", 0);

  if (!questions.is_array() || questions.empty() ||
      question_index >= static_cast<int>(questions.size())) {
    return swaig::FunctionResult("I don't have any questions to ask.");
  }

  const json& current = questions[question_index];
  std::string question_text = current.value("question_text", "");
  bool needs_confirmation = current.value("confirm", false);

  std::string instruction = generate_question_instruction(question_text, needs_confirmation, true);
  swaig::FunctionResult result(instruction);
  result.replace_in_history("Welcome! Let me ask you a few questions.");
  return result;
}

swaig::FunctionResult InfoGathererAgent::submit_answer(const json& args, const json& raw_data) {
  std::string answer = args.value("answer", "");

  json global_data = raw_data.is_object() && raw_data.contains("global_data")
                         ? raw_data["global_data"]
                         : json::object();
  json questions = global_data.value("questions", json::array());
  int question_index = global_data.value("question_index", 0);
  json answers = global_data.value("answers", json::array());

  if (!questions.is_array() || question_index >= static_cast<int>(questions.size())) {
    return swaig::FunctionResult("All questions have already been answered.");
  }

  const json& current = questions[question_index];
  std::string key_name = current.value("key_name", "");

  json new_answers = answers;
  new_answers.push_back(json{{"key_name", key_name}, {"answer", answer}});
  int new_index = question_index + 1;

  if (new_index < static_cast<int>(questions.size())) {
    const json& next = questions[new_index];
    std::string next_text = next.value("question_text", "");
    bool needs_confirmation = next.value("confirm", false);
    std::string instruction = generate_question_instruction(next_text, needs_confirmation, false);

    swaig::FunctionResult result(instruction);
    result.replace_in_history(true);
    result.update_global_data(json{{"answers", new_answers}, {"question_index", new_index}});
    return result;
  }

  swaig::FunctionResult result(
      "Thank you! All questions have been answered. You can now summarize the information "
      "collected "
      "or ask if there's anything else the user would like to discuss.");
  result.replace_in_history(true);
  result.update_global_data(json{{"answers", new_answers}, {"question_index", new_index}});
  return result;
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
