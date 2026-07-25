// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "signalwire/agent/agent_base.hpp"

namespace signalwire {
namespace prefabs {

using json = nlohmann::json;

/// Sequential question collection with key/value answers
class InfoGathererAgent : public agent::AgentBase {
 public:
  explicit InfoGathererAgent(const std::string& name = "info_gatherer",
                             const std::string& route = "/", const std::string& host = "0.0.0.0",
                             int port = 3000);

  InfoGathererAgent& set_questions(const std::vector<json>& questions);
  InfoGathererAgent& set_completion_message(const std::string& msg);
  InfoGathererAgent& set_prefix(const std::string& prefix);

  /// Per-request question producer (dynamic mode). Mirrors the Python
  /// callback signature (query_params, body_params, headers) -> questions.
  using QuestionCallback = std::function<std::vector<json>(
      const json& query_params, const json& body_params, const json& headers)>;

  /// Register a callback that produces the question list per request. When
  /// set (and no static questions were supplied), on_swml_request invokes it
  /// and seeds the returned questions into global_data.
  InfoGathererAgent& set_question_callback(QuestionCallback cb);

  /// Dynamic-config hook: in static mode returns null (no override); in
  /// dynamic mode invokes the question callback (or a name/message fallback)
  /// and returns a {"global_data": {questions, question_index, answers}}
  /// override object. Mirrors Python InfoGathererAgent.on_swml_request.
  json on_swml_request(const json& request_data, const json& query_params, const json& headers);

  /// SWAIG tool handler: return the first question. Reads
  /// questions/question_index from global_data (in raw_data).
  swaig::FunctionResult start_questions(const json& args, const json& raw_data);

  /// SWAIG tool handler: record the current answer and advance to the next
  /// question (or a completion message), updating global_data.
  swaig::FunctionResult submit_answer(const json& args, const json& raw_data);

 private:
  std::vector<json> static_questions_;
  bool has_static_questions_ = false;
  QuestionCallback question_callback_;
};

/// Typed surveys with validation
class SurveyAgent : public agent::AgentBase {
 public:
  explicit SurveyAgent(const std::string& name = "survey", const std::string& route = "/",
                       const std::string& host = "0.0.0.0", int port = 3000);

  SurveyAgent& set_questions(const std::vector<json>& questions);
  SurveyAgent& set_completion_message(const std::string& msg);
  SurveyAgent& set_intro_message(const std::string& msg);
  /// The survey's display name (reference: ``self.survey_name``), used in the
  /// default introduction.
  SurveyAgent& set_survey_name(const std::string& name);
  /// The brand/company name (reference: ``self.brand_name``, default
  /// "Our Company").
  SurveyAgent& set_brand_name(const std::string& name);
  /// Maximum retries for an invalid answer (reference: ``self.max_retries``,
  /// default 2).
  SurveyAgent& set_max_retries(int retries);

  // Configuration the reference keeps as public instance attributes; a caller
  // supplies each of these, so a caller reads each back.
  /// reference: ``self.survey_name``
  [[nodiscard]] const std::string& survey_name() const { return survey_name_; }
  /// reference: ``self.questions`` — the question objects driving the survey.
  [[nodiscard]] const std::vector<json>& questions() const { return survey_questions_; }
  /// reference: ``self.brand_name``
  [[nodiscard]] const std::string& brand_name() const { return brand_name_; }
  /// reference: ``self.introduction`` — the opening line; defaults to
  /// "Welcome to our <survey_name>. We appreciate your participation."
  [[nodiscard]] const std::string& introduction() const { return introduction_; }
  /// reference: ``self.conclusion`` — the closing line; defaults to
  /// "Thank you for completing our survey. Your feedback is valuable to us."
  [[nodiscard]] const std::string& conclusion() const { return conclusion_; }
  /// reference: ``self.max_retries``
  [[nodiscard]] int max_retries() const { return max_retries_; }

  /// Register a post-prompt summary callback (Python SurveyAgent.on_summary).
  /// Wires through to AgentBase::on_summary.
  SurveyAgent& on_summary(agent::SummaryCallback cb);

  /// SWAIG tool handler: validate a response against a question's
  /// constraints (rating range / multiple_choice membership / yes_no /
  /// required open_ended). Looks the question up by `id`.
  swaig::FunctionResult validate_response(const json& args, const json& raw_data);

  /// SWAIG tool handler: acknowledge a validated response. Looks the
  /// question up by `id` for a friendlier confirmation message.
  swaig::FunctionResult log_response(const json& args, const json& raw_data);

 private:
  std::vector<json> survey_questions_;
  std::string survey_name_;
  std::string brand_name_ = "Our Company";
  std::string introduction_ = "Welcome to our survey. We appreciate your participation.";
  std::string conclusion_ = "Thank you for completing our survey. Your feedback is valuable to us.";
  int max_retries_ = 2;
  json find_question_by_id(const std::string& id) const;
};

/// Department routing with call transfer
class ReceptionistAgent : public agent::AgentBase {
 public:
  explicit ReceptionistAgent(const std::string& name = "receptionist",
                             const std::string& route = "/", const std::string& host = "0.0.0.0",
                             int port = 3000);

  ReceptionistAgent& set_departments(const json& departments);
  ReceptionistAgent& set_greeting(const std::string& greeting);
  ReceptionistAgent& set_transfer_message(const std::string& msg);

  /// Register a post-prompt summary callback (Python
  /// ReceptionistAgent.on_summary override point). Wires through to
  /// AgentBase::on_summary.
  ReceptionistAgent& on_summary(agent::SummaryCallback cb);
};

/// Keyword-based FAQ matching
class FAQBotAgent : public agent::AgentBase {
 public:
  explicit FAQBotAgent(const std::string& name = "faq_bot", const std::string& route = "/",
                       const std::string& host = "0.0.0.0", int port = 3000);

  FAQBotAgent& set_faqs(const std::vector<json>& faqs);
  FAQBotAgent& set_no_match_message(const std::string& msg);
  FAQBotAgent& set_suggest_related(bool suggest);
  /// The bot's personality description (reference: ``self.persona``).
  FAQBotAgent& set_persona(const std::string& persona);

  // Configuration the reference keeps as public instance attributes.
  /// reference: ``self.faqs`` — the FAQ items ({question, answer, categories}).
  [[nodiscard]] const std::vector<json>& faqs() const { return faqs_; }
  /// reference: ``self.suggest_related`` — whether related questions are
  /// suggested alongside an answer (default true).
  [[nodiscard]] bool suggest_related() const { return suggest_related_; }
  /// reference: ``self.persona`` — defaults to "You are a helpful FAQ bot that
  /// provides accurate answers to common questions."
  [[nodiscard]] const std::string& persona() const { return persona_; }

  /// Register a post-prompt summary callback (Python FAQBotAgent.on_summary).
  /// Wires through to AgentBase::on_summary.
  FAQBotAgent& on_summary(agent::SummaryCallback cb);

  /// SWAIG tool handler: search FAQs matching a query and/or category.
  /// Scores each FAQ (substring match on the question, prefix boost, and
  /// category match), sorts by score, returns the top 3 matching questions.
  swaig::FunctionResult search_faqs(const json& args, const json& raw_data);

 private:
  std::vector<json> faqs_;
  bool suggest_related_ = true;
  std::string persona_ =
      "You are a helpful FAQ bot that provides accurate answers to common questions.";
};

/// Venue concierge with amenity info
class ConciergeAgent : public agent::AgentBase {
 public:
  explicit ConciergeAgent(const std::string& name = "concierge", const std::string& route = "/",
                          const std::string& host = "0.0.0.0", int port = 3000);

  ConciergeAgent& set_venue_name(const std::string& name);
  ConciergeAgent& set_amenities(const std::vector<json>& amenities);
  ConciergeAgent& set_hours(const json& hours);
  /// The services the venue offers (reference: ``self.services``).
  ConciergeAgent& set_services(const std::vector<std::string>& services);
  /// Extra guidance folded into the prompt (reference:
  /// ``self.special_instructions``).
  ConciergeAgent& set_special_instructions(const std::vector<std::string>& instructions);

  // Configuration the reference keeps as public instance attributes.
  /// reference: ``self.venue_name``
  [[nodiscard]] const std::string& venue_name() const { return venue_name_; }
  /// reference: ``self.amenities`` — the amenity objects ({name, description,
  /// location, …}).
  [[nodiscard]] const std::vector<json>& amenities() const { return amenities_; }
  /// reference: ``self.services``
  [[nodiscard]] const std::vector<std::string>& services() const { return services_; }
  /// reference: ``self.hours_of_operation`` — day -> hours; defaults to
  /// ``{"default": "9 AM - 5 PM"}``.
  [[nodiscard]] const json& hours_of_operation() const { return hours_of_operation_; }
  /// reference: ``self.special_instructions``
  [[nodiscard]] const std::vector<std::string>& special_instructions() const {
    return special_instructions_;
  }

  /// SWAIG tool handler: check availability of an amenity/service on a date
  /// and time. Returns an availability confirmation when the amenity is
  /// offered, otherwise lists the available amenities.
  swaig::FunctionResult check_availability(const json& args, const json& raw_data);

  /// SWAIG tool handler: provide directions to a location or amenity. If the
  /// location is a known amenity with a `location` field returns it;
  /// otherwise points the guest at the front desk.
  swaig::FunctionResult get_directions(const json& args, const json& raw_data);

  /// Register a post-prompt summary callback (Python ConciergeAgent.on_summary).
  /// Wires through to AgentBase::on_summary.
  ConciergeAgent& on_summary(agent::SummaryCallback cb);

 private:
  std::string venue_name_;
  std::vector<json> amenities_;
  std::vector<std::string> services_;
  json hours_of_operation_ = json::object({{"default", "9 AM - 5 PM"}});
  std::vector<std::string> special_instructions_;
};

}  // namespace prefabs
}  // namespace signalwire
