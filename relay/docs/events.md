# Events

RELAY events are server-pushed notifications about call state changes and
operation results. Events arrive over the WebSocket as `signalwire.event`
JSON-RPC messages and are automatically routed to the correct `Call` object and
to any registered handlers.

## Listening for Events

<!-- snippet-setup -->
```cpp
#include <signalwire/relay/client.hpp>
#include <signalwire/relay/call.hpp>
#include <signalwire/relay/relay_event.hpp>
#include <nlohmann/json.hpp>
#include <iostream>
using json = nlohmann::json;
signalwire::relay::RelayClient client("project-id", "api-token", "example.signalwire.com", {"default"});
signalwire::relay::Call call;
```

### On a Call

Register an observer with `on_event`. The handler receives a `CallEvent`:

```cpp
client.on_call([](signalwire::relay::Call& call) {
    call.on_event([](const signalwire::relay::CallEvent& ev) {
        std::cout << "Event: " << ev.event_type << " state=" << ev.call_state << "\n";
    });
});
```

To block until the call reaches a lifecycle state, use the typed waits (each
returns `bool` — `true` on reaching the state, `false` on timeout):

```cpp
call.wait_for_answered(30000);
call.wait_for_ended();
```

### On the Client

A generic observer fires for every dispatched event after typed routing:

```cpp
client.on_event([](const signalwire::relay::RelayEvent& ev) {
    std::cout << "raw event: " << ev.event_type << "\n";
});
```

### Via Actions

Actions returned by `play()`, `record()`, etc. expose `wait()` (blocks until the
operation completes) and `result()` (the terminal payload):

```cpp
auto action = call.play({{{"type", "tts"}, {"params", {{"text", "Hello"}}}}});
action.wait(30000);
std::cout << action.result().dump() << "\n";
```

## Event Type Constants

Declared in `signalwire/relay/constants.hpp`:

| Constant | Value | Description |
|----------|-------|-------------|
| `EVENT_CALL_RECEIVED` | `calling.call.received` | Inbound call notification |
| `EVENT_CALL_STATE` | `calling.call.state` | Call state changes (created, ringing, answered, ending, ended) |
| `EVENT_CALL_PLAY` | `calling.call.play` | Play operation state changes |
| `EVENT_CALL_RECORD` | `calling.call.record` | Record operation state changes |
| `EVENT_CALL_COLLECT` | `calling.call.collect` | Input collection results |
| `EVENT_CALL_TAP` | `calling.call.tap` | Tap operation state changes |
| `EVENT_CALL_DETECT` | `calling.call.detect` | Detection results |
| `EVENT_CALL_CONNECT` | `calling.call.connect` | Bridge/connect state changes |
| `EVENT_CALL_FAX` | `calling.call.fax` | Fax operation state changes |
| `EVENT_CALL_SEND_DIGITS` | `calling.call.send_digits` | DTMF send completion |
| `EVENT_MESSAGING_RECEIVE` | `messaging.receive` | Inbound message received |
| `EVENT_MESSAGING_STATE` | `messaging.state` | Outbound message state change |

## Event Structs

Events parse from the JSON-RPC params via static `from_relay_event` /
`from_json` factories. The base `RelayEvent` carries `event_type`, `params`
(the inner JSON), `event_channel`, and `timestamp`. The typed structs add named
fields:

| Struct | Key Fields |
|--------|-----------|
| `RelayEvent` | `event_type`, `params`, `event_channel`, `timestamp` |
| `CallEvent` | `call_id`, `node_id`, `call_state`, `peer_call_id`, `tag` |
| `ComponentEvent` | `call_id`, `control_id`, `state` (play / record / collect / etc.) |
| `MessageEvent` | `message_id`, `message_state`, `from`, `to`, `body` |
| `DialEvent` | `tag`, `dial_state`, `call_info` |

```cpp
#include <signalwire/relay/relay_event.hpp>
#include <iostream>

void on_event(const signalwire::relay::RelayEvent& ev) {
    if (ev.event_type == "calling.call.state") {
        auto ce = signalwire::relay::CallEvent::from_relay_event(ev);
        std::cout << "call_state: " << ce.call_state << "\n";
    }
}
```

## Typed State Enums

Alongside the bare-string fields, `signalwire/relay/states.hpp` provides typed
enums and `*_from_string` parsers that return `std::optional` (so an unknown
server value never throws):

- `CallState` — `Created`, `Ringing`, `Answered`, `Ending`, `Ended`
  (`Call::call_state()` returns `std::optional<CallState>`).
- `DialState` — `Dialing`, `Answered`, `Failed` (`DialEvent::dial_state_enum()`).
- `MessageState` — `Queued`, `Initiated`, `Sent`, `Delivered`, `Undelivered`,
  `Failed`, `Received` (`Message::message_state()`).

Each has a free `to_string(...)`, a `*_value(...)`, and an `is_terminal(...)`
overload.

## Call States

```
created -> ringing -> answered -> ending -> ended
```

Constants: `CALL_STATE_CREATED`, `CALL_STATE_RINGING`, `CALL_STATE_ANSWERED`,
`CALL_STATE_ENDING`, `CALL_STATE_ENDED`.

## Message States

Outbound messages progress through: `queued` -> `initiated` -> `sent` ->
`delivered` (or `undelivered` / `failed`). Inbound messages arrive with state
`received`.

Constants: `MESSAGE_STATE_QUEUED`, `MESSAGE_STATE_INITIATED`,
`MESSAGE_STATE_SENT`, `MESSAGE_STATE_DELIVERED`, `MESSAGE_STATE_UNDELIVERED`,
`MESSAGE_STATE_FAILED`, `MESSAGE_STATE_RECEIVED`.
