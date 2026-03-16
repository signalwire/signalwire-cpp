// Copyright (c) 2025 SignalWire — MIT License
// Joke agent: uses the joke skill for entertainment.

#include <signalwire/agent/agent_base.hpp>

using namespace signalwire;

int main() {
    agent::AgentBase agent("joke-teller", "/jokes");

    agent.prompt_add_section("Personality", "You are a hilarious comedian.");
    agent.prompt_add_section("Instructions", "", {
        "Tell jokes when asked",
        "Keep the humor clean and family-friendly",
        "Use different joke categories to keep things fresh"
    });

    agent.add_skill("joke");
    agent.add_skill("datetime");
    agent.add_language({"English", "en-US", "inworld.Mark"});

    std::cout << "Joke agent at http://0.0.0.0:3000/jokes\n";
    agent.run();
}
