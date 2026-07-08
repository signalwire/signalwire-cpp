# swaig::FunctionResult Methods Reference (C++)

SWAIG (SignalWire AI Gateway) is the platform's AI tool-calling system -- it connects the AI's decisions to actions like call transfers, SMS, recordings, and API calls, with native access to the media stack. This document is a reference for the `swaig::FunctionResult` class. These methods provide convenient abstractions for SWAIG actions, eliminating the need to manually construct action JSON objects.

Every action method returns `FunctionResult&` (a reference to `*this`), so calls chain fluently. The class is constructed and returned from a `swaig::ToolHandler`:

<!-- snippet-setup -->
```cpp
#include <signalwire/agent/agent_base.hpp>
#include <signalwire/swaig/function_result.hpp>
#include <signalwire/swaig/parameter_schema.hpp>
#include <nlohmann/json.hpp>
#include <iostream>
using json = nlohmann::json;
signalwire::agent::AgentBase agent("my-agent");
signalwire::swaig::FunctionResult result("ok");
```

## Core Methods

### Basic Construction & Control

#### `FunctionResult(response = "", post_process = false)`
Creates a new result object with optional response text and post-processing behavior.

```cpp
signalwire::swaig::FunctionResult r1("Hello, I'll help you with that");
signalwire::swaig::FunctionResult r2("Processing request...", /*post_process=*/true);
```

#### `set_response(response)`
Sets or updates the response text that the AI will speak.

```cpp
result.set_response("I've updated your information");
```

#### `set_post_process(post_process)`
Controls whether the AI gets one more turn before executing actions.

```cpp
result.set_post_process(true);   // AI speaks response before executing actions
result.set_post_process(false);  // Actions execute immediately
```

---

## Action Methods

### Call Control

#### `execute_swml(swml_content, transfer = false)`
Execute SWML content, with optional transfer behavior. `swml_content` is a `json` value.

```cpp
// SWML as a json object
json swml = {{"version", "1.0.0"}, {"sections", {{"main", {{{"say", "Hello"}}}}}}};
result.execute_swml(swml, /*transfer=*/true);
```

#### `connect(destination, final = true, from_addr = "")`
Transfer/connect the call to another destination using SWML.

```cpp
result.connect("+15551234567", /*final=*/true);                       // Permanent transfer
result.connect("support@company.com", /*final=*/false, "+15559876543"); // Temporary transfer
```

#### `swml_transfer(dest, ai_response, final = true)`
Transfer the call to a destination and provide the response the AI speaks on return.

```cpp
result.swml_transfer("+15551234567", "You are now back with the assistant.", true);
```

#### `send_sms(to, from, body = "", media = {}, tags = {}, region = "")`
Send an SMS message to a PSTN phone number using SWML.

```cpp
// Simple text message
result.send_sms("+15551234567", "+15559876543", "Your order has been confirmed!");

// Media message with images (body empty, media vector populated)
result.send_sms("+15551234567", "+15559876543", "",
                {"https://example.com/receipt.jpg", "https://example.com/map.png"});

// Full featured message with tags and region
result.send_sms("+15551234567", "+15559876543", "Order update",
                {"https://example.com/receipt.pdf"},
                {"order", "confirmation", "customer"},
                "us");
```

**Parameters:**
- `to` (required): phone number in E.164 format to send to
- `from` (required): phone number in E.164 format to send from
- `body` (optional): message text (required if no media)
- `media` (optional): vector of URLs to send (required if no body)
- `tags` (optional): vector of tags for UI searching
- `region` (optional): region to originate the message from

#### `pay(payment_connector_url, ...)`
Process payments using the SWML pay action. The first argument is the connector URL; the remaining parameters (input method, timeout, amount, currency, prompts, etc.) all have defaults.

```cpp
// Simple payment setup
result.pay("https://api.example.com/accept-payment",
           /*input_method=*/"dtmf", /*status_url=*/"", /*payment_method=*/"credit-card",
           /*timeout=*/10, /*max_attempts=*/3);
```

Build custom prompts and parameters with the static helpers:

```cpp
// Create payment actions
std::vector<json> welcome = {
    signalwire::swaig::FunctionResult::create_payment_action("Say", "Welcome to our payment system"),
    signalwire::swaig::FunctionResult::create_payment_action("Say", "Please enter your credit card number")
};
json card_prompt = signalwire::swaig::FunctionResult::create_payment_prompt("payment-card-number", welcome);

// Create payment parameters
std::vector<json> params = {
    signalwire::swaig::FunctionResult::create_payment_parameter("customer_id", "12345"),
    signalwire::swaig::FunctionResult::create_payment_parameter("order_id", "ORD-789")
};
```

**Static helper methods:**
- `create_payment_action(action_type, phrase)` → `json`
- `create_payment_prompt(for_situation, actions, card_type = "", error_type = "")` → `json`
- `create_payment_parameter(name, value)` → `json`

#### `record_call(control_id = "", stereo = false, format = "wav", direction = "both", ...)`
Start background call recording using SWML. The script continues executing while recording happens in the background.

```cpp
// Simple background recording
result.record_call();

// Recording with custom settings
result.record_call("support_call_001", /*stereo=*/true, "mp3", "both");
```

A typed overload accepts the `RecordFormat` and `RecordDirection` enums in place of the string `format`/`direction`, for call-site typo checking:

```cpp
result.record_call("voicemail", false, signalwire::swaig::RecordFormat::Wav, signalwire::swaig::RecordDirection::Speak);
```

**Core parameters:**
- `control_id` (optional): identifier for this recording (use with `stop_record_call`)
- `stereo`: record in stereo (default: false)
- `format`: `"wav"`, `"mp3"`, or `"mp4"` (default: `"wav"`)
- `direction`: `"speak"`, `"listen"`, or `"both"` (default: `"both"`)
- additional optional parameters: `terminators`, `beep`, `input_sensitivity`, `initial_timeout`, `end_silence_timeout`, `max_length`, `status_url`

#### `stop_record_call(control_id = "")`
Stop an active background call recording. If `control_id` is empty, stops the most recent recording.

```cpp
result.stop_record_call();                          // Stop the most recent recording
result.stop_record_call("support_call_001");        // Stop a specific recording

// Chain to stop recording and provide feedback
result.stop_record_call("customer_voicemail")
      .say("Thank you, your message has been recorded");
```

#### `join_room(name)`
Join a RELAY room (for multi-party communication and collaboration).

```cpp
result.join_room("support_team_room");

result.join_room("customer_meeting_001")
      .say("Welcome to the customer meeting room");
```

#### `sip_refer(to_uri)`
Send a SIP REFER for call transfer in SIP environments.

```cpp
result.sip_refer("sip:support@company.com");

result.say("Transferring your call to our specialist")
      .sip_refer("sip:specialist@company.com");
```

#### `join_conference(name, ...)`
Join an ad-hoc audio conference. There are two overloads: a flat positional overload mirroring the SWML parameters 1:1, and an options-bag overload taking a `swaig::JoinConferenceOptions` struct (the C++-idiomatic way to pass the many optional parameters).

```cpp
// Simple conference join
result.join_conference("my_conference");

// Options-bag overload — set only the fields you need
signalwire::swaig::JoinConferenceOptions opts;
opts.record = signalwire::swaig::ConferenceRecord::RecordFromStart;
opts.max_participants = 50;
opts.beep = signalwire::swaig::ConferenceBeep::OnEnter;
result.join_conference("customer_support_conf", opts);
```

The closed-set fields (`beep`, `record`, `trim`, the callback methods) accept either the typed enum or a bare string; both produce identical SWML.

#### `tap(uri, control_id = "", direction = "both", codec = "PCMU", rtp_ptime = 20, status_url = "")`
Start a background call tap. Media is streamed over WebSocket or RTP to a customer-controlled URI for real-time monitoring.

```cpp
// Simple WebSocket tap
result.tap("wss://example.com/tap");

// RTP tap with custom settings
result.tap("rtp://192.168.1.100:5004", "monitoring_tap_001", "both", "PCMA", 30);
```

A typed overload accepts the `TapDirection` and `Codec` enums. Note the tap direction set is `{speak, hear, both}` (`hear`, not `record_call`'s `listen`):

```cpp
result.tap("wss://monitoring.example.com/audio", "compliance_tap",
           signalwire::swaig::TapDirection::Speak, signalwire::swaig::Codec::Pcmu);
```

#### `stop_tap(control_id = "")`
Stop an active tap stream. If `control_id` is empty, stops the last tap started.

```cpp
result.stop_tap();
result.stop_tap("monitoring_tap_001");
```

#### `hangup()`
Terminate the call immediately.

```cpp
result.hangup();
```

---

### Call Flow Control

#### `hold(timeout = 300)`
Put the call on hold with a timeout (in seconds).

```cpp
result.hold(60);   // Hold for 1 minute
result.hold(600);  // Hold for 10 minutes
```

#### `wait_for_user(enabled = nullopt, timeout = nullopt, answer_first = false)`
Control how the agent waits for user input. `enabled` and `timeout` are `std::optional`.

```cpp
result.wait_for_user(true);                   // Wait indefinitely
result.wait_for_user(std::nullopt, 30);       // Wait 30 seconds
result.wait_for_user(std::nullopt, std::nullopt, /*answer_first=*/true);
result.wait_for_user(false);                  // Disable waiting
```

#### `stop()`
Stop agent execution completely.

```cpp
result.stop();
```

---

### Speech & Audio Control

#### `say(text)`
Make the agent speak specific text immediately.

```cpp
result.say("Please hold while I look that up for you");
```

#### `play_background_file(filename, wait = false)`
Play an audio file in the background. With `wait=true`, the AI suppresses attention while it plays.

```cpp
result.play_background_file("hold_music.wav");                 // AI tries to get attention
result.play_background_file("announcement.mp3", /*wait=*/true); // AI suppresses attention
```

#### `stop_background_file()`
Stop the currently playing background audio.

```cpp
result.stop_background_file();
```

---

### Speech Recognition Settings

#### `set_end_of_speech_timeout(milliseconds)`
Set the silence timeout after speech detection for finalizing recognition.

```cpp
result.set_end_of_speech_timeout(2000);  // 2 seconds of silence
```

#### `set_speech_event_timeout(milliseconds)`
Set the timeout since the last speech event — better for noisy environments.

```cpp
result.set_speech_event_timeout(3000);  // 3 seconds since last speech event
```

#### `add_dynamic_hints(hints)` / `clear_dynamic_hints()`
Add (or clear) speech-recognition hints for this turn.

```cpp
result.add_dynamic_hints({{"hints", json::array({"SignalWire", "SWML"})}});
result.clear_dynamic_hints();
```

---

### Data Management

#### `update_global_data(data)`
Merge values into the global agent data.

```cpp
result.update_global_data({{"user_name", "John"}, {"step", 2}});
```

#### `remove_global_data(keys)`
Remove global data variables by key(s). `keys` is a `json` value (a single string or an array of strings).

```cpp
result.remove_global_data("temporary_data");          // Single key
result.remove_global_data({"step", "temp_value"});    // Multiple keys
```

#### `set_metadata(data)`
Set metadata scoped to the current function's meta_data_token.

```cpp
result.set_metadata({{"session_id", "abc123"}, {"user_tier", "premium"}});
```

#### `remove_metadata(keys)`
Remove metadata from the current function's scope.

```cpp
result.remove_metadata("temp_session_data");          // Single key
result.remove_metadata({"cache_key", "temp_flag"});   // Multiple keys
```

#### `swml_user_event(event_data)`
Emit a SWML user event.

```cpp
result.swml_user_event({{"event", "checkout_complete"}});
```

---

### Function & Behavior Control

#### `toggle_functions(function_toggles)`
Enable/disable specific SWAIG functions dynamically. The argument is a `json` array of toggle objects.

```cpp
result.toggle_functions(json::array({
    {{"function", "transfer_call"}, {"active", false}},
    {{"function", "lookup_info"},   {"active", true}}
}));
```

#### `enable_functions_on_timeout(enabled = true)`
Control whether functions can be called on speaker timeout.

```cpp
result.enable_functions_on_timeout(true);
result.enable_functions_on_timeout(false);
```

#### `enable_extensive_data(enabled = true)`
Send full data to the LLM for this turn only, then use a smaller replacement.

```cpp
result.enable_extensive_data(true);
```

#### `replace_in_history(text)`
Remove or replace the tool_call + tool_result pair from the LLM's conversation history after the first send. This is useful when a function call is an implementation detail that would confuse the model if it remained visible in context. The argument is a `json` value: pass `true` to remove the pair entirely, or a string to replace it with an assistant message containing that text.

```cpp
// Remove entirely — LLM won't see this function was called
signalwire::swaig::FunctionResult r1("Done.");
r1.replace_in_history(true);

// Replace with a friendly assistant message instead of tool artifacts
signalwire::swaig::FunctionResult r2("Profile saved.");
r2.replace_in_history("I've saved your profile information.");
```

**When to use:**
- Functions that are implementation details (saving data, logging, internal state changes)
- Functions called frequently that would bloat conversation history
- Situations where tool artifacts confuse the model's reasoning

---

### Agent Settings & Configuration

#### `update_settings(settings)`
Update agent runtime settings with server-side validation. The argument is a `json` object.

```cpp
// AI model settings
result.update_settings({
    {"temperature", 0.7},
    {"max-tokens", 2048},
    {"frequency-penalty", -0.5}
});

// Speech recognition settings
result.update_settings({
    {"confidence", 0.8},
    {"barge-confidence", 0.7}
});
```

**Supported settings:**
- `frequency-penalty`: float (-2.0 to 2.0)
- `presence-penalty`: float (-2.0 to 2.0)
- `max-tokens`: integer (0 to 4096)
- `top-p`: float (0.0 to 1.0)
- `confidence`: float (0.0 to 1.0)
- `barge-confidence`: float (0.0 to 1.0)
- `temperature`: float (0.0 to 2.0, clamped to 1.5)

#### `switch_context(system_prompt = "", user_prompt = "", consolidate = false, full_reset = false)`
Change the agent's context/prompt during a conversation.

```cpp
// Simple context switch
result.switch_context("You are now a technical support agent");

// Advanced context switch
result.switch_context("You are a billing specialist",
                      "The user needs help with their invoice",
                      /*consolidate=*/true);
```

#### `swml_change_step(step_name)` / `swml_change_context(context_name)`
Navigate a multi-step / multi-context flow by name.

```cpp
result.swml_change_step("collect_payment");
result.swml_change_context("billing");
```

#### `simulate_user_input(text)`
Queue simulated user input for testing or flow control.

```cpp
result.simulate_user_input("Yes, I'd like to speak to billing");
```

---

## Low-Level Methods

### Manual Action Construction

#### `add_action(name, data)`
Add a single action manually (for custom actions not covered by the helper methods).

```cpp
result.add_action("custom_action", {{"param", "value"}});
```

#### `add_actions(actions)`
Add multiple actions at once from a vector of JSON action objects.

```cpp
result.add_actions({
    {{"say", "Hello"}},
    {{"hold", 300}}
});
```

### Output Generation

#### `to_json()` / `to_string(indent = -1)`
Render the result to JSON, or to a JSON string (optionally indented).

```cpp
json j = result.to_json();
std::string s = result.to_string(2);  // pretty-printed with 2-space indent
```

---

## RPC Methods

`FunctionResult` also exposes low-level RELAY RPC helpers for advanced call control:

- `execute_rpc(method, params = {}, call_id = "", node_id = "")`
- `rpc_dial(to_number, from_number, dest_swml, device_type = "phone")`
- `rpc_ai_message(call_id, message_text, role = "system")`
- `rpc_ai_unhold(call_id)`

```cpp
result.rpc_ai_message("call-abc-123", "A new ticket was created for you.", "system");
```

---

## Method Chaining

All action methods return `*this`, enabling fluent chaining:

```cpp
signalwire::swaig::FunctionResult my_result =
    signalwire::swaig::FunctionResult("Processing your request", /*post_process=*/true)
        .update_global_data({{"status", "processing"}})
        .play_background_file("processing.wav", /*wait=*/true)
        .set_end_of_speech_timeout(2500);

// Transfer example
signalwire::swaig::FunctionResult transfer =
    signalwire::swaig::FunctionResult("Let me transfer you to billing")
        .set_metadata({{"transfer_reason", "billing_inquiry"}})
        .update_global_data({{"last_action", "transfer_to_billing"}})
        .connect("+15551234567", /*final=*/true);
```

---

## Best Practices

1. **Use `post_process=true`** when you want the AI to speak before executing actions.
2. **Chain methods** for cleaner, more readable code.
3. **Use the specific helper methods** instead of manual `add_action` construction when one is available.
4. **Validate settings** — `update_settings()` relies on server-side validation.

---

## Post Data Reference

The `post_data` object is the JSON payload sent to SWAIG function handlers — it is delivered as the `raw_data` argument of a `swaig::ToolHandler`. Its structure differs between webhook functions and DataMap functions.

### Base Keys (All Functions)

| Key | Type | Description |
|-----|------|-------------|
| `app_name` | string | Name of the AI application |
| `function` | string | Name of the SWAIG function being called |
| `call_id` | string | Unique UUID of the current call session |
| `ai_session_id` | string | Unique UUID of the AI session |
| `caller_id_name` | string | Caller ID name (if available) |
| `caller_id_num` | string | Caller ID number (if available) |
| `channel_active` | boolean | Whether the channel is currently up |
| `channel_offhook` | boolean | Whether the channel is off-hook |
| `channel_ready` | boolean | Whether the AI session is ready |
| `argument` | object | Parsed function arguments |
| `argument_desc` | object | Function argument schema/description |
| `purpose` | string | Description of what the function does |
| `content_type` | string | Always `"text/swaig"` |
| `version` | string | SWAIG protocol version |
| `global_data` | object | Application-level global data (when set) |
| `conversation_id` | string | Conversation identifier (when tracking enabled) |
| `project_id` | string | SignalWire project ID |
| `space_id` | string | SignalWire space ID |

### Webhook-Only Keys

These keys are only present for traditional webhook SWAIG functions:

| Key | Type | Description | Present When |
|-----|------|-------------|--------------|
| `meta_data_token` | string | Token for metadata access | Function has metadata token |
| `meta_data` | object | Function-level metadata | Function has metadata token |
| `SWMLVars` | object | SWML variables | `swaig_post_swml_vars` parameter set |
| `SWMLCall` | object | SWML call state | `swaig_post_swml_vars` parameter set |
| `call_log` | array | Processed conversation history | `swaig_post_conversation` is true |
| `raw_call_log` | array | Raw conversation history | `swaig_post_conversation` is true |

**Metadata scoping**: Functions sharing the same `meta_data_token` share access to the same metadata. If no token is specified, scope defaults to function name/URL.

**Conversation history**: `call_log` may shrink after conversation resets (consolidation), while `raw_call_log` preserves full history. Both include timing data (latency, utterance_latency, audio_latency).

### DataMap-Specific Keys

| Key | Type | Description |
|-----|------|-------------|
| `prompt_vars` | object | Template variables built from call context, SWML vars, and global_data |
| `args` | object | First parsed argument object for easy template access |
| `input` | object | Copy of entire post_data for variable expansion |

### prompt_vars Contents

| Key | Source | Description |
|-----|--------|-------------|
| `call_direction` | Call direction | `"inbound"` or `"outbound"` |
| `caller_id_name` | Channel variable | Caller's name |
| `caller_id_number` | Channel variable | Caller's number |
| `local_date` | System time | Current date in local timezone |
| `local_time` | System time | Current time with timezone |
| `time_of_day` | Derived from hour | `"morning"`, `"afternoon"`, or `"evening"` |
| `supported_languages` | App config | Available languages |
| `default_language` | App config | Primary language |

All keys from `global_data` are also merged into `prompt_vars`, with global_data taking precedence.

### SWML Parameters Controlling post_data

| Parameter | Type | Default | Purpose |
|-----------|------|---------|---------|
| `swaig_allow_swml` | boolean | true | Allow functions to execute SWML actions |
| `swaig_allow_settings` | boolean | true | Allow functions to modify AI settings |
| `swaig_post_conversation` | boolean | false | Include conversation history in post_data |
| `swaig_set_global_data` | boolean | true | Allow functions to modify global_data |
| `swaig_post_swml_vars` | boolean/array | false | Include SWML variables in post_data |

### Variable Expansion in DataMap

DataMap processing supports template expansion with access to:

- Nested object access via dot notation: `${user.name}`
- Array access: `${items[0].value}`
- Encoding functions: `${enc:url:variable}`
- Built-in functions: `@{strftime %Y-%m-%d}`, `@{expr 2+2}`

---

## Related Documentation

- **[Agent Guide](agent_guide.md)** — general agent development guide
- **`examples/simple_agent.cpp`** — basic SWAIG function usage
- **`examples/swaig_features_agent.cpp`** — advanced `FunctionResult` action usage (state, media, speech, context, SMS)
