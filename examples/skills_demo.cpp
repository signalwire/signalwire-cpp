// Copyright (c) 2025 SignalWire — MIT License
// Skills system demo: one-liner skill injection.

#include <signalwire/agent/agent_base.hpp>

using namespace signalwire;

int main() {
    agent::AgentBase agent("Multi-Skill Assistant", "/assistant");
    agent.add_language({"English", "en-US", "inworld.Mark"});

    // Add built-in skills
    agent.add_skill("datetime");
    agent.add_skill("math");

    // Web search with custom params
    agent.add_skill("web_search", {
        {"api_key", "your-google-api-key"},
        {"search_engine_id", "your-engine-id"},
        {"num_results", 1}
    });

    auto skills = agent.list_skills();
    std::cout << "Loaded skills:";
    for (const auto& s : skills) std::cout << " " << s;
    std::cout << "\n";

    std::cout << "Skills demo at http://0.0.0.0:3000/assistant\n";
    agent.run();
}
