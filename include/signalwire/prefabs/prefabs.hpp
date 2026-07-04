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

  /// Register a post-prompt summary callback (Python FAQBotAgent.on_summary).
  /// Wires through to AgentBase::on_summary.
  FAQBotAgent& on_summary(agent::SummaryCallback cb);

  /// SWAIG tool handler: search FAQs matching a query and/or category.
  /// Scores each FAQ (substring match on the question, prefix boost, and
  /// category match), sorts by score, returns the top 3 matching questions.
  swaig::FunctionResult search_faqs(const json& args, const json& raw_data);

 private:
  std::vector<json> faqs_;
};

/// Venue concierge with amenity info
class ConciergeAgent : public agent::AgentBase {
 public:
  explicit ConciergeAgent(const std::string& name = "concierge", const std::string& route = "/",
                          const std::string& host = "0.0.0.0", int port = 3000);

  ConciergeAgent& set_venue_name(const std::string& name);
  ConciergeAgent& set_amenities(const std::vector<json>& amenities);
  ConciergeAgent& set_hours(const json& hours);

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
};

}  // namespace prefabs
}  // namespace signalwire
