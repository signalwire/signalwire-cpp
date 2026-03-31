// Copyright (c) 2025 SignalWire — MIT License
// Dynamic InfoGatherer: selects questions based on request parameters.
// Test: ?set=support, ?set=medical, ?set=onboarding

#include <signalwire/agent/agent_base.hpp>
#include <signalwire/prefabs/info_gatherer.hpp>

using namespace signalwire;
using json = nlohmann::json;

int main() {
    // Define question sets
    std::map<std::string, json> question_sets;

    question_sets["default"] = json::array({
        {{"key_name", "name"},   {"question_text", "What is your full name?"}},
        {{"key_name", "phone"},  {"question_text", "What is your phone number?"}, {"confirm", true}},
        {{"key_name", "reason"}, {"question_text", "How can I help you today?"}}
    });

    question_sets["support"] = json::array({
        {{"key_name", "customer_name"},  {"question_text", "What is your name?"}},
        {{"key_name", "account_number"}, {"question_text", "What is your account number?"}, {"confirm", true}},
        {{"key_name", "issue"},          {"question_text", "What issue are you experiencing?"}},
        {{"key_name", "priority"},       {"question_text", "How urgent is this? (Low, Medium, High)"}}
    });

    question_sets["medical"] = json::array({
        {{"key_name", "patient_name"}, {"question_text", "What is the patient's full name?"}},
        {{"key_name", "symptoms"},     {"question_text", "What symptoms are you experiencing?"}, {"confirm", true}},
        {{"key_name", "duration"},     {"question_text", "How long have you had these symptoms?"}},
        {{"key_name", "medications"},  {"question_text", "Are you currently taking any medications?"}}
    });

    question_sets["onboarding"] = json::array({
        {{"key_name", "full_name"},  {"question_text", "What is your full name?"}},
        {{"key_name", "email"},      {"question_text", "What is your email address?"}, {"confirm", true}},
        {{"key_name", "company"},    {"question_text", "What company do you work for?"}},
        {{"key_name", "department"}, {"question_text", "What department?"}},
        {{"key_name", "start_date"}, {"question_text", "What is your start date?"}}
    });

    prefabs::InfoGathererAgent gatherer("dynamic-intake", "/contact");

    gatherer.set_question_callback([&question_sets](
        const std::map<std::string, std::string>& query_params) -> json {
        auto it = query_params.find("set");
        std::string set = (it != query_params.end()) ? it->second : "default";
        std::cout << "Dynamic question set: " << set << "\n";
        auto qs = question_sets.find(set);
        return (qs != question_sets.end()) ? qs->second : question_sets["default"];
    });

    std::cout << "Dynamic InfoGatherer at http://0.0.0.0:3000/contact\n";
    std::cout << "  /contact            (default)\n";
    std::cout << "  /contact?set=support (customer support)\n";
    std::cout << "  /contact?set=medical (medical intake)\n";
    std::cout << "  /contact?set=onboarding (employee onboarding)\n";
    gatherer.run();
}
