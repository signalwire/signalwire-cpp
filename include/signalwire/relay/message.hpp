// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#pragma once

#include <string>
#include <vector>
#include <optional>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <memory>
#include <nlohmann/json.hpp>

namespace signalwire {
namespace relay {

using json = nlohmann::json;

/// Represents an SMS/MMS message tracked through delivery states.
/// States: "queued", "initiated", "sent", "delivered", "undelivered", "failed"
/// Uses shared internal state so the object can be copied/returned by value.
struct Message {
    using CompletedCallback = std::function<void(const Message&)>;

    std::string message_id;
    std::string state;
    std::string from;
    std::string to;
    std::string body;
    std::vector<std::string> media;
    std::vector<std::string> tags;
    std::string direction;
    std::string region;

    Message();

    /// Parse from a RELAY event params object
    static Message from_params(const json& params);

    bool is_delivered() const { return state == "delivered"; }
    bool is_failed() const { return state == "failed" || state == "undelivered"; }
    bool is_terminal() const { return is_delivered() || is_failed(); }

    /// Update state from a messaging.state event
    void update_state(const std::string& new_state);

    /// Block until message reaches a terminal state
    bool wait(int timeout_ms = 0);

    /// Set callback for when message reaches terminal state
    void on_completed(CompletedCallback cb);

private:
    struct SyncState {
        std::mutex mutex;
        std::condition_variable cv;
        bool completed = false;
        CompletedCallback callback;
    };
    std::shared_ptr<SyncState> sync_;
};

} // namespace relay
} // namespace signalwire
