// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#pragma once

#include <string>
#include <optional>
#include <chrono>
#include <nlohmann/json.hpp>

namespace signalwire {
namespace relay {

using json = nlohmann::json;

/// Base class for all RELAY events parsed from signalwire.event JSON-RPC messages.
struct RelayEvent {
    std::string event_type;
    json params;
    std::string event_channel;
    std::chrono::system_clock::time_point timestamp =
        std::chrono::system_clock::now();

    RelayEvent() = default;
    explicit RelayEvent(const std::string& type) : event_type(type) {}

    /// Parse from a signalwire.event params JSON.
    /// The outer params contains event_type and the inner params with event-specific data.
    static RelayEvent from_json(const json& j) {
        RelayEvent ev;
        ev.event_type = j.value("event_type", "");
        ev.params = j.value("params", json::object());
        ev.event_channel = j.value("event_channel", "");
        return ev;
    }
};

/// Call-specific event parsed from calling.call.state and other call events.
struct CallEvent : public RelayEvent {
    std::string call_id;
    std::string node_id;
    std::string call_state;
    std::optional<std::string> peer_call_id;
    std::string tag;

    static CallEvent from_relay_event(const RelayEvent& ev) {
        CallEvent ce;
        ce.event_type = ev.event_type;
        ce.params = ev.params;
        ce.event_channel = ev.event_channel;
        ce.timestamp = ev.timestamp;
        ce.call_id = ev.params.value("call_id", "");
        ce.node_id = ev.params.value("node_id", "");
        ce.call_state = ev.params.value("call_state", "");
        ce.tag = ev.params.value("tag", "");
        if (ev.params.contains("peer")) {
            auto& peer = ev.params["peer"];
            if (peer.contains("call_id")) {
                ce.peer_call_id = peer["call_id"].get<std::string>();
            }
        }
        return ce;
    }
};

/// Play/Record/Collect component event with control_id for action routing.
struct ComponentEvent : public RelayEvent {
    std::string call_id;
    std::string control_id;
    std::string state;

    static ComponentEvent from_relay_event(const RelayEvent& ev) {
        ComponentEvent ce;
        ce.event_type = ev.event_type;
        ce.params = ev.params;
        ce.event_channel = ev.event_channel;
        ce.timestamp = ev.timestamp;
        ce.call_id = ev.params.value("call_id", "");
        ce.control_id = ev.params.value("control_id", "");
        ce.state = ev.params.value("state", "");
        return ce;
    }
};

/// Messaging event for SMS/MMS state changes and inbound messages.
struct MessageEvent : public RelayEvent {
    std::string message_id;
    std::string message_state;
    std::string from;
    std::string to;
    std::string body;

    static MessageEvent from_relay_event(const RelayEvent& ev) {
        MessageEvent me;
        me.event_type = ev.event_type;
        me.params = ev.params;
        me.event_channel = ev.event_channel;
        me.timestamp = ev.timestamp;
        me.message_id = ev.params.value("message_id", "");
        me.message_state = ev.params.value("message_state", "");
        me.from = ev.params.value("from_number", "");
        me.to = ev.params.value("to_number", "");
        me.body = ev.params.value("body", "");
        return me;
    }
};

/// Dial-specific event with nested call info and tag-based correlation.
struct DialEvent : public RelayEvent {
    std::string tag;
    std::string dial_state;
    json call_info;

    static DialEvent from_relay_event(const RelayEvent& ev) {
        DialEvent de;
        de.event_type = ev.event_type;
        de.params = ev.params;
        de.event_channel = ev.event_channel;
        de.timestamp = ev.timestamp;
        de.tag = ev.params.value("tag", "");
        de.dial_state = ev.params.value("dial_state", "");
        de.call_info = ev.params.value("call", json::object());
        return de;
    }
};

} // namespace relay
} // namespace signalwire
