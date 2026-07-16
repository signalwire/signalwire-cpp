# SignalWire REST Client (C++)

Synchronous REST client for managing SignalWire resources, controlling live
calls, and interacting with every SignalWire API surface from C++. No WebSocket
required -- just standard HTTP requests.

## Quick Start

```cpp
#include <signalwire/rest/rest_client.hpp>

using namespace signalwire::rest;

int main() {
    auto client = RestClient::from_env();

    // List AI agents
    auto agents = client.fabric().ai_agents.list();

    // Search for a phone number
    auto results = client.phone_numbers().search({{"area_code", "512"}});

    // Place a call
    auto call = client.calling().dial({
        .from = "+15559876543",
        .to = "+15551234567",
        .url = "https://example.com/handler",
    });
}
```

## Features

- Single `RestClient` with namespaced sub-objects for every API
- 22 API namespaces: Fabric, Calling, Phone Numbers, Datasphere, Video, Messages, Projects, and more
- Generic CRUD resources with `list()`, `create()`, `get()`, `update()`, `delete_()`
- Specialized methods per namespace (dial, search, lookup, publish, etc.)
- Environment variable configuration (`RestClient::from_env()`)
- JSON dict returns via nlohmann/json -- no wrapper objects to learn

## Namespaces

| Namespace | Description |
|-----------|-------------|
| `fabric()` | AI agents, SWML scripts, subscribers, call flows, SIP endpoints, etc. |
| `calling()` | Dial, play, record, collect, detect, tap, connect, transfer, etc. |
| `phone_numbers()` | Search, buy, release, manage numbers |
| `datasphere()` | Document management and semantic search |
| `video()` | Rooms, sessions, recordings |
| `addresses()` | Address management |
| `queues()` | Queue management |
| `recordings()` | Recording management |
| `mfa()` | Multi-factor authentication |
| `pubsub()` | PubSub publishing |
| `chat()` | Chat messaging |
| `lookup()` | Phone number lookup |
| `project()` | Project settings |
| `logs()` | API logs |
| And more... | number_groups, verified_callers, sip_profile, short_codes, imported_numbers, registry |

## Environment Variables

| Variable | Description |
|----------|-------------|
| `SIGNALWIRE_PROJECT_ID` | Project ID for authentication |
| `SIGNALWIRE_API_TOKEN` | API token for authentication |
| `SIGNALWIRE_SPACE` | Space hostname (e.g. `example.signalwire.com`) |

## Documentation

- [Getting Started](docs/getting-started.md)
- [Client Reference](docs/client-reference.md)
- [Fabric Resources](docs/fabric.md)
- [Calling Commands](docs/calling.md)
- [All Namespaces](docs/namespaces.md)

## Examples

See `examples/` for 12 standalone examples covering all namespaces.
