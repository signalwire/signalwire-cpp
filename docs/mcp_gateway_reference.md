# MCP to SWAIG Gateway

## Overview

The MCP-to-SWAIG gateway bridges Model Context Protocol (MCP) servers with
SignalWire AI Gateway (SWAIG) functions, letting a SignalWire AI agent call
MCP-based tools. There are two independent pieces:

1. **The gateway service** — a standalone HTTP server that manages MCP server
   processes, sessions, and protocol translation. It is a separate, operator-run
   component; it is **not** part of the C++ SDK. Deploy it once and point your
   agents at it. See [Running a gateway server](#running-a-gateway-server).
2. **The `mcp_gateway` skill** — a built-in skill in this C++ SDK that connects an
   agent to a running gateway and exposes each MCP service as SWAIG tools.

This document covers the SDK-side skill — what you configure in C++ — and the
language-neutral wire contract the skill speaks to the gateway.

## Using the skill

`mcp_gateway` is a built-in skill; no extra dependency is required. Add it to an
agent with `add_skill`, passing a JSON object of parameters:

```cpp
#include <signalwire/signalwire.hpp>

using signalwire::agent::AgentBase;

class McpAgent : public AgentBase {
 public:
  McpAgent() : AgentBase("mcp-agent", "/mcp") {
    add_skill("mcp_gateway", {
        {"gateway_url", "https://gateway.example.com:8080"},
        {"tool_prefix", "mcp_"},
        {"services", {
            {{"name", "todo"}},
            {{"name", "calculator"}},
        }},
    });
  }
};
```

For each entry in `services`, the skill registers one SWAIG function named
`<tool_prefix><service_name>_query` (with the default prefix, `mcp_todo_query`
and `mcp_calculator_query`). The skill also contributes prompt sections and hints
that tell the AI which MCP services it is connected to, and publishes the gateway
URL, session id, and connected service names into the agent's global data
(`mcp_gateway_url`, `mcp_session_id`, `mcp_services`).

`setup()` requires a non-empty `gateway_url`; the skill fails to load without one.

## Configuration parameters

The skill's `setup()` reads exactly these keys from the parameter object:

| Key           | Type            | Default  | Description                                                                 |
|---------------|-----------------|----------|-----------------------------------------------------------------------------|
| `gateway_url` | string          | (required) | URL of the running MCP gateway service. Loading fails if empty.           |
| `tool_prefix` | string          | `mcp_`   | Prefix for the SWAIG function names the skill registers.                    |
| `services`    | array of object | `[]`     | MCP services to expose. Each element is an object with a `name` field.      |

Each element of `services` is an object; the skill reads its `name`:

```cpp
add_skill("mcp_gateway", {
    {"gateway_url", "https://gateway.example.com:8080"},
    {"services", {
        {{"name", "todo"}},
    }},
});
```

Authentication, session timeouts, retries, and TLS verification are properties of
the gateway **service** and its clients, not of this skill's public parameters —
configure them where you stand up the gateway (see below). The skill itself only
needs the gateway URL, an optional tool-name prefix, and the list of services to
surface.

## Protocol flow

The skill and the gateway service exchange messages in this order over the call's
lifetime:

```
SignalWire Agent                 Gateway Service              MCP Server
      |                                |                          |
      |---(1) Add Skill--------------->|                          |
      |<--(2) Query Tools--------------|                          |
      |                                |---(3) List Tools-------->|
      |                                |<--(4) Tool List----------|
      |---(5) Call SWAIG Function----->|                          |
      |                                |---(6) Spawn Session----->|
      |                                |---(7) Call MCP Tool----->|
      |                                |<--(8) MCP Response-------|
      |<--(9) SWAIG Response-----------|                          |
      |                                |                          |
      |---(10) Hangup Hook------------>|                          |
      |                                |---(11) Close Session---->|
```

## Message envelope format

When the skill invokes an MCP tool it POSTs a JSON envelope to the gateway's
`/services/<name>/call` endpoint. The envelope shape is the language-neutral
contract between any SDK's client skill and the gateway:

```json
{
    "session_id": "call_xyz123",
    "service": "todo",
    "tool": "add_todo",
    "arguments": {
        "text": "Buy milk"
    },
    "timeout": 300,
    "metadata": {
        "agent_id": "mcp-agent",
        "call_id": "call_xyz123"
    }
}
```

- `session_id` is derived from the SWAIG `call_id` so tool calls within the same
  SignalWire call share one MCP session.
- `service` and `tool` select the MCP server and the tool on it.
- `arguments` carries the tool arguments the AI supplied.
- `metadata` carries the agent and call identity for logging and routing.

The gateway responds with the MCP tool's result, which the skill returns to the
AI as a SWAIG function result.

## Running a gateway server

The gateway service is a separate, operator-run component; **this C++ SDK does not
ship or run it**. To stand one up, use the SignalWire-provided gateway service and
consult its operator documentation for configuration (services to expose,
authentication, session limits, TLS) and deployment. Point the skill's
`gateway_url` at the URL where you run it.

Once the gateway is running, verify it is reachable at the URL you will hand the
skill, then supply that URL via `gateway_url`.

## Testing with the swaig-test CLI

The SDK ships a `swaig-test` CLI (`bin/swaig-test`) that lists and invokes an
agent's SWAIG functions without a live SignalWire call. Point it at a running
agent's URL:

```bash
# List the registered MCP tools
bin/swaig-test http://localhost:3000/mcp --list-tools

# Invoke one of the generated MCP tools
bin/swaig-test http://localhost:3000/mcp --exec mcp_todo_query --param query="add: Buy milk"

# Dump the generated SWML document
bin/swaig-test http://localhost:3000/mcp --dump-swml
```

## Troubleshooting

1. **Skill fails to load** — `setup()` returns false when `gateway_url` is empty.
   Confirm you passed a non-empty `gateway_url`.
2. **No MCP tools registered** — the skill registers one tool per entry in
   `services`; confirm each entry is an object with a `name` field.
3. **Tool names unexpected** — SWAIG function names are
   `<tool_prefix><service_name>_query`; adjust `tool_prefix` if they collide with
   other skills' tools.
4. **Gateway unreachable** — the gateway service is a separate component; confirm
   it is deployed and reachable at `gateway_url` before running the agent.

## Examples

- See `examples/` in this repository for an agent that connects to MCP services
  through the `mcp_gateway` skill.
