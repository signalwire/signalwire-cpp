// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#pragma once

#include <string>
#include <optional>
#include <functional>
#include <nlohmann/json.hpp>

namespace signalwire {
namespace relay {

using json = nlohmann::json;

/// Represents a controllable in-progress operation (play, record, collect, etc.)
///
/// NOTE: This is a stub. Full implementation requires the WebSocket transport
/// layer to send control commands and receive state updates.
class Action {
public:
    Action() = default;
    explicit Action(const std::string& control_id) : control_id_(control_id) {}

    const std::string& control_id() const { return control_id_; }
    const std::string& state() const { return state_; }
    bool completed() const { return completed_; }
    const json& result() const { return result_; }

    /// Block until the action completes or times out.
    /// @param timeout_ms  Maximum wait in milliseconds (0 = infinite).
    /// @return true if completed, false on timeout.
    /// STUB: Always returns true immediately.
    bool wait(int timeout_ms = 0) {
        (void)timeout_ms;
        // TODO: implement with condition_variable once WebSocket transport exists
        return true;
    }

    /// Request the server to stop this action.
    /// STUB: No-op until WebSocket transport is implemented.
    void stop() {
        // TODO: send control command via WebSocket
    }

    /// Request the server to pause this action (play only).
    /// STUB: No-op until WebSocket transport is implemented.
    void pause() {
        // TODO: send control command via WebSocket
    }

    /// Request the server to resume this action (play only).
    /// STUB: No-op until WebSocket transport is implemented.
    void resume() {
        // TODO: send control command via WebSocket
    }

    /// Update internal state (called by Call when events arrive)
    void update_state(const std::string& state, const json& result = json::object()) {
        state_ = state;
        result_ = result;
        if (state == "finished" || state == "error" || state == "no_input") {
            completed_ = true;
        }
    }

private:
    std::string control_id_;
    std::string state_;
    bool completed_ = false;
    json result_;
};

} // namespace relay
} // namespace signalwire
