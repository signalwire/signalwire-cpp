// Copyright (c) 2025 SignalWire — MIT License
// InfoGatherer prefab: sequential question collection.

#include <signalwire/prefabs/prefabs.hpp>

using namespace signalwire;
using json = nlohmann::json;

int main() {
    prefabs::InfoGathererAgent agent("contact-form", "/contact-form");

    agent.set_questions({
        {{"key", "name"}, {"question", "What is your full name?"}},
        {{"key", "email"}, {"question", "What is your email address?"}},
        {{"key", "phone"}, {"question", "What is your phone number?"}},
        {{"key", "reason"}, {"question", "How can we help you today?"}}
    });

    agent.set_completion_message("Thank you! We have your information and will follow up.");
    agent.set_prefix("contact");

    std::cout << "InfoGatherer at http://0.0.0.0:3000/contact-form\n";
    agent.run();
}
