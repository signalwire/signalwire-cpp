// Copyright (c) 2025 SignalWire — MIT License
// Survey prefab: typed survey with validation.

#include <signalwire/prefabs/prefabs.hpp>

using namespace signalwire;
using json = nlohmann::json;

int main() {
    prefabs::SurveyAgent agent("satisfaction-survey", "/survey");

    agent.set_intro_message("Welcome to our customer satisfaction survey!");
    agent.set_questions({
        {{"key", "rating"}, {"question", "On a scale of 1-10, how satisfied are you?"}, {"type", "integer"}},
        {{"key", "recommend"}, {"question", "Would you recommend us to a friend?"}, {"type", "boolean"}},
        {{"key", "feedback"}, {"question", "Any additional feedback?"}, {"type", "string"}}
    });
    agent.set_completion_message("Thank you for completing our survey!");

    std::cout << "Survey at http://0.0.0.0:3000/survey\n";
    agent.run();
}
