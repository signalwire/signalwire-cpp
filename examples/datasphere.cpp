// Copyright (c) 2025 SignalWire — MIT License
// Datasphere agent: knowledge search via SignalWire Datasphere.

#include <signalwire/agent/agent_base.hpp>
#include <signalwire/common.hpp>
#include <iostream>

using namespace signalwire;

int main() {
    agent::AgentBase agent("datasphere", "/datasphere");

    agent.prompt_add_section("Role", "You are a knowledge agent with Datasphere access.");
    agent.prompt_add_section("Instructions", "", {
        "Search the knowledge base when users ask questions",
        "Provide accurate answers based on indexed documents"
    });

    agent.add_skill("datasphere", {
        {"document_id", signalwire::get_env("DATASPHERE_DOCUMENT_ID")}
    });

    agent.set_params({{"ai_model", "gpt-4.1-nano"}});
    agent.add_language({"English", "en-US", "inworld.Mark"});

    std::cout << "Datasphere agent at http://0.0.0.0:3000/datasphere\n";
    std::cout << "Requires: SIGNALWIRE_PROJECT_ID, SIGNALWIRE_API_TOKEN, DATASPHERE_DOCUMENT_ID\n";
    agent.run();
}
