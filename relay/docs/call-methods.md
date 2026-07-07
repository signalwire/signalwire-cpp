# Call Methods Reference

A `signalwire::relay::Call` represents a live phone call. You get one from the
`on_call` handler (inbound) or from `client.dial(...)` (outbound). Call objects
hold shared internal state, so they can be copied or returned by value.

<!-- snippet-setup -->
```cpp
#include <signalwire/relay/call.hpp>
#include <signalwire/relay/action.hpp>
#include <signalwire/relay/relay_event.hpp>
#include <nlohmann/json.hpp>
#include <iostream>
using json = nlohmann::json;
signalwire::relay::Call call;
```

## Accessors

| Accessor | Type | Description |
|----------|------|-------------|
| `call_id()` | `const std::string&` | Unique call identifier |
| `node_id()` | `const std::string&` | Server node handling the call |
| `state()` | `const std::string&` | Current state: `created`, `ringing`, `answered`, `ending`, `ended` |
| `call_state()` | `std::optional<CallState>` | Typed state enum (`std::nullopt` for an unknown value) |
| `direction()` | `const std::string&` | `inbound` or `outbound` |
| `from()` / `to()` | `const std::string&` | Caller / callee numbers |
| `tag()` | `const std::string&` | Correlation tag |
| `is_answered()` / `is_ended()` | `bool` | State predicates |

## Actions: Blocking vs Fire-and-Forget

Methods like `play()`, `record()`, `detect()`, etc. return an `Action` object by
value. The call itself sends the command and returns immediately — the operation
runs on the server. You choose how to handle completion.

### Wait inline (blocking)

```cpp
auto action = call.play({{{"type", "tts"}, {"params", {{"text", "Hello"}}}}});
action.wait();  // blocks until playback finishes (returns bool: completed vs timed out)
// execution continues only after play is done
```

### Fire and forget (background)

```cpp
auto action = call.play({{{"type", "tts"}, {"params", {{"text", "Hello"}}}}});
// don't call action.wait() — continue immediately while audio plays
call.send_digits("1234");

// check later if needed
if (action.completed()) {
    std::cout << "Play result: " << action.result().dump() << "\n";
}
```

### Fire with callback

```cpp
auto action = call.play({{{"type", "tts"}, {"params", {{"text", "Hello"}}}}});
action.on_completed([](const signalwire::relay::Action& a) {
    std::cout << "Done: " << a.result().dump() << "\n";
});
// continues immediately; callback fires when playback finishes
```

### Action methods summary

| Method | Returns | Description |
|--------|---------|-------------|
| `wait(int timeout_ms = 0)` | `bool` | Blocks until the action completes; `true` if completed, `false` on timeout |
| `completed()` | `bool` | `true` once the action reached a terminal state |
| `state()` | `const std::string&` | Current action state |
| `result()` | `const json&` | Terminal result payload |
| `stop()` | `void` | Stop the operation on the server |
| `pause(extra_params)` / `resume()` | `void` | Pause / resume (play, record) |
| `volume(double amount)` | `void` | Adjust playback volume in dB (play only) |
| `start_input_timers()` | `void` | Start inter-digit / final-digit timers (collect) |
| `on_completed(cb)` | `void` | Register a completion callback |

## Lifecycle

### `Action answer()`

Answer an inbound call.

```cpp
call.answer();
```

### `Action hangup(const std::string& reason = "hangup")`

End the call.

```cpp
call.hangup();
call.hangup("busy");
```

## Audio Playback

### `Action play(const json& media, double volume = 0.0, const std::string& control_id = "")`

Play audio, TTS, silence, or ringtone. The returned `Action` supports `stop()`,
`pause()`, `resume()`, `volume()`, and `wait()`.

```cpp
// TTS
auto action = call.play({{{"type", "tts"}, {"params", {{"text", "Hello!"}}}}});
action.wait();

// Audio file
action = call.play({{{"type", "audio"}, {"params", {{"url", "https://example.com/sound.mp3"}}}}});

// Control playback
action.pause();
action.resume();
action.volume(-3.0);
action.stop();
```

Typed convenience wrappers build the media frame for you:

```cpp
call.play_tts("Hello!", "en-US");                 // text, language
call.play_audio("https://example.com/sound.mp3");
call.play_silence(2.0);                            // seconds
call.play_ringtone("us");                          // name
```

## Recording

### `Action record(const json& params = {}, const std::string& control_id = "")`

Record the call. The returned `Action` supports `stop()`, `pause()`, `resume()`,
and `wait()`. `record_call(params)` is a convenience for full-call recording.

```cpp
auto action = call.record({{"format", "wav"}, {"stereo", true}, {"direction", "both"}});
// ... later ...
action.stop();
action.wait();
std::cout << "Recording: " << action.result().dump() << "\n";
```

## Input Collection

### `Action play_and_collect(const json& play_media, const json& collect_params, const std::string& control_id = "")`

Play audio and collect DTMF or speech input.

```cpp
auto action = call.play_and_collect(
    {{{"type", "tts"}, {"params", {{"text", "Press 1 for sales, 2 for support."}}}}},
    {{"digits", {{"max", 1}, {"digit_timeout", 5.0}}}});
action.wait();
```

`prompt(play_media, collect_params, control_id)` is an alias, and
`prompt_tts(...)` / `prompt_audio(...)` are typed convenience wrappers.

### `Action collect(const json& params, const std::string& control_id = "")`

Collect input without playing audio.

```cpp
auto action = call.collect({
    {"digits", {{"max", 4}, {"terminators", "#"}}},
    {"speech", {{"language", "en-US"}}},
    {"partial_results", true},
});
action.wait();
```

## Bridging

### `Action connect(const json& devices)`

Bridge the call to another destination. `devices` is the nested
serial/parallel device array.

```cpp
call.connect({{
    {{"type", "phone"}, {"params", {{"to_number", "+15551234567"}, {"from_number", "+15559876543"}}}}
}});
```

### `Action disconnect()`

Unbridge a connected call.

```cpp
call.disconnect();
```

## DTMF

### `Action send_digits(const std::string& digits)`

Send DTMF tones.

```cpp
call.send_digits("1234#");
```

## Detection

### `Action detect(const json& params, const std::string& control_id = "")`

Detect machine, fax, or digits.

```cpp
auto action = call.detect({{"type", "machine"}, {"params", {{"initial_timeout", 4.5}}}});
action.wait();
```

Typed wrappers build the detect descriptor for you:

```cpp
call.detect_digit("123", 10.0);              // digits, timeout (seconds)
call.detect_answering_machine({}, 30.0);     // amd params, timeout
call.detect_fax();
```

## SIP Refer

### `Action sip_refer(const std::string& to_uri)`

Transfer via SIP REFER.

```cpp
call.sip_refer("sip:user@example.com");
```

## Transfer

### `Action transfer(const json& params)`

Transfer call control to another RELAY app or SWML script.

```cpp
call.transfer({{"dest", "https://example.com/swml-endpoint"}});
```

## Fax

### `Action send_fax(const std::string& document_url, const std::string& header = "", const std::string& identity = "", const std::string& control_id = "")`

```cpp
auto action = call.send_fax("https://example.com/document.pdf", "", "+15551234567");
action.wait();
```

### `Action receive_fax(const std::string& control_id = "")`

```cpp
auto action = call.receive_fax();
action.wait();
```

## Tap (Media Interception)

### `Action tap(const json& params, const std::string& control_id = "")`

Intercept call media and stream to an RTP endpoint. `tap_audio(params, control_id)`
and `stop_tap(control_id)` are also available.

```cpp
auto action = call.tap({
    {"tap", {{"type", "audio"}, {"params", {{"direction", "both"}}}}},
    {"device", {{"type", "rtp"}, {"params", {{"addr", "192.168.1.100"}, {"port", 5000}}}}},
});
```

## Streaming

### `Action stream(const json& params, const std::string& control_id = "")`

Stream call audio to a WebSocket endpoint. `stream(...).stop()` ends the stream.

```cpp
auto action = call.stream({
    {"url", "wss://example.com/audio"},
    {"codec", "PCMU"},
    {"track", "inbound_track"},
});
action.stop();
```

## Payment

### `Action pay(const json& params, const std::string& control_id = "")`

Collect a payment via DTMF.

```cpp
auto action = call.pay({
    {"payment_connector_url", "https://pay.example.com"},
    {"charge_amount", "25.99"},
    {"currency", "usd"},
    {"input_method", "dtmf"},
});
action.wait();
```

## Conference & Rooms

### `Action join_conference(const std::string& name, const json& params = {})`

```cpp
call.join_conference("my_conference", {{"muted", false}, {"beep", "onEnter"}});
```

### `Action join_room(const std::string& name)`

```cpp
call.join_room("my_room");
```

## Hold

### `Action hold()` / `Action unhold()`

```cpp
call.hold();
// ... later ...
call.unhold();
```

## Transcription

### `Action transcribe(const json& params = {}, const std::string& control_id = "")`

```cpp
auto action = call.transcribe({{"status_url", "https://example.com/transcription"}});
// ... later ...
action.stop();
```

### `Action live_transcribe(const json& params = {})` / `Action live_translate(const json& params = {})`

```cpp
call.live_transcribe({{"start", {{"language", "en-US"}}}});
call.live_translate({{"start", {{"source", "en-US"}, {"target", "es"}}}});
```

## AI Agent

### `Action ai(const json& params, const std::string& control_id = "")`

Start an AI agent session on the call.

```cpp
auto action = call.ai({
    {"prompt", {{"text", "You are a helpful support agent."}}},
    {"SWAIG", {{"functions", json::array()}}},
    {"ai_params", {{"end_of_speech_timeout", 3000}}},
});
action.wait();
```

## Execute SWML

### `Action execute_swml(const json& swml)`

Execute an inline SWML document on the call.

```cpp
call.execute_swml({{"version", "1.0.0"}, {"sections", {/* ... */}}});
```

## Event Handling

### `void on_event(CallEventHandler handler)`

Register an observer for every event on this call.
`CallEventHandler = std::function<void(const CallEvent&)>`.

```cpp
call.on_event([](const signalwire::relay::CallEvent& ev) {
    std::cout << "Event: " << ev.event_type << " state=" << ev.call_state << "\n";
});
```

### State waits

Each returns `bool` (`true` on reaching the state, `false` on timeout);
`timeout_ms <= 0` waits indefinitely.

```cpp
call.wait_for_answered(30000);
call.wait_for_ringing();
call.wait_for_ending();
call.wait_for_ended();
```
