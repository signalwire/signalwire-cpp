// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#pragma once

#include "signalwire/agent/agent_base.hpp"

namespace signalwire {
namespace prefabs {

using json = nlohmann::json;

/// Sequential question collection with key/value answers
class InfoGathererAgent : public agent::AgentBase {
public:
    explicit InfoGathererAgent(const std::string& name = "info_gatherer",
                               const std::string& route = "/",
                               const std::string& host = "0.0.0.0",
                               int port = 3000);

    InfoGathererAgent& set_questions(const std::vector<json>& questions);
    InfoGathererAgent& set_completion_message(const std::string& msg);
    InfoGathererAgent& set_prefix(const std::string& prefix);
};

/// Typed surveys with validation
class SurveyAgent : public agent::AgentBase {
public:
    explicit SurveyAgent(const std::string& name = "survey",
                         const std::string& route = "/",
                         const std::string& host = "0.0.0.0",
                         int port = 3000);

    SurveyAgent& set_questions(const std::vector<json>& questions);
    SurveyAgent& set_completion_message(const std::string& msg);
    SurveyAgent& set_intro_message(const std::string& msg);
};

/// Department routing with call transfer
class ReceptionistAgent : public agent::AgentBase {
public:
    explicit ReceptionistAgent(const std::string& name = "receptionist",
                               const std::string& route = "/",
                               const std::string& host = "0.0.0.0",
                               int port = 3000);

    ReceptionistAgent& set_departments(const json& departments);
    ReceptionistAgent& set_greeting(const std::string& greeting);
    ReceptionistAgent& set_transfer_message(const std::string& msg);
};

/// Keyword-based FAQ matching
class FAQBotAgent : public agent::AgentBase {
public:
    explicit FAQBotAgent(const std::string& name = "faq_bot",
                         const std::string& route = "/",
                         const std::string& host = "0.0.0.0",
                         int port = 3000);

    FAQBotAgent& set_faqs(const std::vector<json>& faqs);
    FAQBotAgent& set_no_match_message(const std::string& msg);
    FAQBotAgent& set_suggest_related(bool suggest);
};

/// Venue concierge with amenity info
class ConciergeAgent : public agent::AgentBase {
public:
    explicit ConciergeAgent(const std::string& name = "concierge",
                            const std::string& route = "/",
                            const std::string& host = "0.0.0.0",
                            int port = 3000);

    ConciergeAgent& set_venue_name(const std::string& name);
    ConciergeAgent& set_amenities(const std::vector<json>& amenities);
    ConciergeAgent& set_hours(const json& hours);
};

} // namespace prefabs
} // namespace signalwire
