// Copyright (c) 2025 SignalWire — MIT License
// Dynamic-intake InfoGatherer: pick one of several static question sets
// at configuration time. The C++ port exposes a static set_questions()
// surface; per-request dynamic selection is not yet implemented in C++.

#include <signalwire/agent/agent_base.hpp>
#include <signalwire/prefabs/prefabs.hpp>
#include <signalwire/common.hpp>
#include <iostream>

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

    // Choose the question set at startup via the QUESTION_SET env var.
    std::string set_name = signalwire::get_env("QUESTION_SET", "default");
    auto selected = question_sets.find(set_name);
    if (selected == question_sets.end()) {
        std::cout << "Unknown QUESTION_SET=" << set_name << "; using default.\n";
        selected = question_sets.find("default");
    }

    std::vector<json> questions;
    for (const auto& q : selected->second) {
        questions.push_back(q);
    }

    prefabs::InfoGathererAgent gatherer("dynamic-intake", "/contact");
    gatherer.set_questions(questions);

    std::cout << "InfoGatherer (" << set_name << ") at http://0.0.0.0:3000/contact\n";
    std::cout << "Select a set at startup with QUESTION_SET={default|support|medical|onboarding}\n";
    gatherer.run();
}
