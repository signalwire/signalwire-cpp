// Copyright (c) 2025 SignalWire — MIT License
// FAQ Bot prefab: keyword-based FAQ matching.

#include <signalwire/prefabs/prefabs.hpp>

using namespace signalwire;
using json = nlohmann::json;

int main() {
    prefabs::FAQBotAgent agent("faq", "/faq");

    agent.set_faqs({
        {{"question", "What are your hours?"}, {"answer", "We are open Monday-Friday 9AM-5PM EST."}},
        {{"question", "What is your return policy?"}, {"answer", "You can return items within 30 days for a full refund."}},
        {{"question", "How do I contact support?"}, {"answer", "Email support@example.com or call 1-800-EXAMPLE."}},
        {{"question", "Do you ship internationally?"}, {"answer", "Yes, we ship to over 50 countries."}}
    });
    agent.set_no_match_message("I don't have an answer for that. Let me connect you with a human agent.");
    agent.set_suggest_related(true);

    std::cout << "FAQ Bot at http://0.0.0.0:3000/faq\n";
    agent.run();
}
