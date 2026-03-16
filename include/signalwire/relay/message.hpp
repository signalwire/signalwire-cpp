// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#pragma once

#include <string>
#include <vector>
#include <optional>
#include <nlohmann/json.hpp>

namespace signalwire {
namespace relay {

using json = nlohmann::json;

/// Represents an SMS/MMS message tracked through delivery states.
///
/// NOTE: This is a stub. Full implementation requires the WebSocket
/// transport layer for state tracking and event delivery.
struct Message {
    std::string message_id;
    std::string state;    // "queued", "initiated", "sent", "delivered", "undelivered", "failed"
    std::string from;
    std::string to;
    std::string body;
    std::vector<std::string> media;  // MMS media URLs
    std::vector<std::string> tags;
    std::string direction;  // "inbound" or "outbound"
    std::string region;

    Message() = default;

    /// Parse from a RELAY event params object
    static Message from_params(const json& params) {
        Message msg;
        msg.message_id = params.value("message_id", "");
        msg.state = params.value("message_state", params.value("state", ""));
        msg.from = params.value("from_number", "");
        msg.to = params.value("to_number", "");
        msg.body = params.value("body", "");
        msg.direction = params.value("direction", "");
        msg.region = params.value("region", "");
        if (params.contains("media") && params["media"].is_array()) {
            for (const auto& m : params["media"]) {
                msg.media.push_back(m.get<std::string>());
            }
        }
        if (params.contains("tags") && params["tags"].is_array()) {
            for (const auto& t : params["tags"]) {
                msg.tags.push_back(t.get<std::string>());
            }
        }
        return msg;
    }

    bool is_delivered() const { return state == "delivered"; }
    bool is_failed() const { return state == "failed" || state == "undelivered"; }
};

} // namespace relay
} // namespace signalwire
