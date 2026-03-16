# SignalWire RELAY Client (C++)

Real-time call control and messaging over WebSocket. The RELAY client connects
to SignalWire via the Blade protocol (JSON-RPC 2.0 over WebSocket) and gives
you imperative control over live phone calls and SMS/MMS messaging.

> **Status:** Header stubs are provided so downstream code can compile and link.
> The WebSocket transport layer is not yet implemented -- all I/O methods are
> no-ops that return immediately.

## Quick Start

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

## Headers

| Header | Contents |
|--------|----------|
| `signalwire/relay/client.hpp` | `RelayClient` -- connection, auth, event dispatch |
| `signalwire/relay/call.hpp` | `Call` -- all calling methods and `Action` objects |
| `signalwire/relay/message.hpp` | `Message` -- SMS/MMS message tracking |
| `signalwire/relay/relay_event.hpp` | Typed event structs (CallEvent, ComponentEvent, MessageEvent) |
| `signalwire/relay/action.hpp` | `Action` -- controllable in-progress operations |
| `signalwire/relay/constants.hpp` | Protocol constants, call states, event types |

## Features (API surface defined, transport pending)

- All calling methods: answer, hangup, play, record, collect, connect, detect, tap, etc.
- SMS/MMS messaging: send outbound, receive inbound, track delivery state
- Action objects with `wait()`, `stop()`, `pause()`, `resume()`
- Typed event classes for all call events
- Environment variable configuration
- Dynamic context subscription/unsubscription

## Environment Variables

| Variable | Description |
|----------|-------------|
| `SIGNALWIRE_PROJECT_ID` | Project ID for authentication |
| `SIGNALWIRE_API_TOKEN` | API token for authentication |
| `SIGNALWIRE_SPACE` | Space hostname (default: `relay.signalwire.com`) |

## Documentation

- [Getting Started](docs/getting-started.md)
- [Call Methods](docs/call-methods.md)
- [Events](docs/events.md)
- [Messaging](docs/messaging.md)
- [Client Reference](docs/client-reference.md)

## Examples

- [relay_answer_and_welcome.cpp](examples/relay_answer_and_welcome.cpp) -- answer an inbound call and play TTS
- [relay_dial_and_play.cpp](examples/relay_dial_and_play.cpp) -- dial outbound and play a greeting
- [relay_ivr_connect.cpp](examples/relay_ivr_connect.cpp) -- IVR menu with DTMF collection
