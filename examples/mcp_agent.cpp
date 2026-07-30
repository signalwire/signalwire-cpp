// Copyright (c) 2025 SignalWire -- MIT License
// MCP Integration example: Client and Server
//
// This agent demonstrates both MCP features:
//
// 1. MCP Server: Exposes tools at /mcp so external MCP clients
//    (Claude Desktop, other agents) can discover and invoke them.
//
// 2. MCP Client: Connects to an external MCP server to pull in additional
//    tools for voice calls.
//
// Build: cmake --build build
// Run:   ./build/examples/mcp_agent

#include <signalwire/agent/agent_base.hpp>

using namespace signalwire;
using json = nlohmann::json;

class McpAgent : public agent::AgentBase {
 public:
  McpAgent() : AgentBase("mcp-agent", "/agent") {
    // -- MCP Server --
    // Adds a /mcp endpoint that speaks JSON-RPC 2.0 (MCP protocol).
    enable_mcp_server();

    // -- MCP Client --
    // Connect to an external MCP server. Tools are discovered automatically.
    add_mcp_server("https://mcp.example.com/tools",
                   {{"Authorization", "Bearer sk-your-mcp-api-key"}});

    // -- MCP Client with Resources --
    add_mcp_server("https://mcp.example.com/crm", {{"Authorization", "Bearer sk-your-crm-key"}},
                   true, {{"caller_id", "${caller_id_number}"}, {"tenant", "acme-corp"}});

    // -- Agent Configuration --
    prompt_add_section("Role",
                       "You are a helpful customer support agent. "
                       "You have access to the customer's profile via global_data.");

    set_params({{"attention_timeout", 15000}});

    // -- Local Tools --
    define_tool("get_weather", "Get the current weather for a location",
                json::object(
                    {{"type", "object"},
                     {"properties",
                      json::object({{"location",
                                     json::object({{"type", "string"},
                                                   {"description", "City name or zip code"}})}})}}),
                [](const json& args, const json&) -> swaig::FunctionResult {
                  std::string location = args.value("location", "unknown");
                  return swaig::FunctionResult("Currently 72F and sunny in " + location + ".");
                });

    define_tool(
        "create_ticket", "Create a support ticket",
        json::object(
            {{"type", "object"},
             {"properties",
              json::object(
                  {{"subject",
                    json::object({{"type", "string"}, {"description", "Ticket subject"}})},
                   {"description",
                    json::object({{"type", "string"}, {"description", "Issue description"}})}})}}),
        [](const json& args, const json&) -> swaig::FunctionResult {
          std::string subject = args.value("subject", "No subject");
          return swaig::FunctionResult("Ticket created: '" + subject + "'. Reference: TK-12345.");
        });
  }
};

int main() {
  McpAgent agent;
  agent.run();
  return 0;
}
