// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#pragma once

#include <stdexcept>
#include <string>

namespace signalwire {
namespace rest {

/// Enumeration of ``call_handler`` values accepted by ``phone_numbers.update``.
///
/// Setting a phone number's ``call_handler`` + the handler-specific companion
/// field routes inbound calls and auto-materializes the matching Fabric
/// resource on the server. The typed ``set_*`` helpers on
/// ``PhoneNumbersNamespace`` wrap the low-level update call with the right
/// combination — prefer those over constructing the wire body by hand.
///
/// Named ``PhoneCallHandler`` (not ``CallHandler``) to avoid colliding with
/// ``signalwire::relay::InboundCallHandler`` — the callback type used by the
/// RELAY client for inbound-call events.
///
/// | Enum value          | Wire value          | Companion field              | Auto-materializes |
/// |---------------------|---------------------|------------------------------|-------------------|
/// | `RelayScript`       | `relay_script`      | `call_relay_script_url`      | `swml_webhook`    |
/// | `LamlWebhooks`      | `laml_webhooks`     | `call_request_url`           | `cxml_webhook`    |
/// | `LamlApplication`   | `laml_application`  | `call_laml_application_id`   | `cxml_application`|
/// | `AiAgent`           | `ai_agent`          | `call_ai_agent_id`           | `ai_agent`        |
/// | `CallFlow`          | `call_flow`         | `call_flow_id`               | `call_flow`       |
/// | `RelayApplication`  | `relay_application` | `call_relay_application`     | `relay_application`
/// | | `RelayTopic`        | `relay_topic`       | `call_relay_topic`           | (RELAY routing) |
/// | `RelayContext`      | `relay_context`     | `call_relay_context`         | (legacy)          |
/// | `RelayConnector`    | `relay_connector`   | (connector config)           | (internal)        |
/// | `VideoRoom`         | `video_room`        | `call_video_room_id`         | (Video API)       |
/// | `Dialogflow`        | `dialogflow`        | `call_dialogflow_agent_id`   | (none)            |
///
/// Note: ``LamlWebhooks`` (wire value ``laml_webhooks``) produces a **cXML**
/// handler despite the plural name. For SWML, use ``RelayScript``.
enum class PhoneCallHandler {
  RelayScript,
  LamlWebhooks,
  LamlApplication,
  AiAgent,
  CallFlow,
  RelayApplication,
  RelayTopic,
  RelayContext,
  RelayConnector,
  VideoRoom,
  Dialogflow,
};

/// Serialize a ``PhoneCallHandler`` to its wire string.
[[nodiscard]] inline std::string to_wire_string(PhoneCallHandler h) {
  switch (h) {
    case PhoneCallHandler::RelayScript:
      return "relay_script";
    case PhoneCallHandler::LamlWebhooks:
      return "laml_webhooks";
    case PhoneCallHandler::LamlApplication:
      return "laml_application";
    case PhoneCallHandler::AiAgent:
      return "ai_agent";
    case PhoneCallHandler::CallFlow:
      return "call_flow";
    case PhoneCallHandler::RelayApplication:
      return "relay_application";
    case PhoneCallHandler::RelayTopic:
      return "relay_topic";
    case PhoneCallHandler::RelayContext:
      return "relay_context";
    case PhoneCallHandler::RelayConnector:
      return "relay_connector";
    case PhoneCallHandler::VideoRoom:
      return "video_room";
    case PhoneCallHandler::Dialogflow:
      return "dialogflow";
  }
  throw std::invalid_argument("Unknown PhoneCallHandler value");
}

}  // namespace rest
}  // namespace signalwire
