# MCP Integration

The SDK supports the [Model Context Protocol (MCP)](https://modelcontextprotocol.io/) in two ways:

1. **MCP Client** -- Connect to external MCP servers and use their tools in your agent
2. **MCP Server** -- Expose your agent's tools as an MCP endpoint for other clients

These features are independent and can be used separately or together.

## Adding External MCP Servers

Use `add_mcp_server()` to connect your agent to remote MCP servers. Tools are discovered at call start via the MCP protocol and added to the AI's tool list alongside your defined tools.

<!-- snippet-setup -->
```cpp
#include <signalwire/agent/agent_base.hpp>
#include <signalwire/swaig/function_result.hpp>
#include <signalwire/swaig/parameter_schema.hpp>
#include <signalwire/datamap/datamap.hpp>
#include <signalwire/contexts/contexts.hpp>
#include <signalwire/prefabs/prefabs.hpp>
#include <signalwire/server/agent_server.hpp>
#include <nlohmann/json.hpp>
#include <iostream>
using json = nlohmann::json;
signalwire::agent::AgentBase agent("my-agent");
signalwire::swaig::FunctionResult result("ok");
```

```cpp
agent.add_mcp_server("https://mcp.example.com/tools",
    {{"Authorization", "Bearer sk-xxx"}});
```

### Parameters

| Parameter | Type | Description |
|---|---|---|
| `url` | std::string | MCP server HTTP endpoint URL |
| `headers` | std::map<std::string, std::string> | Optional HTTP headers for authentication |
| `resources` | bool | Fetch resources into `global_data` (default: false) |
| `resource_vars` | std::map<std::string, std::string> | Variables for URI template substitution |

### With Resources

```cpp
agent.add_mcp_server("https://mcp.example.com/crm",
    {{"Authorization", "Bearer sk-xxx"}},
    true,
    {{"caller_id", "${caller_id_number}"}});
```

### Multiple Servers

```cpp
agent.add_mcp_server("https://mcp-search.example.com/tools",
    {{"Authorization", "Bearer search-key"}});
agent.add_mcp_server("https://mcp-crm.example.com/tools",
    {{"Authorization", "Bearer crm-key"}});
```

## Exposing Tools as MCP Server

Use `enable_mcp_server()` to add an MCP endpoint at `/mcp` on your agent's server.

```cpp
agent.enable_mcp_server();

agent.define_tool("get_weather", "Get weather for a location",
    json::object({{"type", "object"}, {"properties", json::object({
        {"location", json::object({{"type", "string"}})}
    })}}),
    [](const json& args, const json&) -> signalwire::swaig::FunctionResult {
        return signalwire::swaig::FunctionResult("72F sunny in " + args.value("location", "unknown"));
    });
```

The `/mcp` endpoint handles:
- `initialize` -- protocol version and capability negotiation
- `notifications/initialized` -- ready signal
- `tools/list` -- returns all tools in MCP format
- `tools/call` -- invokes the handler and returns the result
- `ping` -- keepalive

### Connecting from Claude Desktop

```json
{
    "mcpServers": {
        "my-agent": {
            "url": "https://your-server.com/agent/mcp"
        }
    }
}
```

## Using Both Together

```cpp
agent.enable_mcp_server();
agent.add_mcp_server("https://mcp.example.com/crm",
    {{"Authorization", "Bearer sk-xxx"}},
    true, {});
```

## MCP vs SWAIG Webhooks

| | SWAIG Webhooks | MCP Tools |
|---|---|---|
| Response format | JSON with `response`, `action`, `SWML` | Text content only |
| Call control | Can trigger hold, transfer, SWML | Response only |
| Discovery | Defined in SWML config | Auto-discovered via protocol |
| Auth | `web_hook_auth_user/password` | `headers` dict |

MCP tools are best for data retrieval. Use tool handlers with SWAIG webhooks when you need call control actions.
