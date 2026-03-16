// Copyright (c) 2025 SignalWire — MIT License
// Wikipedia search agent using the wikipedia_search skill.

#include <signalwire/agent/agent_base.hpp>

using namespace signalwire;

int main() {
    agent::AgentBase agent("wikipedia", "/wikipedia");

    agent.prompt_add_section("Role", "You are a knowledge assistant with Wikipedia access.");
    agent.prompt_add_section("Instructions", "", {
        "Search Wikipedia when users ask about topics",
        "Provide concise summaries from Wikipedia articles"
    });

    agent.add_skill("wikipedia_search");
    agent.add_skill("datetime");
    agent.add_language({"English", "en-US", "inworld.Mark"});

    std::cout << "Wikipedia agent at http://0.0.0.0:3000/wikipedia\n";
    agent.run();
}
