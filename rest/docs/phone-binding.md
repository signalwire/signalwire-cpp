# Binding a phone number to a call handler

Routing an inbound phone number to something — an SWML webhook, a cXML app, an AI Agent, a call flow — is configured on the **phone number**, not on the Fabric resource. Fabric resources are *derived representations* of bindings configured on adjacent entities. Read this page before writing code that creates webhook/agent/flow resources manually; for the common cases, you don't need to.

## The mental model

A phone number has a `call_handler` field that chooses what to do with inbound calls. Setting `call_handler` (together with its handler-specific required field) triggers the server to materialize the appropriate Fabric resource automatically.

```
┌────────────────────────┐      sets       ┌──────────────────────────┐
│ PUT  /phone_numbers/X  │────────────────▶│ call_handler +           │
│ (you call this)        │                 │ handler-specific URL/ID  │
└────────────────────────┘                 └──────────────────────────┘
                                                        │
                                                        ▼
                                       ┌──────────────────────────────┐
                                       │ Fabric resource materializes │
                                       │ automatically, keyed off the │
                                       │ URL or ID you supplied       │
                                       └──────────────────────────────┘
```

You rarely create a Fabric webhook resource directly. Doing so without binding a phone number to it leaves an orphan.

## The `PhoneCallHandler` enum

The authoritative list of handler values, shipped in `include/signalwire/rest/phone_call_handler.hpp`.

| `PhoneCallHandler`  | `call_handler` wire value | Required companion field     | Auto-materializes Fabric resource |
|---------------------|---------------------------|------------------------------|-----------------------------------|
| `RelayScript`       | `relay_script`            | `call_relay_script_url`      | `swml_webhook`                    |
| `LamlWebhooks`      | `laml_webhooks`           | `call_request_url`           | `cxml_webhook`                    |
| `LamlApplication`   | `laml_application`        | `call_laml_application_id`   | `cxml_application`                |
| `AiAgent`           | `ai_agent`                | `call_ai_agent_id`           | `ai_agent`                        |
| `CallFlow`          | `call_flow`               | `call_flow_id`               | `call_flow`                       |
| `RelayApplication`  | `relay_application`       | `call_relay_application`     | `relay_application`               |
| `RelayTopic`        | `relay_topic`             | `call_relay_topic`           | *(no Fabric resource — routes via RELAY client)* |
| `RelayContext`      | `relay_context`           | `call_relay_context`         | *(no Fabric resource — legacy; prefer `RelayTopic`)* |
| `RelayConnector`    | `relay_connector`         | *(connector config)*         | *(internal)*                      |
| `VideoRoom`         | `video_room`              | `call_video_room_id`         | *(no Fabric resource — routes to Video API)* |
| `Dialogflow`        | `dialogflow`              | `call_dialogflow_agent_id`   | *(no Fabric resource)*            |

**Naming note on `laml_webhooks`:** The wire value is plural and contains "webhooks", but it produces a **cXML** (Twilio-compat) handler — not a generic webhook, not an SWML webhook. For SWML, use `RelayScript`. The dashboard labels these resources "cXML Webhook" after assignment.

**`calling_handler_resource_id`** (where present in responses) is **server-derived** and read-only. Don't try to set it on update; the server computes it from the handler you chose.

## Typed helpers on `phone_numbers()`

Seven typed helpers wrap `phone_numbers().update` with the right `call_handler` value and companion field already set. They're the one-line recipe for every common case.

```cpp
#include <signalwire/rest/rest_client.hpp>
#include <signalwire/rest/phone_call_handler.hpp>

using namespace signalwire::rest;

auto client = RestClient::from_env();

// SWML webhook (the common case — your backend returns SWML per call)
client.phone_numbers().set_swml_webhook(pn_id, "https://example.com/swml");

// cXML / LAML webhook (Twilio-compat)
RestClient::PhoneNumbersNamespace::CxmlWebhookOptions cxml_opts;
cxml_opts.fallback_url        = "https://example.com/fallback.xml"; // optional
cxml_opts.status_callback_url = "https://example.com/status";       // optional
client.phone_numbers().set_cxml_webhook(pn_id, "https://example.com/voice.xml", cxml_opts);

// Existing cXML application by ID
client.phone_numbers().set_cxml_application(pn_id, "app-uuid");

// AI Agent by ID (agent created via fabric.ai_agents or AgentBase)
client.phone_numbers().set_ai_agent(pn_id, "agent-uuid");

// Call flow (optionally pin a version — default is current_deployed)
RestClient::PhoneNumbersNamespace::CallFlowOptions cf_opts;
cf_opts.version = "current_deployed";
client.phone_numbers().set_call_flow(pn_id, "flow-uuid", cf_opts);

// Relay application (named routing)
client.phone_numbers().set_relay_application(pn_id, "my-relay-app");

// Relay topic (RELAY client subscription)
client.phone_numbers().set_relay_topic(pn_id, "office");
```

All helpers return the updated phone number representation. All are thin wrappers over the single underlying `phone_numbers().update(sid, body)` call; use the update form directly when you need an unusual combination.

The wire-level form is always available:

```cpp
client.phone_numbers().update(pn_id, {
    {"call_handler", to_wire_string(PhoneCallHandler::RelayScript)},
    {"call_relay_script_url", "https://example.com/swml"},
});
```

## What NOT to do

### Don't pre-create the webhook resource

Don't construct an SWML/cXML webhook resource manually and then try to attach a phone number to it:

- `fabric().swml_webhooks` and `fabric().cxml_webhooks` are not exposed on this SDK's Fabric namespace. They appear only as **server-derived** resources after you call `phone_numbers().set_swml_webhook(...)` / `set_cxml_webhook(...)`.
- There is deliberately no `assign_phone_route` method in this SDK. Other SDKs ship one for legacy reasons; it does not work for SWML/cXML/AI-agent bindings.

If you find yourself writing "create a webhook, then attach a phone number" — stop. The binding *is* the create.

## Summary

- Bindings live on `phone_numbers`, not on Fabric resources.
- Set `call_handler` + the one handler-specific field; the server materializes the resource for you.
- Use the typed `phone_numbers().set_*` helpers — they document the enum values inline.
- `swml_webhook` and `cxml_webhook` Fabric resources are auto-materialized. There is no `create` surface for them in this SDK.
- `laml_webhooks` produces a **cXML** handler despite the name. Use `RelayScript` for SWML.
- `assign_phone_route` is deliberately not shipped; it is not needed for the common handlers.

See the working example: `rest/examples/rest_bind_phone_to_swml_webhook.cpp`.
