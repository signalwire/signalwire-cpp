# Getting Started with RELAY

The RELAY client connects to SignalWire over a WebSocket and gives you
real-time, imperative control over phone calls. The C++ client
(`signalwire::relay::RelayClient`) is a full JSON-RPC 2.0 implementation built
on a vendored WebSocket transport — call control, dialing, and messaging are all
wired through to the live protocol.

## Installation

The RELAY client is part of the `signalwire` C++ static library
(`libsignalwire.a`), built from this repository. Add `include/` to your include
path and link the library (RELAY pulls in the IXWebSocket transport, configured
via FetchContent, plus OpenSSL 3.0+; on macOS the CoreFoundation/Security
frameworks are also linked). See the root README for the full build recipe.

```cpp
#include <signalwire/relay/client.hpp>
```

## Configuration

You need three things to connect:

| Parameter    | Env Var                 | Description |
|--------------|-------------------------|-------------|
| `project`    | `SIGNALWIRE_PROJECT_ID` | Your SignalWire project ID |
| `token`      | `SIGNALWIRE_API_TOKEN`  | Your SignalWire API token |
| `host`       | `SIGNALWIRE_SPACE`      | Your space hostname (default `relay.signalwire.com`) |

`RelayClient::from_env()` reads exactly these three variables.

## Minimal Example

```cpp
#include <signalwire/relay/client.hpp>
#include <iostream>

using namespace signalwire::relay;

int main() {
    RelayClient client("your-project-id", "your-api-token",
                       "example.signalwire.com", {"default"});

    client.on_call([](Call& call) {
        std::cout << "Inbound call from " << call.from() << "\n";
        call.answer();
        auto action = call.play({{{"type", "tts"}, {"params", {{"text", "Hello!"}}}}});
        action.wait();
        call.hangup();
    });

    client.run();  // connect + block, dispatching inbound calls
}
```

Or use environment variables via `from_env()`:

```bash
export SIGNALWIRE_PROJECT_ID=your-project-id
export SIGNALWIRE_API_TOKEN=your-api-token
export SIGNALWIRE_SPACE=example.signalwire.com
```

```cpp
auto client = RelayClient::from_env();
client.on_call([](Call& call) {
    call.answer();
    call.hangup();
});
client.run();
```

## Contexts

Contexts are topics your client subscribes to for receiving inbound calls. When
a call arrives on a context you're subscribed to, your `on_call` handler is
invoked. Set contexts at construction, or change them after connecting:

```cpp
RelayClient client("project", "token", "example.signalwire.com", {"sales", "support"});

// Or dynamically after connecting
client.subscribe({"billing"});
client.unsubscribe({"sales"});
```

## Making Outbound Calls

Use `client.dial()` to place an outbound call. The `devices` argument is a
nested array: the outer list is serial attempts, each inner list is parallel
attempts.

```cpp
client.connect();

json devices = {{
    {{"type", "phone"}, {"params", {{"to_number", "+15551234567"}, {"from_number", "+15559876543"}}}}
}};
Call call = client.dial(devices);
std::cout << "call_id: " << call.call_id() << "\n";

auto action = call.play({{{"type", "tts"}, {"params", {{"text", "This is an outbound call."}}}}});
action.wait();
call.hangup();
call.wait_for_ended();
client.disconnect();
```

To try two numbers simultaneously, put them in the same inner list:

```cpp
json devices = {{
    {{"type", "phone"}, {"params", {{"to_number", "+15551111111"}, {"from_number", "+15559876543"}}}},
    {{"type", "phone"}, {"params", {{"to_number", "+15552222222"}, {"from_number", "+15559876543"}}}}
}};
Call call = client.dial(devices);
```

`dial(devices, tag, dial_timeout_ms, max_duration)` accepts an optional explicit
dial tag, a dial timeout in milliseconds (default 120000), and a max call
duration in seconds.

## Debug Logging

Set the log level to see WebSocket traffic:

```bash
export SIGNALWIRE_LOG_LEVEL=debug
```

## Next Steps

- [Call Methods Reference](call-methods.md) -- all methods available on a Call object
- [Events](events.md) -- handling real-time call events
- [Client Reference](client-reference.md) -- RelayClient configuration and methods
- [Messaging](messaging.md) -- sending SMS/MMS
