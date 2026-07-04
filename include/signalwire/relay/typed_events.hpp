// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
//
// Typed RELAY event wrappers — Python parity with signalwire.relay.event.
// Each class wraps the raw ``params`` dict from a ``signalwire.event`` message
// and exposes the event-specific fields the Python dataclasses expose. All are
// optional conveniences over the raw dict (which stays accessible via ``params``).
//
// This header is INTENTIONALLY separate from relay_event.hpp (which carries the
// port's transport-side CallEvent/ComponentEvent/DialEvent structs used by the
// RelayClient) — these live in namespace ``signalwire::relay::events`` and are
// the Python-canonical typed surface routed to ``signalwire.relay.event``.
#pragma once

#include <nlohmann/json.hpp>
#include <string>

namespace signalwire {
namespace relay {
namespace events {

using json = nlohmann::json;

/// Base typed event — wraps the raw params dict from a signalwire.event message.
struct RelayEvent {
  std::string event_type;
  json params = json::object();
  std::string call_id;
  double timestamp = 0.0;

  [[nodiscard]] static RelayEvent from_payload(const json& payload) {
    RelayEvent ev;
    ev.event_type = payload.value("event_type", std::string());
    ev.params = payload.value("params", json::object());
    ev.call_id = ev.params.value("call_id", std::string());
    ev.timestamp = ev.params.value("timestamp", 0.0);
    return ev;
  }
};

/// Typed event for the matching signalwire.event type.
struct CallStateEvent : public RelayEvent {
  std::string call_state = "";
  std::string end_reason = "";
  std::string direction = "";
  json device = json::object();
  [[nodiscard]] static CallStateEvent from_payload(const json& payload) {
    RelayEvent base = RelayEvent::from_payload(payload);
    CallStateEvent e;
    e.event_type = base.event_type;
    e.params = base.params;
    e.call_id = base.call_id;
    e.timestamp = base.timestamp;
    const json& p = base.params;
    e.call_state = p.value("call_state", std::string());
    e.end_reason = p.value("end_reason", std::string());
    e.direction = p.value("direction", std::string());
    e.device = p.value("device", json::object());
    return e;
  }
};

/// Typed event for the matching signalwire.event type.
struct CallReceiveEvent : public RelayEvent {
  std::string call_state = "";
  std::string direction = "";
  json device = json::object();
  std::string node_id = "";
  std::string project_id = "";
  std::string context = "";
  std::string segment_id = "";
  std::string tag = "";
  [[nodiscard]] static CallReceiveEvent from_payload(const json& payload) {
    RelayEvent base = RelayEvent::from_payload(payload);
    CallReceiveEvent e;
    e.event_type = base.event_type;
    e.params = base.params;
    e.call_id = base.call_id;
    e.timestamp = base.timestamp;
    const json& p = base.params;
    e.call_state = p.value("call_state", std::string());
    e.direction = p.value("direction", std::string());
    e.device = p.value("device", json::object());
    e.node_id = p.value("node_id", std::string());
    e.project_id = p.value("project_id", std::string());
    e.context = p.value("context", std::string());
    e.segment_id = p.value("segment_id", std::string());
    e.tag = p.value("tag", std::string());
    return e;
  }
};

/// Typed event for the matching signalwire.event type.
struct PlayEvent : public RelayEvent {
  std::string control_id = "";
  std::string state = "";
  [[nodiscard]] static PlayEvent from_payload(const json& payload) {
    RelayEvent base = RelayEvent::from_payload(payload);
    PlayEvent e;
    e.event_type = base.event_type;
    e.params = base.params;
    e.call_id = base.call_id;
    e.timestamp = base.timestamp;
    const json& p = base.params;
    e.control_id = p.value("control_id", std::string());
    e.state = p.value("state", std::string());
    return e;
  }
};

/// Typed event for the matching signalwire.event type.
struct RecordEvent : public RelayEvent {
  std::string control_id = "";
  std::string state = "";
  [[nodiscard]] static RecordEvent from_payload(const json& payload) {
    RelayEvent base = RelayEvent::from_payload(payload);
    RecordEvent e;
    e.event_type = base.event_type;
    e.params = base.params;
    e.call_id = base.call_id;
    e.timestamp = base.timestamp;
    const json& p = base.params;
    e.control_id = p.value("control_id", std::string());
    e.state = p.value("state", std::string());
    return e;
  }
};

/// Typed event for the matching signalwire.event type.
struct CollectEvent : public RelayEvent {
  std::string control_id = "";
  std::string state = "";
  std::string result = "";
  bool final = false;
  [[nodiscard]] static CollectEvent from_payload(const json& payload) {
    RelayEvent base = RelayEvent::from_payload(payload);
    CollectEvent e;
    e.event_type = base.event_type;
    e.params = base.params;
    e.call_id = base.call_id;
    e.timestamp = base.timestamp;
    const json& p = base.params;
    e.control_id = p.value("control_id", std::string());
    e.state = p.value("state", std::string());
    e.result = p.value("result", std::string());
    e.final = p.value("final", false);
    return e;
  }
};

/// Typed event for the matching signalwire.event type.
struct ConnectEvent : public RelayEvent {
  std::string connect_state = "";
  json peer = json::object();
  [[nodiscard]] static ConnectEvent from_payload(const json& payload) {
    RelayEvent base = RelayEvent::from_payload(payload);
    ConnectEvent e;
    e.event_type = base.event_type;
    e.params = base.params;
    e.call_id = base.call_id;
    e.timestamp = base.timestamp;
    const json& p = base.params;
    e.connect_state = p.value("connect_state", std::string());
    e.peer = p.value("peer", json::object());
    return e;
  }
};

/// Typed event for the matching signalwire.event type.
struct DetectEvent : public RelayEvent {
  std::string control_id = "";
  json detect = json::object();
  [[nodiscard]] static DetectEvent from_payload(const json& payload) {
    RelayEvent base = RelayEvent::from_payload(payload);
    DetectEvent e;
    e.event_type = base.event_type;
    e.params = base.params;
    e.call_id = base.call_id;
    e.timestamp = base.timestamp;
    const json& p = base.params;
    e.control_id = p.value("control_id", std::string());
    e.detect = p.value("detect", json::object());
    return e;
  }
};

/// Typed event for the matching signalwire.event type.
struct FaxEvent : public RelayEvent {
  std::string control_id = "";
  json fax = json::object();
  [[nodiscard]] static FaxEvent from_payload(const json& payload) {
    RelayEvent base = RelayEvent::from_payload(payload);
    FaxEvent e;
    e.event_type = base.event_type;
    e.params = base.params;
    e.call_id = base.call_id;
    e.timestamp = base.timestamp;
    const json& p = base.params;
    e.control_id = p.value("control_id", std::string());
    e.fax = p.value("fax", json::object());
    return e;
  }
};

/// Typed event for the matching signalwire.event type.
struct TapEvent : public RelayEvent {
  std::string control_id = "";
  std::string state = "";
  json tap = json::object();
  json device = json::object();
  [[nodiscard]] static TapEvent from_payload(const json& payload) {
    RelayEvent base = RelayEvent::from_payload(payload);
    TapEvent e;
    e.event_type = base.event_type;
    e.params = base.params;
    e.call_id = base.call_id;
    e.timestamp = base.timestamp;
    const json& p = base.params;
    e.control_id = p.value("control_id", std::string());
    e.state = p.value("state", std::string());
    e.tap = p.value("tap", json::object());
    e.device = p.value("device", json::object());
    return e;
  }
};

/// Typed event for the matching signalwire.event type.
struct StreamEvent : public RelayEvent {
  std::string control_id = "";
  std::string state = "";
  std::string url = "";
  std::string name = "";
  [[nodiscard]] static StreamEvent from_payload(const json& payload) {
    RelayEvent base = RelayEvent::from_payload(payload);
    StreamEvent e;
    e.event_type = base.event_type;
    e.params = base.params;
    e.call_id = base.call_id;
    e.timestamp = base.timestamp;
    const json& p = base.params;
    e.control_id = p.value("control_id", std::string());
    e.state = p.value("state", std::string());
    e.url = p.value("url", std::string());
    e.name = p.value("name", std::string());
    return e;
  }
};

/// Typed event for the matching signalwire.event type.
struct SendDigitsEvent : public RelayEvent {
  std::string control_id = "";
  std::string state = "";
  [[nodiscard]] static SendDigitsEvent from_payload(const json& payload) {
    RelayEvent base = RelayEvent::from_payload(payload);
    SendDigitsEvent e;
    e.event_type = base.event_type;
    e.params = base.params;
    e.call_id = base.call_id;
    e.timestamp = base.timestamp;
    const json& p = base.params;
    e.control_id = p.value("control_id", std::string());
    e.state = p.value("state", std::string());
    return e;
  }
};

/// Typed event for the matching signalwire.event type.
struct DialEvent : public RelayEvent {
  std::string tag = "";
  std::string dial_state = "";
  json call = json::object();
  [[nodiscard]] static DialEvent from_payload(const json& payload) {
    RelayEvent base = RelayEvent::from_payload(payload);
    DialEvent e;
    e.event_type = base.event_type;
    e.params = base.params;
    e.call_id = base.call_id;
    e.timestamp = base.timestamp;
    const json& p = base.params;
    e.tag = p.value("tag", std::string());
    e.dial_state = p.value("dial_state", std::string());
    e.call = p.value("call", json::object());
    return e;
  }
};

/// Typed event for the matching signalwire.event type.
struct ReferEvent : public RelayEvent {
  std::string state = "";
  std::string sip_refer_to = "";
  long sip_refer_response_code = 0;
  long sip_notify_response_code = 0;
  [[nodiscard]] static ReferEvent from_payload(const json& payload) {
    RelayEvent base = RelayEvent::from_payload(payload);
    ReferEvent e;
    e.event_type = base.event_type;
    e.params = base.params;
    e.call_id = base.call_id;
    e.timestamp = base.timestamp;
    const json& p = base.params;
    e.state = p.value("state", std::string());
    e.sip_refer_to = p.value("sip_refer_to", std::string());
    e.sip_refer_response_code = p.value<long>("sip_refer_response_code", 0);
    e.sip_notify_response_code = p.value<long>("sip_notify_response_code", 0);
    return e;
  }
};

/// Typed event for the matching signalwire.event type.
struct DenoiseEvent : public RelayEvent {
  bool denoised = false;
  [[nodiscard]] static DenoiseEvent from_payload(const json& payload) {
    RelayEvent base = RelayEvent::from_payload(payload);
    DenoiseEvent e;
    e.event_type = base.event_type;
    e.params = base.params;
    e.call_id = base.call_id;
    e.timestamp = base.timestamp;
    const json& p = base.params;
    e.denoised = p.value("denoised", false);
    return e;
  }
};

/// Typed event for the matching signalwire.event type.
struct PayEvent : public RelayEvent {
  std::string control_id = "";
  std::string state = "";
  [[nodiscard]] static PayEvent from_payload(const json& payload) {
    RelayEvent base = RelayEvent::from_payload(payload);
    PayEvent e;
    e.event_type = base.event_type;
    e.params = base.params;
    e.call_id = base.call_id;
    e.timestamp = base.timestamp;
    const json& p = base.params;
    e.control_id = p.value("control_id", std::string());
    e.state = p.value("state", std::string());
    return e;
  }
};

/// Typed event for the matching signalwire.event type.
struct QueueEvent : public RelayEvent {
  std::string control_id = "";
  std::string status = "";
  std::string queue_id = "";
  std::string queue_name = "";
  long position = 0;
  long size = 0;
  [[nodiscard]] static QueueEvent from_payload(const json& payload) {
    RelayEvent base = RelayEvent::from_payload(payload);
    QueueEvent e;
    e.event_type = base.event_type;
    e.params = base.params;
    e.call_id = base.call_id;
    e.timestamp = base.timestamp;
    const json& p = base.params;
    e.control_id = p.value("control_id", std::string());
    e.status = p.value("status", std::string());
    e.queue_id = p.value("id", std::string());
    e.queue_name = p.value("name", std::string());
    e.position = p.value<long>("position", 0);
    e.size = p.value<long>("size", 0);
    return e;
  }
};

/// Typed event for the matching signalwire.event type.
struct EchoEvent : public RelayEvent {
  std::string state = "";
  [[nodiscard]] static EchoEvent from_payload(const json& payload) {
    RelayEvent base = RelayEvent::from_payload(payload);
    EchoEvent e;
    e.event_type = base.event_type;
    e.params = base.params;
    e.call_id = base.call_id;
    e.timestamp = base.timestamp;
    const json& p = base.params;
    e.state = p.value("state", std::string());
    return e;
  }
};

/// Typed event for the matching signalwire.event type.
struct TranscribeEvent : public RelayEvent {
  std::string control_id = "";
  std::string state = "";
  std::string url = "";
  std::string recording_id = "";
  long duration = 0;
  long size = 0;
  [[nodiscard]] static TranscribeEvent from_payload(const json& payload) {
    RelayEvent base = RelayEvent::from_payload(payload);
    TranscribeEvent e;
    e.event_type = base.event_type;
    e.params = base.params;
    e.call_id = base.call_id;
    e.timestamp = base.timestamp;
    const json& p = base.params;
    e.control_id = p.value("control_id", std::string());
    e.state = p.value("state", std::string());
    e.url = p.value("url", std::string());
    e.recording_id = p.value("recording_id", std::string());
    e.duration = p.value<long>("duration", 0);
    e.size = p.value<long>("size", 0);
    return e;
  }
};

/// Typed event for the matching signalwire.event type.
struct HoldEvent : public RelayEvent {
  std::string state = "";
  [[nodiscard]] static HoldEvent from_payload(const json& payload) {
    RelayEvent base = RelayEvent::from_payload(payload);
    HoldEvent e;
    e.event_type = base.event_type;
    e.params = base.params;
    e.call_id = base.call_id;
    e.timestamp = base.timestamp;
    const json& p = base.params;
    e.state = p.value("state", std::string());
    return e;
  }
};

/// Typed event for the matching signalwire.event type.
struct ConferenceEvent : public RelayEvent {
  std::string conference_id = "";
  std::string name = "";
  std::string status = "";
  [[nodiscard]] static ConferenceEvent from_payload(const json& payload) {
    RelayEvent base = RelayEvent::from_payload(payload);
    ConferenceEvent e;
    e.event_type = base.event_type;
    e.params = base.params;
    e.call_id = base.call_id;
    e.timestamp = base.timestamp;
    const json& p = base.params;
    e.conference_id = p.value("conference_id", std::string());
    e.name = p.value("name", std::string());
    e.status = p.value("status", std::string());
    return e;
  }
};

/// Typed event for the matching signalwire.event type.
struct CallingErrorEvent : public RelayEvent {
  long code = 0;
  std::string message = "";
  [[nodiscard]] static CallingErrorEvent from_payload(const json& payload) {
    RelayEvent base = RelayEvent::from_payload(payload);
    CallingErrorEvent e;
    e.event_type = base.event_type;
    e.params = base.params;
    e.call_id = base.call_id;
    e.timestamp = base.timestamp;
    const json& p = base.params;
    e.code = p.value<long>("code", 0);
    e.message = p.value("message", std::string());
    return e;
  }
};

/// Typed event for the matching signalwire.event type.
struct MessageReceiveEvent : public RelayEvent {
  std::string message_id = "";
  std::string context = "";
  std::string direction = "";
  std::string from_number = "";
  std::string to_number = "";
  std::string body = "";
  json media = json::object();
  json segments = json::object();
  std::string message_state = "";
  json tags = json::object();
  [[nodiscard]] static MessageReceiveEvent from_payload(const json& payload) {
    RelayEvent base = RelayEvent::from_payload(payload);
    MessageReceiveEvent e;
    e.event_type = base.event_type;
    e.params = base.params;
    e.call_id = base.call_id;
    e.timestamp = base.timestamp;
    const json& p = base.params;
    e.message_id = p.value("message_id", std::string());
    e.context = p.value("context", std::string());
    e.direction = p.value("direction", std::string());
    e.from_number = p.value("from_number", std::string());
    e.to_number = p.value("to_number", std::string());
    e.body = p.value("body", std::string());
    e.media = p.value("media", json::object());
    e.segments = p.value("segments", json::object());
    e.message_state = p.value("message_state", std::string());
    e.tags = p.value("tags", json::object());
    return e;
  }
};

/// Typed event for the matching signalwire.event type.
struct MessageStateEvent : public RelayEvent {
  std::string message_id = "";
  std::string context = "";
  std::string direction = "";
  std::string from_number = "";
  std::string to_number = "";
  std::string body = "";
  json media = json::object();
  json segments = json::object();
  std::string message_state = "";
  std::string reason = "";
  json tags = json::object();
  [[nodiscard]] static MessageStateEvent from_payload(const json& payload) {
    RelayEvent base = RelayEvent::from_payload(payload);
    MessageStateEvent e;
    e.event_type = base.event_type;
    e.params = base.params;
    e.call_id = base.call_id;
    e.timestamp = base.timestamp;
    const json& p = base.params;
    e.message_id = p.value("message_id", std::string());
    e.context = p.value("context", std::string());
    e.direction = p.value("direction", std::string());
    e.from_number = p.value("from_number", std::string());
    e.to_number = p.value("to_number", std::string());
    e.body = p.value("body", std::string());
    e.media = p.value("media", json::object());
    e.segments = p.value("segments", json::object());
    e.message_state = p.value("message_state", std::string());
    e.reason = p.value("reason", std::string());
    e.tags = p.value("tags", json::object());
    return e;
  }
};

/// Parse a raw signalwire.event payload into a typed event object. Unknown
/// event types fall back to the base RelayEvent (never throws).
[[nodiscard]] inline RelayEvent parse_event(const json& payload) {
  const std::string et = payload.value("event_type", std::string());
  if (et == "calling.call.state") {
    return CallStateEvent::from_payload(payload);
  }
  if (et == "calling.call.receive") {
    return CallReceiveEvent::from_payload(payload);
  }
  if (et == "calling.call.play") {
    return PlayEvent::from_payload(payload);
  }
  if (et == "calling.call.record") {
    return RecordEvent::from_payload(payload);
  }
  if (et == "calling.call.collect") {
    return CollectEvent::from_payload(payload);
  }
  if (et == "calling.call.connect") {
    return ConnectEvent::from_payload(payload);
  }
  if (et == "calling.call.detect") {
    return DetectEvent::from_payload(payload);
  }
  if (et == "calling.call.fax") {
    return FaxEvent::from_payload(payload);
  }
  if (et == "calling.call.tap") {
    return TapEvent::from_payload(payload);
  }
  if (et == "calling.call.stream") {
    return StreamEvent::from_payload(payload);
  }
  if (et == "calling.call.send_digits") {
    return SendDigitsEvent::from_payload(payload);
  }
  if (et == "calling.call.dial") {
    return DialEvent::from_payload(payload);
  }
  if (et == "calling.call.refer") {
    return ReferEvent::from_payload(payload);
  }
  if (et == "calling.call.denoise") {
    return DenoiseEvent::from_payload(payload);
  }
  if (et == "calling.call.pay") {
    return PayEvent::from_payload(payload);
  }
  if (et == "calling.call.queue") {
    return QueueEvent::from_payload(payload);
  }
  if (et == "calling.call.echo") {
    return EchoEvent::from_payload(payload);
  }
  if (et == "calling.call.transcribe") {
    return TranscribeEvent::from_payload(payload);
  }
  if (et == "calling.call.hold") {
    return HoldEvent::from_payload(payload);
  }
  if (et == "calling.conference") {
    return ConferenceEvent::from_payload(payload);
  }
  if (et == "calling.error") {
    return CallingErrorEvent::from_payload(payload);
  }
  if (et == "messaging.receive") {
    return MessageReceiveEvent::from_payload(payload);
  }
  if (et == "messaging.state") {
    return MessageStateEvent::from_payload(payload);
  }
  return RelayEvent::from_payload(payload);
}

}  // namespace events
}  // namespace relay
}  // namespace signalwire
