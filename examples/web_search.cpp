// Copyright (c) 2025 SignalWire — MIT License
// Web search agent using the web_search skill.

#include <signalwire/agent/agent_base.hpp>
#include <signalwire/common.hpp>
#include <iostream>

using namespace signalwire;

int main() {
    agent::AgentBase agent("web-search", "/web-search");

    agent.prompt_add_section("Role", "You are a research assistant with web search.");
    agent.prompt_add_section("Instructions", "", {
        "Search the web when users ask factual questions",
        "Cite your sources when providing information"
    });

    agent.add_skill("web_search", {
        {"api_key", signalwire::get_env("GOOGLE_SEARCH_API_KEY")},
        {"search_engine_id", signalwire::get_env("GOOGLE_SEARCH_ENGINE_ID")},
        {"num_results", 3}
    });

    agent.add_skill("datetime");
    agent.add_language({"English", "en-US", "inworld.Mark"});

    std::cout << "Web search agent at http://0.0.0.0:3000/web-search\n";
    std::cout << "Requires: GOOGLE_SEARCH_API_KEY, GOOGLE_SEARCH_ENGINE_ID\n";
    agent.run();
}
