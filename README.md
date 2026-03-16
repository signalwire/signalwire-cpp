# SignalWire AI Agents SDK for C++

A C++17 framework for building, deploying, and managing AI agents as microservices. The SDK provides tools for creating self-contained HTTP services that expose endpoints to interact with the SignalWire platform.

## Quick Start

```cpp
#include <signalwire/agent/agent_base.hpp>

using namespace signalwire;

class MyAgent : public agent::AgentBase {
public:
    MyAgent() : AgentBase("my-agent", "/my-agent") {
        prompt_add_section("Personality", "You are a friendly assistant.");

        define_tool("get_time", "Get the current time",
            {{"type", "object"}, {"properties", json::object()}},
            [](const json& args, const json& raw) -> swaig::FunctionResult {
                auto t = std::time(nullptr);
                char buf[64];
                std::strftime(buf, sizeof(buf), "%H:%M:%S", std::localtime(&t));
                return swaig::FunctionResult(std::string("The time is ") + buf);
            });
    }
};

int main() {
    MyAgent agent;
    agent.run();  // Serves on http://0.0.0.0:3000/my-agent
}
```

### C API (for C, Lua, Python FFI, etc.)

```c
#include <signalwire/signalwire_agents_c.h>

sw_function_result_t get_time_handler(const char* args, const char* raw, void* ud) {
    sw_function_result_t result = sw_result_create("The current time is 3:00 PM");
    return result;
}

int main() {
    sw_agent_t agent = sw_agent_create("my-agent");
    sw_agent_set_prompt(agent, "You are a helpful assistant.");
    sw_agent_define_tool(agent, "get_time", "Get the current time",
                         "{}", get_time_handler, NULL);
    sw_agent_run(agent);
    sw_agent_destroy(agent);
    return 0;
}
```

## Features

- **AgentBase** -- structured prompts (POM), SWAIG tools, skills, contexts, sessions
- **SWMLService** -- low-level SWML document builder with all 38 verbs
- **AgentServer** -- multi-agent hosting on a single port
- **DataMap** -- server-side API tools without webhooks
- **Contexts & Steps** -- multi-phase conversation workflows
- **Skills System** -- 18 built-in skills (datetime, math, web_search, datasphere, etc.)
- **Prefabs** -- InfoGatherer, Survey, Receptionist, FAQBot, Concierge
- **REST Client** -- synchronous HTTP client with 21 API namespaces
- **RELAY Client** -- real-time call control over WebSocket (raw TCP + OpenSSL TLS)
- **Security** -- HMAC-SHA256 session tokens, basic auth, timing-safe compare
- **C API** -- `extern "C"` wrapper for FFI from C, Python, Ruby, etc.

## Building

### Prerequisites

- C++17 compiler (GCC 8+, Clang 7+, MSVC 2019+)
- CMake 3.16+
- OpenSSL development libraries
- pthreads

### Build Steps

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
```

### Run Tests

```bash
cd build
./run_tests
```

The test suite contains 258 tests covering all components.

### Build Your Own Agent

```bash
# Compile against the static library
g++ -std=c++17 -I include -I deps my_agent.cpp -L build -lsignalwire_agents -lssl -lcrypto -lpthread -o my_agent
```

Or add to your CMakeLists.txt:

```cmake
add_executable(my_agent my_agent.cpp)
target_link_libraries(my_agent signalwire_agents)
```

## Project Structure

```
signalwire-agents-cpp/
    include/signalwire/         # Public headers
        agent/agent_base.hpp    # AgentBase class
        server/agent_server.hpp # Multi-agent server
        swml/                   # SWML document, service, schema
        swaig/                  # FunctionResult, ToolDefinition
        datamap/datamap.hpp     # DataMap builder
        contexts/contexts.hpp   # Contexts, Steps, GatherInfo
        skills/                 # SkillBase, SkillManager, SkillRegistry
        prefabs/prefabs.hpp     # Pre-built agent types
        rest/                   # REST client + HTTP client
        security/               # SessionManager (HMAC tokens)
        signalwire_agents.hpp   # Umbrella header
        signalwire_agents_c.h   # C wrapper API
    src/                        # Implementation files
    tests/                      # 258 unit tests
    examples/                   # 37+ standalone examples
    relay/                      # RELAY client docs + examples
    rest/                       # REST client docs + examples
    docs/                       # Guides (architecture, agent, SWAIG, etc.)
    deps/                       # Vendored: nlohmann/json, cpp-httplib
    bin/                        # CLI tools (swaig-test)
```

## Core Concepts

### Prompt Object Model (POM)

Build structured prompts from sections, bullets, and subsections:

```cpp
agent.prompt_add_section("Role", "You are a technical support agent.");
agent.prompt_add_section("Instructions", "",
    {"Always verify the customer's account first",
     "Escalate to a human if the issue is unresolved after 3 attempts"});
agent.prompt_add_subsection("Instructions", "Tone", "Be empathetic and patient.");
```

### SWAIG Tools

Define functions the AI can call during conversations:

```cpp
agent.define_tool("lookup_order", "Look up an order by ID",
    {{"type", "object"},
     {"properties", {{"order_id", {{"type", "string"}, {"description", "The order ID"}}}}}},
    [](const json& args, const json& raw) -> swaig::FunctionResult {
        std::string id = args.value("order_id", "");
        // ... look up order ...
        return swaig::FunctionResult("Order " + id + " shipped on March 1.");
    });
```

### DataMap Tools (Server-Side)

Create API-calling tools that run on SignalWire servers -- no webhook needed:

```cpp
auto weather = datamap::DataMap("get_weather")
    .purpose("Get current weather for a city")
    .parameter("city", "string", "City name", true)
    .webhook("GET", "https://api.weather.com/v1?q=${args.city}&key=KEY")
    .output(swaig::FunctionResult("Weather: ${response.current.condition}"));
agent.register_swaig_function(weather.to_swaig_function());
```

### Contexts & Steps

Build multi-phase conversation workflows:

```cpp
auto& ctx = agent.define_contexts().add_context("sales");
ctx.add_step("greet")
    .add_section("Task", "Welcome the customer and ask how you can help.")
    .set_step_criteria("Customer has stated their need")
    .set_valid_steps({"recommend"});
ctx.add_step("recommend")
    .add_section("Task", "Recommend a product based on the customer's needs.")
    .set_step_criteria("Customer has received a recommendation");
```

### Skills

One-liner capability injection:

```cpp
agent.add_skill("datetime");
agent.add_skill("math");
agent.add_skill("web_search", {{"api_key", "..."}, {"search_engine_id", "..."}});
```

### Prefabs

Pre-built agent types for common patterns:

```cpp
prefabs::InfoGathererAgent gatherer;
gatherer.set_questions({
    {{"key", "name"}, {"question", "What is your name?"}},
    {{"key", "email"}, {"question", "What is your email?"}},
});
gatherer.run();
```

### REST Client

```cpp
auto client = rest::SignalWireClient::from_env();
auto agents = client.fabric().agents.list();
auto result = client.calling().dial({{"to", "+15551234567"}, {"from", "+15559876543"}});
```

## Environment Variables

| Variable | Description | Default |
|----------|-------------|---------|
| `SWML_BASIC_AUTH_USER` | HTTP basic auth username | auto-generated |
| `SWML_BASIC_AUTH_PASSWORD` | HTTP basic auth password | auto-generated |
| `SWML_PROXY_URL_BASE` | Public URL base for webhooks | auto-detected |
| `SWML_SSL_ENABLED` | Enable HTTPS | `false` |
| `SIGNALWIRE_PROJECT_ID` | Project ID for REST/RELAY | -- |
| `SIGNALWIRE_API_TOKEN` | API token for REST/RELAY | -- |
| `SIGNALWIRE_SPACE` | Space hostname for REST | -- |
| `SIGNALWIRE_LOG_LEVEL` | Log level (debug/info/warn/error) | `info` |
| `SIGNALWIRE_LOG_MODE` | Set to `off` to suppress all logging | -- |

## CLI Tool

```bash
# Test SWAIG functions against a running agent
bin/swaig-test http://localhost:3000/my-agent --list-tools
bin/swaig-test http://localhost:3000/my-agent --exec get_time
bin/swaig-test http://localhost:3000/my-agent --exec get_weather --param location=NYC
```

## Documentation

- [Architecture](docs/architecture.md) -- system design and component overview
- [Agent Guide](docs/agent_guide.md) -- building agents step by step
- [SWAIG Reference](docs/swaig_reference.md) -- function definitions, results, actions
- [DataMap Guide](docs/datamap_guide.md) -- server-side API tools
- [Contexts Guide](docs/contexts_guide.md) -- multi-phase workflows
- [Skills System](docs/skills_system.md) -- built-in and custom skills
- [SWML Service Guide](docs/swml_service_guide.md) -- low-level SWML document builder
- [Security](docs/security.md) -- authentication and session management
- [LLM Parameters](docs/llm_parameters.md) -- tuning AI behavior
- [REST Client](rest/README.md) -- REST API client
- [RELAY Client](relay/README.md) -- real-time call control

## License

MIT License. Copyright (c) 2025 SignalWire.
