# Getting Started with the REST Client

The REST client provides synchronous access to all SignalWire APIs using standard HTTP requests. No WebSocket connection required.

## Installation

The REST client is part of the `signalwire` C++ static library (`libsignalwire.a`),
built from this repository — there is no separate package to install. Add the
`include/` directory to your include path and link the library:

```bash
g++ -std=c++20 -I include -I deps your_app.cpp -L build -lsignalwire -lssl -lcrypto -lpthread -o your_app
```

The REST client uses the vendored cpp-httplib (header-only) plus OpenSSL 3.0+ for
TLS. See the root README for full build instructions.

## Configuration

You need three things to connect:

| Parameter    | Env Var                 | Description |
|--------------|-------------------------|-------------|
| `space`      | `SIGNALWIRE_SPACE`      | Your space hostname (e.g. `example.signalwire.com`) |
| `project_id` | `SIGNALWIRE_PROJECT_ID` | Your SignalWire project ID |
| `token`      | `SIGNALWIRE_API_TOKEN`  | Your SignalWire API token |

## Minimal Example

```cpp
#include <signalwire/rest/rest_client.hpp>
#include <iostream>

using namespace signalwire::rest;

int main() {
    RestClient client("example.signalwire.com", "your-project-id", "your-api-token");

    // List your AI agents
    auto agents = client.fabric().ai_agents.list();
    std::cout << agents.dump(2) << "\n";
}
```

Or build from environment variables with `RestClient::from_env()`:

```bash
export SIGNALWIRE_PROJECT_ID=your-project-id
export SIGNALWIRE_API_TOKEN=your-api-token
export SIGNALWIRE_SPACE=example.signalwire.com
```

<!-- snippet-setup -->
```cpp
#include <signalwire/rest/rest_client.hpp>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
signalwire::rest::RestClient client("example-space", "project-id", "api-token");
```

```cpp
auto env_client = signalwire::rest::RestClient::from_env();
auto agents = env_client.fabric().ai_agents.list();
```

## CRUD Pattern

Most resources follow the same CRUD pattern. Request bodies are `nlohmann::json`
objects; query params are a `std::map<std::string, std::string>`. The delete
method is named `delete_` (because `delete` is a C++ keyword):

```cpp
// List
auto items = client.fabric().ai_agents.list();

// Create
auto agent = client.fabric().ai_agents.create({{"name", "Support"}, {"prompt", {{"text", "Be helpful"}}}});

// Get by ID
agent = client.fabric().ai_agents.get("agent-uuid");

// Update
client.fabric().ai_agents.update("agent-uuid", {{"name", "Updated Name"}});

// Delete
client.fabric().ai_agents.delete_("agent-uuid");
```

Fabric resources also support listing addresses:

```cpp
auto addresses = client.fabric().ai_agents.list_addresses("agent-uuid");
```

## Error Handling

Non-2xx responses throw `SignalWireRestError`, which carries the HTTP status
(`status()`), the response body (`body()`), and the message (`what()`):

```cpp
#include <signalwire/rest/rest_client.hpp>
#include <iostream>

using namespace signalwire::rest;

int main() {
    auto client = RestClient::from_env();
    try {
        auto agent = client.fabric().ai_agents.get("nonexistent-id");
    } catch (const SignalWireRestError& e) {
        std::cerr << "HTTP " << e.status() << ": " << e.what() << "\n";
        std::cerr << "Body: " << e.body() << "\n";
        // HTTP 404: ...
    }
}
```

## Debug Logging

Set the log level to see HTTP request details:

```bash
export SIGNALWIRE_LOG_LEVEL=debug
```

## Next Steps

- [Client Reference](client-reference.md) -- all namespaces and constructor options
- [Fabric Resources](fabric.md) -- managing AI agents, SWML scripts, and more
- [Calling Commands](calling.md) -- REST-based call control
- [All Namespaces](namespaces.md) -- phone numbers, video, datasphere, and more
