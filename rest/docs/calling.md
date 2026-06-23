# Calling Commands

The Calling API provides REST-based call control. All commands are dispatched via a single `POST /api/calling/calls` endpoint with a `command` field. No WebSocket connection is needed.

## How It Works

Every method on `client.calling()` sends a POST request with this structure:

```json
{
    "command": "calling.play",
    "id": "<call-uuid>",
    "params": { ... }
}
```

For `dial` and `update`, the call details are inside `params` (no top-level `id`).
For all other commands, the `call_id` argument is the UUID of the call to
control. Each method returns the `nlohmann::json` response body; bodies are
passed as `nlohmann::json` objects.

## Call Lifecycle

### `dial(const json& params) -> json`

Initiate an outbound call.

```cpp
auto result = client.calling().dial({
    {"from", "+15559876543"},
    {"to", "+15551234567"},
    {"url", "https://example.com/call-handler"},
});
std::string call_id = result.value("id", "");
```

### `update(const json& params) -> json`

Update an active call's dialplan mid-call.

```cpp
client.calling().update({{"id", call_id}, {"url", "https://example.com/new-handler"}});
```

### `end(const std::string& call_id, const json& params = {}) -> json`

Terminate a call.

```cpp
client.calling().end(call_id, {{"reason", "hangup"}});
```

### `transfer(const std::string& call_id, const json& params) -> json`

Transfer a call to a new destination.

```cpp
client.calling().transfer(call_id, {{"dest", "sip:agent@example.com"}});
```

### `disconnect(const std::string& call_id, const json& params = {}) -> json`

Disconnect bridged calls without hanging up either leg.

```cpp
client.calling().disconnect(call_id);
```

## Audio Playback

### `play(const std::string& call_id, const json& params) -> json`

Play audio, TTS, silence, or ringtone.

```cpp
client.calling().play(call_id, {
    {"play", {{{"type", "tts"}, {"text", "Hello!"}}}},
    {"volume", 5.0},
});
```

### `play_pause` / `play_resume`

Pause or resume active playback.

```cpp
client.calling().play_pause(call_id, {{"control_id", "ctrl-1"}});
client.calling().play_resume(call_id, {{"control_id", "ctrl-1"}});
```

### `play_stop`

Stop active playback.

```cpp
client.calling().play_stop(call_id, {{"control_id", "ctrl-1"}});
```

### `play_volume`

Adjust playback volume.

```cpp
client.calling().play_volume(call_id, {{"control_id", "ctrl-1"}, {"volume", -3.0}});
```

## Recording

### `record` / `record_pause` / `record_resume` / `record_stop`

```cpp
client.calling().record(call_id, {
    {"control_id", "rec-1"},
    {"audio", {{"beep", true}, {"format", "wav"}, {"stereo", true}}},
});
client.calling().record_pause(call_id, {{"control_id", "rec-1"}});
client.calling().record_resume(call_id, {{"control_id", "rec-1"}});
client.calling().record_stop(call_id, {{"control_id", "rec-1"}});
```

## Input Collection

### `collect` / `collect_stop` / `collect_start_input_timers`

```cpp
client.calling().collect(call_id, {
    {"control_id", "coll-1"},
    {"digits", {{"max", 4}, {"terminators", "#"}}},
    {"speech", {{"end_silence_timeout", 2.0}}},
});
client.calling().collect_stop(call_id, {{"control_id", "coll-1"}});
client.calling().collect_start_input_timers(call_id, {{"control_id", "coll-1"}});
```

## Detection

### `detect` / `detect_stop`

```cpp
client.calling().detect(call_id, {
    {"control_id", "det-1"},
    {"detect", {{"type", "machine"}, {"params", {{"initial_timeout", 4.5}}}}},
});
client.calling().detect_stop(call_id, {{"control_id", "det-1"}});
```

## Tap & Stream

### `tap` / `tap_stop`

```cpp
client.calling().tap(call_id, {
    {"control_id", "tap-1"},
    {"tap", {{"type", "audio"}, {"params", {{"direction", "both"}}}}},
    {"device", {{"type", "rtp"}, {"params", {{"addr", "192.168.1.1"}, {"port", 1234}}}}},
});
client.calling().tap_stop(call_id, {{"control_id", "tap-1"}});
```

### `stream` / `stream_stop`

```cpp
client.calling().stream(call_id, {
    {"control_id", "str-1"},
    {"url", "wss://example.com/audio-stream"},
    {"codec", "PCMU"},
});
client.calling().stream_stop(call_id, {{"control_id", "str-1"}});
```

## Denoise

### `denoise` / `denoise_stop`

```cpp
client.calling().denoise(call_id);
client.calling().denoise_stop(call_id, json::object());
```

## Transcription

### `transcribe` / `transcribe_stop`

```cpp
client.calling().transcribe(call_id, {{"control_id", "tx-1"}, {"status_url", "https://example.com/hook"}});
client.calling().transcribe_stop(call_id, {{"control_id", "tx-1"}});
```

## AI

### `ai_message`

Inject a message into an active AI session.

```cpp
client.calling().ai_message(call_id, {{"role", "user"}, {"message_text", "Transfer me to billing"}});
```

### `ai_hold` / `ai_unhold`

```cpp
client.calling().ai_hold(call_id, {{"timeout", 60}, {"prompt", "Please wait while I transfer you."}});
client.calling().ai_unhold(call_id, {{"prompt", "I'm back, how can I help?"}});
```

### `ai_stop`

```cpp
client.calling().ai_stop(call_id);
```

## Live Transcribe & Translate

```cpp
client.calling().live_transcribe(call_id, {{"action", "start"}, {"lang", "en"}});
client.calling().live_translate(call_id, {{"action", "start"}, {"from_lang", "en"}, {"to_lang", "es"}});
```

## Fax

```cpp
client.calling().send_fax_stop(call_id, {{"control_id", "fax-1"}});
client.calling().receive_fax_stop(call_id, {{"control_id", "fax-1"}});
```

## SIP & Custom Events

```cpp
// SIP REFER transfer
client.calling().refer(call_id, {{"device", {{"to", "sip:agent@example.com"}}}});

// Custom event
client.calling().user_event(call_id, {{"event", {{"type", "custom"}, {"data", {{"key", "value"}}}}}});
```

## Complete Method List

All methods take `(const std::string& call_id, const json& params)` except
`dial`/`update` (which take `(const json& params)`) and the `*_stop` / `end` /
`disconnect` / `denoise*` / `ai_*` methods whose `params` defaults to an empty
object.

| Method | Command | Requires call_id |
|--------|---------|:-:|
| `dial(params)` | `dial` | No |
| `update(params)` | `update` | No |
| `end(call_id, params)` | `calling.end` | Yes |
| `transfer(call_id, params)` | `calling.transfer` | Yes |
| `disconnect(call_id, params)` | `calling.disconnect` | Yes |
| `play(call_id, params)` | `calling.play` | Yes |
| `play_pause(call_id, params)` | `calling.play.pause` | Yes |
| `play_resume(call_id, params)` | `calling.play.resume` | Yes |
| `play_stop(call_id, params)` | `calling.play.stop` | Yes |
| `play_volume(call_id, params)` | `calling.play.volume` | Yes |
| `record(call_id, params)` | `calling.record` | Yes |
| `record_pause(call_id, params)` | `calling.record.pause` | Yes |
| `record_resume(call_id, params)` | `calling.record.resume` | Yes |
| `record_stop(call_id, params)` | `calling.record.stop` | Yes |
| `collect(call_id, params)` | `calling.collect` | Yes |
| `collect_stop(call_id, params)` | `calling.collect.stop` | Yes |
| `collect_start_input_timers(call_id, params)` | `calling.collect.start_input_timers` | Yes |
| `detect(call_id, params)` | `calling.detect` | Yes |
| `detect_stop(call_id, params)` | `calling.detect.stop` | Yes |
| `tap(call_id, params)` | `calling.tap` | Yes |
| `tap_stop(call_id, params)` | `calling.tap.stop` | Yes |
| `stream(call_id, params)` | `calling.stream` | Yes |
| `stream_stop(call_id, params)` | `calling.stream.stop` | Yes |
| `denoise(call_id, params)` | `calling.denoise` | Yes |
| `denoise_stop(call_id, params)` | `calling.denoise.stop` | Yes |
| `transcribe(call_id, params)` | `calling.transcribe` | Yes |
| `transcribe_stop(call_id, params)` | `calling.transcribe.stop` | Yes |
| `ai_message(call_id, params)` | `calling.ai_message` | Yes |
| `ai_hold(call_id, params)` | `calling.ai_hold` | Yes |
| `ai_unhold(call_id, params)` | `calling.ai_unhold` | Yes |
| `ai_stop(call_id, params)` | `calling.ai.stop` | Yes |
| `live_transcribe(call_id, params)` | `calling.live_transcribe` | Yes |
| `live_translate(call_id, params)` | `calling.live_translate` | Yes |
| `send_fax_stop(call_id, params)` | `calling.send_fax.stop` | Yes |
| `receive_fax_stop(call_id, params)` | `calling.receive_fax.stop` | Yes |
| `refer(call_id, params)` | `calling.refer` | Yes |
| `user_event(call_id, params)` | `calling.user_event` | Yes |
