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
#include "signalwire/relay/states.hpp"

namespace signalwire {
namespace relay {

using json = nlohmann::json;

/// Represents an SMS/MMS message tracked through delivery states.
/// States: "queued", "initiated", "sent", "delivered", "undelivered", "failed"
/// Uses shared internal state so the object can be copied/returned by value
/// — copies of a Message observe the same state updates as the underlying
/// instance the registry tracks.
struct Message {
    using CompletedCallback = std::function<void(const Message&)>;

    Message();
    Message(const Message&) = default;
    Message& operator=(const Message&) = default;
    ~Message() = default;

    /// Parse from a RELAY event params object
    [[nodiscard]] static Message from_params(const json& params);

    // Identity / outbound metadata. These are write-once-by-construction so
    // sharing across copies isn't required for these fields.
    std::string message_id;
    std::string from;
    std::string to;
    std::string body;
    std::vector<std::string> media;
    std::vector<std::string> tags;
    std::string direction;
    std::string region;

    // Mutable fields — accessor pattern so returned-by-value Message
    // copies see updates the registry-owned instance applies.
    const std::string& state() const;
    // Typed delivery-state accessor (Tier-3 idiom) ALONGSIDE the bare-string
    // state() above. Parses the same backing string into a MessageState enum;
    // std::nullopt if the server emitted an unknown value (the set can grow —
    // see states.hpp). [[nodiscard]]: the returned enum is the point.
    [[nodiscard]] std::optional<MessageState> message_state() const {
        return message_state_from_string(state());
    }
    const std::string& reason() const;
    void set_state(const std::string& s);
    void set_reason(const std::string& r);

    [[nodiscard]] bool is_delivered() const { return state() == "delivered"; }
    [[nodiscard]] bool is_failed() const {
        const std::string& s = state();
        return s == "failed" || s == "undelivered";
    }
    [[nodiscard]] bool is_terminal() const { return is_delivered() || is_failed(); }

    /// Update state from a messaging.state event. Notifies waiters /
    /// callbacks when the state is terminal.
    void update_state(const std::string& new_state);

    /// Block until message reaches a terminal state. Returns true if
    /// terminal, false on timeout.
    /// [[nodiscard]]: the delivered-vs-timed-out result is the reason you
    /// called wait() — dropping it is a bug.
    [[nodiscard]] bool wait(int timeout_ms = 0);

    /// Set callback for when message reaches terminal state. If the
    /// message is already terminal the callback fires immediately.
    void on_completed(CompletedCallback cb);

private:
    struct SyncState {
        std::mutex mutex;
        std::condition_variable cv;
        bool completed = false;
        CompletedCallback callback;
        std::string state;
        std::string reason;
    };
    std::shared_ptr<SyncState> sync_;
};

} // namespace relay
} // namespace signalwire
