// Copyright (c) 2025 SignalWire — MIT License
// Joke skill demo using the modular skills system with DataMap.
// Compare with joke_agent.cpp (raw data_map).
// Required: API_NINJAS_KEY environment variable.

#include <signalwire/agent/agent_base.hpp>
#include <cstdlib>

using namespace signalwire;

int main() {
    const char* api_key = std::getenv("API_NINJAS_KEY");
    if (!api_key || std::string(api_key).empty()) {
        std::cerr << "Error: API_NINJAS_KEY environment variable is required.\n";
        std::cerr << "Get your free API key from https://api.api-ninjas.com/\n";
        return 1;
    }

    agent::AgentBase agent("joke-skill-demo", "/joke-skill");

    agent.prompt_add_section("Personality",
        "You are a cheerful comedian who loves sharing jokes.");
    agent.prompt_add_section("Instructions", "", {
        "When users ask for jokes, use your joke functions",
        "Be enthusiastic and fun in your responses",
        "You can tell both regular jokes and dad jokes"
    });

    agent.add_language({"English", "en-US", "inworld.Mark"});
    agent.set_params({{"ai_model", "gpt-4.1-nano"}});

    agent.add_skill("joke", {{"api_key", api_key}});

    std::cout << "Joke Skill Demo (modular skills system)\n";
    std::cout << "  Benefits over raw DataMap:\n";
    std::cout << "    - One-liner integration via skills system\n";
    std::cout << "    - Automatic validation and error handling\n";
    std::cout << "    - Reusable across agents\n";
    agent.run();
}
