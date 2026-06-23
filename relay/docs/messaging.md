# Messaging

Send and receive SMS/MMS messages through the RELAY client.

## Sending Messages

Use `client.send_message()` to send an outbound SMS or MMS. The signature is:

```cpp
Message send_message(const std::string& from, const std::string& to, const std::string& body,
                     const std::vector<std::string>& media = {},
                     const std::vector<std::string>& tags = {},
                     const std::string& region = "", const std::string& context = "");
```

Note the argument order: **`from` comes before `to`**.

```cpp
auto message = client.send_message("+15551111111", "+15552222222", "Hello from SignalWire!");
```

### Wait for delivery

`message.wait()` blocks until the message reaches a terminal state and returns a
`bool` (`true` if terminal, `false` on timeout):

```cpp
auto message = client.send_message("+15551111111", "+15552222222", "Hello!");
message.wait();  // blocks until delivered/failed
std::cout << "Final state: " << message.state() << "\n";
if (!message.reason().empty()) {
    std::cout << "Reason: " << message.reason() << "\n";
}
```

### Fire and forget

```cpp
auto message = client.send_message("+15551111111", "+15552222222", "Hello!");
// don't call message.wait() — continue immediately
```

### Callback on completion

`Message::on_completed` registers a callback fired when the message reaches a
terminal state (`CompletedCallback = std::function<void(const Message&)>`):

```cpp
auto message = client.send_message("+15551111111", "+15552222222", "Hello!");
message.on_completed([](const Message& m) {
    std::cout << "Delivery: " << m.state() << "\n";
});
```

### MMS (media messages)

```cpp
auto message = client.send_message(
    "+15551111111", "+15552222222", "Check this out!",
    {"https://example.com/image.jpg"});
```

### All parameters

```cpp
auto message = client.send_message(
    "+15551111111",                  // from — E.164
    "+15552222222",                  // to — E.164
    "Message text",                  // body (required if no media)
    {"https://..."},                 // media (required if no body)
    {"vip", "support"},              // tags for searching in the UI
    "us",                            // origination region
    "my_context");                   // context for state events (default: relay protocol)
```

## Receiving Messages

Register a handler with `client.on_message()` to receive inbound SMS/MMS.
`InboundMessageHandler = std::function<void(const Message&)>`.

```cpp
#include <signalwire/relay/client.hpp>

using namespace signalwire::relay;

int main() {
    auto client = RelayClient::from_env();

    client.on_message([&client](const Message& message) {
        std::cout << "From: " << message.from << "\n";
        std::cout << "To: " << message.to << "\n";
        std::cout << "Body: " << message.body << "\n";

        // Reply back (note from/to order)
        client.send_message(message.to, message.from, "You said: " + message.body);
    });

    client.run();
}
```

## Message Object

### Fields and accessors

| Member | Type | Description |
|--------|------|-------------|
| `message_id` | `std::string` | Unique message identifier |
| `direction` | `std::string` | `inbound` or `outbound` |
| `from` | `std::string` | Sender phone number (E.164) |
| `to` | `std::string` | Recipient phone number (E.164) |
| `body` | `std::string` | Text body of the message |
| `media` | `std::vector<std::string>` | Media URLs (MMS) |
| `tags` | `std::vector<std::string>` | Tags attached to the message |
| `region` | `std::string` | Origination region |
| `state()` | `const std::string&` | Current message state |
| `message_state()` | `std::optional<MessageState>` | Typed state enum |
| `reason()` | `const std::string&` | Failure reason (on `undelivered` / `failed`) |
| `is_delivered()` / `is_failed()` / `is_terminal()` | `bool` | State predicates |

### Methods

| Method | Description |
|--------|-------------|
| `wait(int timeout_ms = 0)` | Block until terminal state. Returns `bool` (`true` if terminal, `false` on timeout). |
| `on_completed(cb)` | Register a callback fired when the message reaches a terminal state. |

### Message States

| State | Description |
|-------|-------------|
| `queued` | Message accepted and queued for sending |
| `initiated` | Sending has started |
| `sent` | Message sent to carrier |
| `delivered` | Message delivered to recipient (terminal) |
| `undelivered` | Delivery failed (terminal) — check `reason()` |
| `failed` | Message failed to send (terminal) — check `reason()` |

Inbound messages arrive with state `received`.

## Combining Calls and Messages

The same `RelayClient` handles both calls and messages:

```cpp
auto client = RelayClient::from_env();

client.on_call([](Call& call) {
    call.answer();
    auto action = call.play({{{"type", "tts"}, {"params", {{"text", "Hello!"}}}}});
    action.wait();
    call.hangup();
});

client.on_message([](const Message& message) {
    std::cout << "SMS from " << message.from << ": " << message.body << "\n";
});

client.run();
```
