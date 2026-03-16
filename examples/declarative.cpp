// Copyright (c) 2025 SignalWire — MIT License
// Declarative agent: build entirely from JSON-like config.

#include <signalwire/agent/agent_base.hpp>

using namespace signalwire;
using json = nlohmann::json;

int main() {
    agent::AgentBase agent("declarative", "/declarative");

    // Configure everything declaratively
    agent.set_prompt_text("You are a helpful assistant for Acme Corp.");
    agent.set_post_prompt("Summarize the conversation as JSON.");
    agent.set_params({
        {"ai_model", "gpt-4.1-nano"},
        {"wait_for_user", false},
        {"end_of_speech_timeout", 1000}
    });
    agent.set_global_data({
        {"company", "Acme Corp"},
        {"department", "Sales"}
    });
    agent.add_hints({"Acme", "SWML"});
    agent.set_native_functions({"check_time"});
    agent.add_language({"English", "en-US", "inworld.Mark"});

    std::cout << "Declarative agent at http://0.0.0.0:3000/declarative\n";
    agent.run();
}
