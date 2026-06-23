# RelayClient Reference

## Construction

`RelayClient` is constructed from explicit parameters, from a `RelayConfig`
struct, or from the environment:

```cpp
// From individual parameters (project, token, host, contexts)
RelayClient client("project-id", "api-token", "example.signalwire.com", {"default"});

// From a config struct
RelayConfig config;
config.project = "project-id";
config.token = "api-token";
config.host = "example.signalwire.com";
config.contexts = {"default"};
config.max_active_calls = 1000;
config.max_connections = 1;
RelayClient client2(config);

// From SIGNALWIRE_PROJECT_ID / SIGNALWIRE_API_TOKEN / SIGNALWIRE_SPACE
auto client3 = RelayClient::from_env();
```

`RelayConfig` fields: `project`, `token`, `host` (default `relay.signalwire.com`),
`port` (default 443), `contexts` (default `{"default"}`), `max_active_calls`
(default 1000), `max_connections` (default 1). Authentication uses
`project` + `token`.

## Methods

### `void run()`

Blocking entry point. Connects, authenticates, and runs the event loop with
auto-reconnect until interrupted.

```cpp
client.run();
```

### `bool connect()` / `void disconnect()`

Manual lifecycle control. `connect()` returns `true` on success;
`is_connected()` reports the current state.

```cpp
client.connect();
// ... use client ...
client.disconnect();
```

### `void on_call(InboundCallHandler handler)`

Register the inbound call handler.
`InboundCallHandler = std::function<void(Call&)>`.

```cpp
client.on_call([](Call& call) {
    call.answer();
});
```

### `Call dial(const json& devices, const std::string& tag = "", int dial_timeout_ms = 120000, int max_duration = 0)`

Place an outbound call. Returns a `Call` once the server emits the terminal
dial event for the tag, or an empty `Call` on timeout/failure (no exception is
thrown on timeout).

- `devices` -- nested array of device objects (serial/parallel dial)
- `tag` -- optional correlation tag (a UUID is generated if blank)
- `dial_timeout_ms` -- milliseconds to wait for the terminal dial event (default 120000)
- `max_duration` -- max call duration in seconds (omitted when 0)

```cpp
Call call = client.dial({{
    {{"type", "phone"}, {"params", {{"to_number", "+15551234567"}, {"from_number", "+15559876543"}}}}
}});
```

### `void on_message(InboundMessageHandler handler)`

Register the inbound message handler.
`InboundMessageHandler = std::function<void(const Message&)>`.

```cpp
client.on_message([](const Message& message) {
    std::cout << "SMS from " << message.from << ": " << message.body << "\n";
});
```

### `Message send_message(const std::string& from, const std::string& to, const std::string& body, const std::vector<std::string>& media = {}, const std::vector<std::string>& tags = {}, const std::string& region = "", const std::string& context = "")`

Send an outbound SMS/MMS. Note the argument order is **(from, to, body, ...)**.
Returns a `Message` that tracks delivery state.

```cpp
auto message = client.send_message("+15551111111", "+15552222222", "Hello!");
message.wait();  // block until delivered/failed (returns bool)
```

See [Messaging](messaging.md) for full details.

### `void on_event(EventHandler handler)`

Register a generic observer fired for every dispatched `signalwire.event` after
typed routing. `EventHandler = std::function<void(const RelayEvent&)>`. The
most-recent registration wins.

### `json execute(const std::string& method, const json& params)`

Send a raw JSON-RPC request. Used internally by Call/Action methods, but
available for custom commands. `send_raw_request(method, params)` is also public
for test harnesses.

### `void subscribe(const std::vector<std::string>& contexts)` / `void unsubscribe(...)`

Dynamically subscribe to or unsubscribe from contexts after connecting.

```cpp
client.subscribe({"new-context"});
client.unsubscribe({"old-context"});
```

## Accessors

| Accessor | Type | Description |
|----------|------|-------------|
| `config()` | `const RelayConfig&` | The client's configuration (project, host, contexts, ...) |
| `relay_protocol()` | `const std::string&` | Server-assigned protocol string from the connect response |
| `is_connected()` | `bool` | Current connection state |
| `session_id()` | `const std::string&` | Server-assigned session id (test/audit use; empty until connected) |

## Connection Behavior

- **Auto-reconnect**: On connection loss, the client reconnects with exponential
  backoff (base 1s, max 30s, factor 2.0, up to 50 attempts).
- **Authorization state**: The server can send encrypted auth state for fast
  re-authentication on reconnect.
- **Server disconnect**: The server can request a graceful disconnect; the
  client auto-reconnects afterward.
- Pending requests are rejected on disconnect.

## Concurrency

The client dispatches inbound calls on a background WebSocket thread.
`max_active_calls` (default 1000) caps concurrent calls; `max_connections`
(default 1) bounds WebSocket connections per process.

## Error Handling

Call/Action methods do not throw per-call. Operation outcomes surface through
the returned `Action`'s `state()` / `result()` and the `wait()` return value
(`true` = completed, `false` = timed out). Calls that have ended (404/410) cause
pending actions to resolve rather than raise.
