// Copyright (c) 2025 SignalWire — MIT License
// Web search skill with multiple instances (general, news, quick) plus Wikipedia.
// Required: GOOGLE_SEARCH_API_KEY, GOOGLE_SEARCH_ENGINE_ID

#include <signalwire/agent/agent_base.hpp>
#include <cstdlib>

using namespace signalwire;

int main() {
    const char* api_key = std::getenv("GOOGLE_SEARCH_API_KEY");
    const char* engine_id = std::getenv("GOOGLE_SEARCH_ENGINE_ID");

    agent::AgentBase agent("multi-search", "/multi-search");

    agent.prompt_add_section("Role",
        "You are a research assistant with access to multiple search tools. "
        "Use the most appropriate tool for each query.");

    agent.add_language({"English", "en-US", "inworld.Mark"});
    agent.set_params({{"ai_model", "gpt-4.1-nano"}});

    agent.add_skill("datetime", {});
    agent.add_skill("math", {});

    // Wikipedia search
    agent.add_skill("wikipedia_search", {{"num_results", 2}});

    if (!api_key || !engine_id ||
        std::string(api_key).empty() || std::string(engine_id).empty()) {
        std::cout << "Warning: Missing GOOGLE_SEARCH_API_KEY or GOOGLE_SEARCH_ENGINE_ID.\n";
        std::cout << "Web search instances will not be available.\n";
    } else {
        // General web search (default tool name)
        agent.add_skill("web_search", {
            {"api_key", api_key},
            {"search_engine_id", engine_id},
            {"num_results", 3}
        });

        // News search
        agent.add_skill("web_search", {
            {"api_key", api_key},
            {"search_engine_id", engine_id},
            {"tool_name", "search_news"},
            {"num_results", 5}
        });

        // Quick single-result search
        agent.add_skill("web_search", {
            {"api_key", api_key},
            {"search_engine_id", engine_id},
            {"tool_name", "quick_search"},
            {"num_results", 1}
        });
    }

    std::cout << "Multi-search agent at http://0.0.0.0:3000/multi-search\n";
    std::cout << "Tools: web_search, search_news, quick_search, search_wiki\n";
    agent.run();
}
