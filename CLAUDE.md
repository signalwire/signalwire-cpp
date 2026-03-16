# CLAUDE.md

This file provides guidance to Claude Code when working with this C++ codebase.

## Project Overview

C++17 port of the SignalWire AI Agents SDK. Provides AgentBase, SWML document
building, SWAIG function results, DataMap tools, Contexts/Steps, Skills, Prefabs,
REST client, and RELAY client stubs as a single static library.

## Build Commands

```bash
# Full build
cd build && cmake .. && make -j$(nproc)

# Run all 258 tests
cd build && ./run_tests

# Build from scratch
mkdir -p build && cd build && cmake .. && make -j$(nproc) && ./run_tests

# Compile a standalone example (outside CMake)
g++ -std=c++17 -I include -I deps examples/simple_agent.cpp -L build -lsignalwire_agents -lssl -lcrypto -lpthread -o simple_agent
```

## Architecture

### Directory Layout

```
include/signalwire/           Public headers
    agent/agent_base.hpp      Core agent class (~400 lines)
    server/agent_server.hpp   Multi-agent hosting
    swml/                     Document, Service, Schema
    swaig/                    FunctionResult, ToolDefinition
    datamap/datamap.hpp       Server-side API tools
    contexts/contexts.hpp     ContextBuilder, Context, Step, GatherInfo
    skills/                   SkillBase, SkillManager, SkillRegistry
    prefabs/prefabs.hpp       Pre-built agent types
    rest/                     SignalWireClient, HttpClient, CrudResource
    security/                 SessionManager (HMAC-SHA256)
    relay/                    RELAY client stubs (constants, event, call, client)
    common.hpp                Utilities (UUID, base64, url_encode, env)
    logging.hpp               Thread-safe logger
    signalwire_agents.hpp     Umbrella include
    signalwire_agents_c.h     C wrapper (extern "C")

src/                          Implementation (.cpp)
tests/                        All test files compiled into one binary via test_main.cpp
deps/                         Vendored: nlohmann/json.hpp, httplib.h
```

### Key Namespaces

- `signalwire::agent` -- AgentBase, PomSection, DynamicConfigCallback
- `signalwire::swml` -- Document, Section, Verb, Service, Schema
- `signalwire::swaig` -- FunctionResult, ToolDefinition, ToolHandler
- `signalwire::datamap` -- DataMap fluent builder
- `signalwire::contexts` -- ContextBuilder, Context, Step, GatherInfo, GatherQuestion
- `signalwire::skills` -- SkillBase, SkillManager, SkillRegistry
- `signalwire::prefabs` -- InfoGathererAgent, SurveyAgent, etc.
- `signalwire::rest` -- SignalWireClient, HttpClient, CrudResource
- `signalwire::server` -- AgentServer
- `signalwire::security` -- SessionManager

### Dependencies

- **nlohmann/json** -- vendored in `deps/json.hpp`, included as `<nlohmann/json.hpp>`
- **cpp-httplib** -- vendored in `deps/httplib.h`, header-only HTTP server/client
- **OpenSSL** -- linked for HMAC-SHA256, random bytes, crypto
- **pthreads** -- for `std::thread`, `std::mutex`, etc.

### Test Framework

Minimal custom framework in `tests/test_main.cpp`. Macros:
- `TEST(name)` -- define a test
- `ASSERT_TRUE(x)`, `ASSERT_FALSE(x)`, `ASSERT_EQ(a,b)`, `ASSERT_NE(a,b)`
- `ASSERT_THROWS(expr)` -- expect exception

All test files are `#include`d into `test_main.cpp` and compiled as one translation unit.

## Common Patterns

### Agent Creation
```cpp
class MyAgent : public agent::AgentBase {
public:
    MyAgent() : AgentBase("name", "/route") {
        prompt_add_section("Role", "...");
        define_tool("tool_name", "description", params_json, handler);
        add_skill("datetime");
    }
};
```

### Tool Handlers
```cpp
swaig::ToolHandler handler = [](const json& args, const json& raw) {
    return swaig::FunctionResult("Response text")
        .update_global_data({{"key", "value"}})
        .say("Speaking this aloud");
};
```

### DataMap (server-side tools)
```cpp
auto dm = datamap::DataMap("tool")
    .purpose("...")
    .parameter("p", "string", "desc", true)
    .webhook("GET", "https://api.example.com/${args.p}")
    .output(swaig::FunctionResult("Result: ${response.value}"));
agent.register_swaig_function(dm.to_swaig_function());
```

### Contexts
```cpp
auto& ctx = agent.define_contexts().add_context("default");
ctx.add_step("step1")
    .add_section("Task", "Do something")
    .set_step_criteria("Done condition")
    .set_valid_steps({"step2"});
```

## Important Notes

- Library is built as a static archive: `libsignalwire_agents.a`
- No package manager required; all deps vendored
- CPPHTTPLIB_OPENSSL_SUPPORT is disabled (requires OpenSSL 3.0+)
- SSL for httplib handled externally; crypto primitives use OpenSSL directly
- RELAY client headers are stubs; WebSocket transport not yet implemented
- C wrapper (`signalwire_agents_c.h`) provides FFI for other languages
- Examples are standalone `.cpp` files meant to illustrate usage, not built by CMake
- Logging controlled by `SIGNALWIRE_LOG_LEVEL` env var and `Logger::instance()`
