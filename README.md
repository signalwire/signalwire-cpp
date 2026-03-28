<!-- Header -->
<div align="center">
    <a href="https://signalwire.com" target="_blank">
        <img src="https://github.com/user-attachments/assets/0c8ed3b9-8c50-4dc6-9cc4-cc6cd137fd50" width="500" />
    </a>

# SignalWire SDK for C++

_Build AI voice agents, control live calls over WebSocket, and manage every SignalWire resource over REST -- all from modern C++17._

<p align="center">
  <a href="https://developer.signalwire.com/sdks/agents-sdk" target="_blank">Documentation</a> &middot;
  <a href="https://github.com/signalwire/signalwire-docs/issues/new/choose" target="_blank">Report an Issue</a> &middot;
  <a href="https://github.com/signalwire/signalwire-cpp" target="_blank">GitHub</a>
</p>

<a href="https://discord.com/invite/F2WNYTNjuF" target="_blank"><img src="https://img.shields.io/badge/Discord%20Community-5865F2" alt="Discord" /></a>
<a href="LICENSE"><img src="https://img.shields.io/badge/MIT-License-blue" alt="MIT License" /></a>
<a href="https://github.com/signalwire/signalwire-cpp" target="_blank"><img src="https://img.shields.io/github/stars/signalwire/signalwire-cpp" alt="GitHub Stars" /></a>

</div>

---

## What's in this SDK

| Capability | What it does | Quick link |
|-----------|-------------|------------|
| **AI Agents** | Build voice agents that handle calls autonomously -- the platform runs the AI pipeline, your code defines the persona, tools, and call flow | [Agent Guide](#ai-agents) |
| **RELAY Client** | Control live calls and SMS/MMS in real time over WebSocket -- answer, play, record, collect DTMF, conference, transfer, and more | [RELAY docs](relay/README.md) |
| **REST Client** | Manage SignalWire resources over HTTP -- phone numbers, SIP endpoints, Fabric AI agents, video rooms, messaging, and 21 API namespaces | [REST docs](rest/README.md) |

```bash
# Requirements: C++17 compiler, CMake 3.16+, OpenSSL
git clone https://github.com/signalwire/signalwire-cpp.git
cd signalwire-cpp && mkdir build && cd build
cmake .. && make -j$(nproc)
```

---

## AI Agents

Each agent is a self-contained microservice that generates [SWML](docs/swml_service_guide.md) (SignalWire Markup Language) and handles [SWAIG](docs/swaig_reference.md) (SignalWire AI Gateway) tool calls. The SignalWire platform runs the entire AI pipeline (STT, LLM, TTS) -- your agent just defines the behavior.

```cpp
#include <signalwire/agent/agent_base.hpp>
#include <ctime>

using namespace signalwire;
using json = nlohmann::json;

class MyAgent : public agent::AgentBase {
public:
    MyAgent() : AgentBase("my-agent", "/agent") {
        add_language({"English", "en-US", "inworld.Mark"});
        prompt_add_section("Role", "You are a helpful assistant.");

        define_tool("get_time", "Get the current time",
            {{"type", "object"}, {"properties", json::object()}},
            [](const json& /*args*/, const json& /*raw*/) -> swaig::FunctionResult {
                auto now = std::time(nullptr);
                char buf[32];
                std::strftime(buf, sizeof(buf), "%H:%M:%S", std::localtime(&now));
                return swaig::FunctionResult(std::string("The time is ") + buf);
            });
    }
};

int main() {
    MyAgent agent;
    agent.run();  // Serves on http://0.0.0.0:3000/agent
}
```

Test locally without running a server:

```bash
bin/swaig-test http://localhost:3000/agent --list-tools
bin/swaig-test http://localhost:3000/agent --dump-swml
bin/swaig-test http://localhost:3000/agent --exec get_time
```

### Agent Features

- **Prompt Object Model (POM)** -- structured prompt composition via `prompt_add_section()`
- **SWAIG tools** -- define functions with `define_tool()` that the AI calls mid-conversation, with native access to the call's media stack
- **Skills system** -- add capabilities with one-liners: `agent.add_skill("datetime")`
- **Contexts and steps** -- structured multi-step workflows with navigation control
- **DataMap tools** -- tools that execute on SignalWire's servers, calling REST APIs without your own webhook
- **Dynamic configuration** -- per-request agent customization for multi-tenant deployments
- **Call flow control** -- pre-answer, post-answer, and post-AI verb insertion
- **Prefab agents** -- ready-to-use archetypes (InfoGatherer, Survey, FAQ, Receptionist, Concierge)
- **Multi-agent hosting** -- serve multiple agents on a single server with `AgentServer`
- **SIP routing** -- route SIP calls to agents based on usernames
- **Session state** -- persistent conversation state with global data and post-prompt summaries
- **Security** -- auto-generated basic auth, function-specific HMAC tokens, SSL support
- **C API** -- `extern "C"` wrapper for FFI from C, Python, Ruby, Lua, etc.

### Agent Examples

The [`examples/`](examples/) directory contains 37+ working examples:

| Example | What it demonstrates |
|---------|---------------------|
| [simple_agent.cpp](examples/simple_agent.cpp) | POM prompts, SWAIG tools, hints, languages, SIP routing |
| [contexts_demo.cpp](examples/contexts_demo.cpp) | Multi-persona workflow with context switching and step navigation |
| [datamap_demo.cpp](examples/datamap_demo.cpp) | Server-side API tools without webhooks |
| [skills_demo.cpp](examples/skills_demo.cpp) | Loading built-in skills (datetime, math, web_search) |
| [call_flow.cpp](examples/call_flow.cpp) | 5-phase verb pipeline: pre-answer, answer, post-answer, post-AI |
| [session_state.cpp](examples/session_state.cpp) | Global data, session tokens, callbacks |
| [multi_agent_server.cpp](examples/multi_agent_server.cpp) | Multiple agents on one server |
| [comprehensive_dynamic.cpp](examples/comprehensive_dynamic.cpp) | Per-request dynamic configuration, multi-tenant routing |

See [examples/README.md](examples/README.md) for the full list organized by category.

---

## RELAY Client

Real-time call control and messaging over WebSocket. The RELAY client connects to SignalWire via the Blade protocol and gives you imperative control over live phone calls and SMS/MMS.

```cpp
#include <signalwire/relay/client.hpp>

using namespace signalwire::relay;

int main() {
    auto client = RelayClient::from_env();

    client.on_call([](Call& call) {
        call.answer();
        auto action = call.play({
            {{"type", "tts"}, {"params", {{"text", "Welcome to SignalWire!"}}}}
        });
        action.wait();
        call.hangup();
    });

    client.run();
}
```

- 57+ calling methods (play, record, collect, detect, tap, stream, AI, conferencing, and more)
- SMS/MMS messaging with delivery tracking
- Action objects with `wait()`, `stop()`, `pause()`, `resume()`
- Auto-reconnect with exponential backoff

See the **[RELAY documentation](relay/README.md)** for the full guide, API reference, and examples.

---

## REST Client

Synchronous REST client for managing SignalWire resources and controlling calls over HTTP. No WebSocket required.

```cpp
#include <signalwire/rest/rest_client.hpp>

using namespace signalwire::rest;
using json = nlohmann::json;

int main() {
    auto client = RestClient::from_env();

    auto agents = client.fabric().agents.list();
    auto call   = client.calling().dial({
        {"to", "+15551234567"}, {"from", "+15559876543"},
        {"url", "https://example.com/handler"}
    });
    auto numbers = client.phone_numbers().search({{"area_code", "512"}});
    auto results = client.datasphere().documents.search({{"query_string", "billing policy"}});
}
```

- 21 namespaced API surfaces: Fabric (13 resource types), Calling (37 commands), Video, Datasphere, Compat (Twilio-compatible), Phone Numbers, SIP, Queues, Recordings, and more
- Generic CRUD resources with `list()`, `create()`, `get()`, `update()`, `del()`
- JSON dict returns via nlohmann/json -- no wrapper objects

See the **[REST documentation](rest/README.md)** for the full guide, API reference, and examples.

---

## Installation

### Prerequisites

- C++17 compiler (GCC 8+, Clang 7+, MSVC 2019+)
- CMake 3.16+
- OpenSSL development libraries
- pthreads

### Build from Source

```bash
git clone https://github.com/signalwire/signalwire-cpp.git
cd signalwire-cpp
mkdir build && cd build
cmake ..
make -j$(nproc)
```

### Link to Your Project

Add to your `CMakeLists.txt`:

```cmake
add_subdirectory(signalwire-cpp)
add_executable(my_agent my_agent.cpp)
target_link_libraries(my_agent signalwire)
```

Or compile directly:

```bash
g++ -std=c++17 -I include -I deps my_agent.cpp \
    -L build -lsignalwire -lssl -lcrypto -lpthread -o my_agent
```

## Documentation

Full reference documentation is available at **[developer.signalwire.com/sdks/agents-sdk](https://developer.signalwire.com/sdks/agents-sdk)**.

Guides are also available in the [`docs/`](docs/) directory:

### Getting Started

- [Agent Guide](docs/agent_guide.md) -- creating agents, prompt configuration, dynamic setup
- [Architecture](docs/architecture.md) -- SDK architecture and core concepts
- [SDK Features](docs/sdk_features.md) -- feature overview, SDK vs raw SWML comparison

### Core Features

- [SWAIG Reference](docs/swaig_reference.md) -- function results, actions, post_data lifecycle
- [Contexts and Steps](docs/contexts_guide.md) -- structured workflows, navigation, gather mode
- [DataMap Guide](docs/datamap_guide.md) -- serverless API tools without webhooks
- [LLM Parameters](docs/llm_parameters.md) -- temperature, top_p, barge confidence tuning
- [SWML Service Guide](docs/swml_service_guide.md) -- low-level construction of SWML documents

### Skills and Extensions

- [Skills System](docs/skills_system.md) -- built-in skills and the modular framework
- [Third-Party Skills](docs/third_party_skills.md) -- creating and publishing custom skills
- [MCP Gateway](docs/mcp_gateway_reference.md) -- Model Context Protocol integration

### Deployment

- [CLI Guide](docs/cli_guide.md) -- `swaig-test` command reference
- [Cloud Functions](docs/cloud_functions_guide.md) -- containerized deployment
- [Configuration](docs/configuration.md) -- environment variables, SSL, proxy setup
- [Security](docs/security.md) -- authentication and security model

### Reference

- [API Reference](docs/api_reference.md) -- complete class and method reference
- [Web Service](docs/web_service.md) -- HTTP server and endpoint details
- [REST Client](rest/README.md) -- REST API client
- [RELAY Client](relay/README.md) -- real-time call control

## Environment Variables

| Variable | Used by | Description |
|----------|---------|-------------|
| `SIGNALWIRE_PROJECT_ID` | RELAY, REST | Project identifier |
| `SIGNALWIRE_API_TOKEN` | RELAY, REST | API token |
| `SIGNALWIRE_SPACE` | RELAY, REST | Space hostname (e.g. `example.signalwire.com`) |
| `SWML_BASIC_AUTH_USER` | Agents | Basic auth username (default: auto-generated) |
| `SWML_BASIC_AUTH_PASSWORD` | Agents | Basic auth password (default: auto-generated) |
| `SWML_PROXY_URL_BASE` | Agents | Base URL when behind a reverse proxy |
| `SWML_SSL_ENABLED` | Agents | Enable HTTPS (`true`, `1`, `yes`) |
| `SWML_SSL_CERT_PATH` | Agents | Path to SSL certificate |
| `SWML_SSL_KEY_PATH` | Agents | Path to SSL private key |
| `SIGNALWIRE_LOG_LEVEL` | All | Logging level (`debug`, `info`, `warn`, `error`) |
| `SIGNALWIRE_LOG_MODE` | All | Set to `off` to suppress all logging |

## Testing

```bash
# Build and run the test suite
mkdir build && cd build
cmake .. && make -j$(nproc)
./run_tests

# Run by category via ctest
ctest -R agent
ctest -R relay
ctest -R rest
```

The test suite contains 258 tests covering all components.

## License

MIT -- see [LICENSE](LICENSE) for details.
