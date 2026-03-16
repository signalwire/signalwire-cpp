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

/// Base class for all RELAY events
struct RelayEvent {
    std::string event_type;
    json params;
    std::string event_channel;
    std::chrono::system_clock::time_point timestamp =
        std::chrono::system_clock::now();

    RelayEvent() = default;
    explicit RelayEvent(const std::string& type) : event_type(type) {}

    /// Parse from a Blade event JSON
    static RelayEvent from_json(const json& j) {
        RelayEvent ev;
        ev.event_type = j.value("event_type", "");
        ev.params = j.value("params", json::object());
        ev.event_channel = j.value("event_channel", "");
        return ev;
    }
};

/// Call-specific event
struct CallEvent : public RelayEvent {
    std::string call_id;
    std::string node_id;
    std::string call_state;
    std::optional<std::string> peer_call_id;

    static CallEvent from_relay_event(const RelayEvent& ev) {
        CallEvent ce;
        ce.event_type = ev.event_type;
        ce.params = ev.params;
        ce.event_channel = ev.event_channel;
        ce.timestamp = ev.timestamp;
        ce.call_id = ev.params.value("call_id", "");
        ce.node_id = ev.params.value("node_id", "");
        ce.call_state = ev.params.value("call_state", "");
        if (ev.params.contains("peer")) {
            auto& peer = ev.params["peer"];
            if (peer.contains("call_id")) {
                ce.peer_call_id = peer["call_id"].get<std::string>();
            }
        }
        return ce;
    }
};

/// Play/Record/Collect component event
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

/// Messaging event
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

} // namespace relay
} // namespace signalwire
