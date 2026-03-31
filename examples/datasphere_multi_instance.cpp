// Copyright (c) 2025 SignalWire — MIT License
// DataSphere skill with multiple instances and custom tool names.

#include <signalwire/agent/agent_base.hpp>

using namespace signalwire;

int main() {
    agent::AgentBase agent("multi-datasphere", "/datasphere-multi");

    agent.prompt_add_section("Role",
        "You are an assistant with access to multiple knowledge bases. "
        "Use the appropriate search tool depending on the topic.");

    agent.add_language({"English", "en-US", "inworld.Mark"});
    agent.set_params({{"ai_model", "gpt-4.1-nano"}});

    agent.add_skill("datetime", {});
    agent.add_skill("math", {});

    // Instance 1: Drinks knowledge
    agent.add_skill("datasphere", {
        {"document_id", "drinks-doc-123"},
        {"tool_name", "search_drinks_knowledge"},
        {"count", 2},
        {"distance", 5.0}
    });

    // Instance 2: Food knowledge
    agent.add_skill("datasphere", {
        {"document_id", "food-doc-456"},
        {"tool_name", "search_food_knowledge"},
        {"count", 3},
        {"distance", 4.0}
    });

    // Instance 3: General knowledge (default tool name)
    agent.add_skill("datasphere", {
        {"document_id", "general-doc-789"},
        {"count", 1},
        {"distance", 3.0}
    });

    std::cout << "Multi-DataSphere agent at http://0.0.0.0:3000/datasphere-multi\n";
    std::cout << "Tools: search_drinks_knowledge, search_food_knowledge, search_knowledge\n";
    std::cout << "Note: Replace document IDs with your actual DataSphere documents.\n";
    agent.run();
}
