// MCP Integration tests

#include "signalwire/agent/agent_base.hpp"

using namespace signalwire::agent;
using namespace signalwire::swaig;
using json = nlohmann::json;

static AgentBase make_mcp_agent() {
  AgentBase agent("test-mcp", "/test");
  agent.set_auth("u", "p");
  agent.enable_mcp_server();
  agent.define_tool(
      "get_weather", "Get the weather for a location",
      json::object({{"type", "object"},
                    {"properties",
                     json::object({{"location", json::object({{"type", "string"},
                                                              {"description", "City name"}})}})}}),
      [](const json& args, const json&) -> FunctionResult {
        std::string loc = args.value("location", "unknown");
        return FunctionResult("72F sunny in " + loc);
      });
  return agent;
}

// ========================================================================
// MCP Tool List
// ========================================================================

TEST(mcp_build_tool_list) {
  auto agent = make_mcp_agent();
  auto tools = agent.build_mcp_tool_list();
  ASSERT_EQ(tools.size(), 1u);
  ASSERT_EQ(tools[0]["name"].get<std::string>(), "get_weather");
  ASSERT_EQ(tools[0]["description"].get<std::string>(), "Get the weather for a location");
  ASSERT_TRUE(tools[0].contains("inputSchema"));
  ASSERT_EQ(tools[0]["inputSchema"]["type"].get<std::string>(), "object");
  return true;
}

// ========================================================================
// Initialize Handshake
// ========================================================================

TEST(mcp_initialize) {
  auto agent = make_mcp_agent();
  json resp = agent.handle_mcp_request(json::object(
      {{"jsonrpc", "2.0"},
       {"id", 1},
       {"method", "initialize"},
       {"params",
        json::object({{"protocolVersion", "2025-06-18"}, {"capabilities", json::object()}})}}));
  ASSERT_EQ(resp["jsonrpc"].get<std::string>(), "2.0");
  ASSERT_EQ(resp["id"].get<int>(), 1);
  ASSERT_TRUE(resp.contains("result"));
  ASSERT_EQ(resp["result"]["protocolVersion"].get<std::string>(), "2025-06-18");
  ASSERT_TRUE(resp["result"]["capabilities"].contains("tools"));
  return true;
}

// ========================================================================
// Initialized Notification
// ========================================================================

TEST(mcp_initialized_notification) {
  auto agent = make_mcp_agent();
  json resp = agent.handle_mcp_request(
      json::object({{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}}));
  ASSERT_TRUE(resp.contains("result"));
  return true;
}

// ========================================================================
// Tools List
// ========================================================================

TEST(mcp_tools_list) {
  auto agent = make_mcp_agent();
  json resp = agent.handle_mcp_request(json::object(
      {{"jsonrpc", "2.0"}, {"id", 2}, {"method", "tools/list"}, {"params", json::object()}}));
  ASSERT_EQ(resp["id"].get<int>(), 2);
  auto& tools = resp["result"]["tools"];
  ASSERT_EQ(tools.size(), 1u);
  ASSERT_EQ(tools[0]["name"].get<std::string>(), "get_weather");
  return true;
}

// ========================================================================
// Tools Call
// ========================================================================

TEST(mcp_tools_call) {
  auto agent = make_mcp_agent();
  json resp = agent.handle_mcp_request(json::object(
      {{"jsonrpc", "2.0"},
       {"id", 3},
       {"method", "tools/call"},
       {"params", json::object({{"name", "get_weather"},
                                {"arguments", json::object({{"location", "Orlando"}})}})}}));
  ASSERT_EQ(resp["id"].get<int>(), 3);
  ASSERT_EQ(resp["result"]["isError"].get<bool>(), false);
  auto& content = resp["result"]["content"];
  ASSERT_EQ(content.size(), 1u);
  ASSERT_EQ(content[0]["type"].get<std::string>(), "text");
  ASSERT_TRUE(content[0]["text"].get<std::string>().find("Orlando") != std::string::npos);
  return true;
}

// ========================================================================
// Tools Call Unknown
// ========================================================================

TEST(mcp_tools_call_unknown) {
  auto agent = make_mcp_agent();
  json resp = agent.handle_mcp_request(json::object(
      {{"jsonrpc", "2.0"},
       {"id", 4},
       {"method", "tools/call"},
       {"params", json::object({{"name", "nonexistent"}, {"arguments", json::object()}})}}));
  ASSERT_TRUE(resp.contains("error"));
  ASSERT_EQ(resp["error"]["code"].get<int>(), -32602);
  ASSERT_TRUE(resp["error"]["message"].get<std::string>().find("nonexistent") != std::string::npos);
  return true;
}

// ========================================================================
// Unknown Method
// ========================================================================

TEST(mcp_unknown_method) {
  auto agent = make_mcp_agent();
  json resp = agent.handle_mcp_request(json::object(
      {{"jsonrpc", "2.0"}, {"id", 5}, {"method", "resources/list"}, {"params", json::object()}}));
  ASSERT_TRUE(resp.contains("error"));
  ASSERT_EQ(resp["error"]["code"].get<int>(), -32601);
  return true;
}

// ========================================================================
// Ping
// ========================================================================

TEST(mcp_ping) {
  auto agent = make_mcp_agent();
  json resp =
      agent.handle_mcp_request(json::object({{"jsonrpc", "2.0"}, {"id", 6}, {"method", "ping"}}));
  ASSERT_TRUE(resp.contains("result"));
  return true;
}

// ========================================================================
// Invalid JSON-RPC Version
// ========================================================================

TEST(mcp_invalid_jsonrpc_version) {
  auto agent = make_mcp_agent();
  json resp = agent.handle_mcp_request(
      json::object({{"jsonrpc", "1.0"}, {"id", 7}, {"method", "initialize"}}));
  ASSERT_TRUE(resp.contains("error"));
  ASSERT_EQ(resp["error"]["code"].get<int>(), -32600);
  return true;
}

// ========================================================================
// add_mcp_server Tests
// ========================================================================

TEST(mcp_add_server_basic) {
  AgentBase agent("test", "/test");
  agent.add_mcp_server("https://mcp.example.com/tools");
  ASSERT_EQ(agent.mcp_servers().size(), 1u);
  ASSERT_EQ(agent.mcp_servers()[0]["url"].get<std::string>(), "https://mcp.example.com/tools");
  return true;
}

TEST(mcp_add_server_with_headers) {
  AgentBase agent("test", "/test");
  agent.add_mcp_server("https://mcp.example.com/tools", {{"Authorization", "Bearer sk-xxx"}});
  ASSERT_EQ(agent.mcp_servers()[0]["headers"]["Authorization"].get<std::string>(), "Bearer sk-xxx");
  return true;
}

TEST(mcp_add_server_with_resources) {
  AgentBase agent("test", "/test");
  agent.add_mcp_server("https://mcp.example.com/crm", {}, true,
                       {{"caller_id", "${caller_id_number}"}});
  ASSERT_EQ(agent.mcp_servers()[0]["resources"].get<bool>(), true);
  ASSERT_EQ(agent.mcp_servers()[0]["resource_vars"]["caller_id"].get<std::string>(),
            "${caller_id_number}");
  return true;
}

TEST(mcp_add_multiple_servers) {
  AgentBase agent("test", "/test");
  agent.add_mcp_server("https://mcp1.example.com");
  agent.add_mcp_server("https://mcp2.example.com");
  ASSERT_EQ(agent.mcp_servers().size(), 2u);
  return true;
}

TEST(mcp_enable_server) {
  AgentBase agent("test", "/test");
  ASSERT_FALSE(agent.is_mcp_server_enabled());
  agent.enable_mcp_server();
  ASSERT_TRUE(agent.is_mcp_server_enabled());
  return true;
}

// ========================================================================
// MCP Servers in SWML
// ========================================================================

TEST(mcp_servers_in_swml) {
  AgentBase agent("test", "/test");
  agent.set_auth("u", "p");
  agent.set_prompt_text("Test");
  agent.add_mcp_server("https://mcp.example.com/tools", {{"Authorization", "Bearer sk-xxx"}});
  // Add a tool so SWAIG section exists
  agent.define_tool("fn", "Test", json::object(), nullptr);

  json swml = agent.render_swml();
  auto& main = swml["sections"]["main"];
  for (const auto& verb : main) {
    if (verb.contains("ai") && verb["ai"].contains("SWAIG")) {
      auto& swaig = verb["ai"]["SWAIG"];
      ASSERT_TRUE(swaig.contains("mcp_servers"));
      ASSERT_EQ(swaig["mcp_servers"].size(), 1u);
      ASSERT_EQ(swaig["mcp_servers"][0]["url"].get<std::string>(), "https://mcp.example.com/tools");
      return true;
    }
  }
  ASSERT_TRUE(false);  // SWAIG section not found
  return true;
}
