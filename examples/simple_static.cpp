// Copyright (c) 2025 SignalWire — MIT License
// Simple static agent: no tools, just a prompt-driven conversation.

#include <signalwire/agent/agent_base.hpp>

using namespace signalwire;

int main() {
    agent::AgentBase agent("static-agent", "/static");

    agent.set_prompt_text(
        "You are a friendly greeter for Acme Corp. "
        "Welcome callers, answer basic questions about the company, "
        "and offer to transfer them to the right department. "
        "Acme Corp is open Monday-Friday 9AM-5PM.");

    agent.set_params({{"ai_model", "gpt-4.1-nano"}, {"wait_for_user", false}});
    agent.add_language({"English", "en-US", "inworld.Mark"});
    agent.add_hints({"Acme", "Acme Corp"});

    std::cout << "Static agent at http://0.0.0.0:3000/static\n";
    agent.run();
}
