// Copyright (c) 2025 SignalWire — MIT License
// MCP gateway demo: agent with MCP skill integration.

#include <signalwire/agent/agent_base.hpp>

using namespace signalwire;
using json = nlohmann::json;

int main() {
    agent::AgentBase agent("mcp-gateway", "/mcp-gateway");

    agent.prompt_add_section("Role", "You are an assistant with MCP tool access.");
    agent.prompt_add_section("Instructions", "", {
        "Use MCP tools when the user requests external operations",
        "Explain what tools are available if asked"
    });

    // MCP gateway skill
    agent.add_skill("mcp_gateway", {
        {"url", "http://localhost:8080/mcp"},
        {"timeout", 30}
    });

    agent.add_skill("datetime");
    agent.set_params({{"ai_model", "gpt-4.1-nano"}});

    std::cout << "MCP Gateway at http://0.0.0.0:3000/mcp-gateway\n";
    agent.run();
}
