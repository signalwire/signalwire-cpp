// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#pragma once

#include <string>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <nlohmann/json.hpp>

namespace signalwire {
namespace relay {

using json = nlohmann::json;

class RelayClient;

/// Represents a controllable in-progress operation (play, record, collect, etc.)
/// Uses shared internal state so the object can be copied/moved freely while
/// maintaining a single underlying condition_variable for synchronization.
class Action {
public:
    using CompletedCallback = std::function<void(const Action&)>;

    Action();
    explicit Action(const std::string& control_id);
    Action(const std::string& control_id, RelayClient* client,
           const std::string& call_id, const std::string& node_id);

    const std::string& control_id() const { return state_->control_id; }
    const std::string& state() const;
    bool completed() const;
    const json& result() const;
    const std::string& call_id() const { return state_->call_id; }
    const std::string& node_id() const { return state_->node_id; }

    /// Block until the action completes or times out.
    bool wait(int timeout_ms = 0);

    /// Request the server to stop this action.
    void stop();

    /// Request the server to pause this action (play only).
    void pause();

    /// Request the server to resume this action (play only).
    void resume();

    /// Set a callback to fire when the action completes.
    void on_completed(CompletedCallback cb);

    /// Update internal state (called by Call/Client when events arrive).
    void update_state(const std::string& new_state, const json& result = json::object());

    /// Resolve the action immediately (used for call-gone scenarios).
    void resolve(const std::string& final_state = "finished", const json& result = json::object());

private:
    void send_control_command(const std::string& operation);

    struct SharedState {
        std::string control_id;
        std::string call_id;
        std::string node_id;
        RelayClient* client = nullptr;

        mutable std::mutex mutex;
        std::condition_variable cv;
        std::string current_state;
        bool is_completed = false;
        json result_data;
        CompletedCallback completed_callback;
    };

    std::shared_ptr<SharedState> state_;
};

} // namespace relay
} // namespace signalwire
