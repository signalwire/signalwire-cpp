// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <nlohmann/json.hpp>
#include "signalwire/relay/action.hpp"
#include "signalwire/relay/relay_event.hpp"
#include "signalwire/relay/constants.hpp"

namespace signalwire {
namespace relay {

using json = nlohmann::json;

class RelayClient;

/// Callback for call events
using CallEventHandler = std::function<void(const CallEvent&)>;

/// Represents a live call with methods for call control.
/// Uses shared internal state so the object can be copied/returned by value.
/// All command methods send JSON-RPC requests through the RelayClient.
class Call {
public:
    Call();
    Call(const std::string& call_id, const std::string& node_id);
    Call(const std::string& call_id, const std::string& node_id, RelayClient* client);

    // Identity
    const std::string& call_id() const { return s_->call_id; }
    const std::string& node_id() const { return s_->node_id; }
    const std::string& state() const { return s_->state; }
    const std::string& direction() const { return s_->direction; }
    const std::string& from() const { return s_->from; }
    const std::string& to() const { return s_->to; }
    const std::string& tag() const { return s_->tag; }

    bool is_answered() const { return s_->state == CALL_STATE_ANSWERED; }
    bool is_ended() const { return s_->state == CALL_STATE_ENDED; }

    // Call control methods
    Action answer();
    Action hangup(const std::string& reason = "hangup");
    Action play(const json& media, double volume = 0.0);
    Action record(const json& params = json::object());
    Action record_call(const json& params = json::object());
    Action prompt(const json& play_media, const json& collect_params);
    Action collect(const json& params);
    Action connect(const json& devices);
    Action disconnect();
    Action detect(const json& params);
    Action tap_audio(const json& params);
    Action stop_tap(const std::string& control_id);
    Action send_digits(const std::string& digits);
    Action transfer(const json& params);
    Action live_transcribe(const json& params = json::object());
    Action live_translate(const json& params = json::object());
    Action ai(const json& params);
    Action send_fax(const std::string& document_url, const std::string& header = "");
    Action receive_fax();
    Action hold();
    Action unhold();
    Action sip_refer(const std::string& to_uri);
    Action join_conference(const std::string& name, const json& params = json::object());
    Action join_room(const std::string& name);
    Action execute_swml(const json& swml);

    // Event handling
    void on_event(CallEventHandler handler);
    bool wait_for_ended(int timeout_ms = 0);

    // State updates (called internally by the client)
    void update_state(const std::string& new_state);
    void set_direction(const std::string& dir) { s_->direction = dir; }
    void set_from(const std::string& f) { s_->from = f; }
    void set_to(const std::string& t) { s_->to = t; }
    void set_tag(const std::string& t) { s_->tag = t; }
    void set_client(RelayClient* c) { s_->client = c; }

    void dispatch_event(const CallEvent& ev);

    // Action tracking by control_id
    void register_action(const std::string& control_id, Action* action);
    void unregister_action(const std::string& control_id);
    Action* find_action(const std::string& control_id);

    // Resolve all pending actions (used when call ends)
    void resolve_all_actions(const std::string& final_state = "finished");

private:
    static std::string generate_uuid();
    json base_params() const;
    Action execute_simple(const std::string& method, const json& extra_params = json::object());
    Action execute_action(const std::string& method, const json& extra_params = json::object());

    struct SharedState {
        std::string call_id;
        std::string node_id;
        std::string state;
        std::string direction;
        std::string from;
        std::string to;
        std::string tag;
        RelayClient* client = nullptr;

        std::vector<CallEventHandler> event_handlers;
        std::unordered_map<std::string, Action*> actions;
        std::mutex actions_mutex;
        std::mutex ended_mutex;
        std::condition_variable ended_cv;
    };

    std::shared_ptr<SharedState> s_;
};

} // namespace relay
} // namespace signalwire
