// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#pragma once

#include <string>
#include <vector>
#include <optional>
#include <functional>
#include <nlohmann/json.hpp>
#include "signalwire/relay/action.hpp"
#include "signalwire/relay/relay_event.hpp"
#include "signalwire/relay/constants.hpp"

namespace signalwire {
namespace relay {

using json = nlohmann::json;

/// Callback for call events
using CallEventHandler = std::function<void(const CallEvent&)>;

/// Represents a live call with methods for call control.
///
/// NOTE: This is a stub. All methods that send commands to the server
/// require the WebSocket transport layer (not yet implemented).
/// The API surface matches the Python SDK's Call object.
class Call {
public:
    Call() = default;
    Call(const std::string& call_id, const std::string& node_id)
        : call_id_(call_id), node_id_(node_id), state_(CALL_STATE_CREATED) {}

    // ========================================================================
    // Identity
    // ========================================================================

    const std::string& call_id() const { return call_id_; }
    const std::string& node_id() const { return node_id_; }
    const std::string& state() const { return state_; }
    const std::string& direction() const { return direction_; }
    const std::string& from() const { return from_; }
    const std::string& to() const { return to_; }

    bool is_answered() const { return state_ == CALL_STATE_ANSWERED; }
    bool is_ended() const { return state_ == CALL_STATE_ENDED; }

    // ========================================================================
    // Call control (stubs)
    // ========================================================================

    /// Answer an inbound call. STUB.
    Action answer() { return Action("answer"); }

    /// Hang up the call. STUB.
    Action hangup(const std::string& reason = "hangup") {
        (void)reason;
        return Action("hangup");
    }

    /// Play media items. STUB.
    /// @param media  Array of media objects, e.g. [{"type":"tts","params":{"text":"Hi"}}]
    Action play(const json& media, double volume = 0.0) {
        (void)media; (void)volume;
        return Action("play");
    }

    /// Record the call. STUB.
    Action record(const json& params = json::object()) {
        (void)params;
        return Action("record");
    }

    /// Record the call audio. STUB.
    Action record_call(const json& params = json::object()) {
        (void)params;
        return Action("record_call");
    }

    /// Prompt for input (play + collect). STUB.
    Action prompt(const json& play_media, const json& collect_params) {
        (void)play_media; (void)collect_params;
        return Action("prompt");
    }

    /// Collect user input (DTMF/speech). STUB.
    Action collect(const json& params) {
        (void)params;
        return Action("collect");
    }

    /// Connect to another party. STUB.
    Action connect(const json& devices) {
        (void)devices;
        return Action("connect");
    }

    /// Disconnect a connected party. STUB.
    Action disconnect() { return Action("disconnect"); }

    /// Detect (AMD, fax, digit). STUB.
    Action detect(const json& params) {
        (void)params;
        return Action("detect");
    }

    /// Tap the call audio. STUB.
    Action tap_audio(const json& params) {
        (void)params;
        return Action("tap");
    }

    /// Stop tapping. STUB.
    Action stop_tap(const std::string& control_id) {
        (void)control_id;
        return Action("stop_tap");
    }

    /// Send DTMF digits. STUB.
    Action send_digits(const std::string& digits) {
        (void)digits;
        return Action("send_digits");
    }

    /// Transfer the call. STUB.
    Action transfer(const json& params) {
        (void)params;
        return Action("transfer");
    }

    /// Start live transcription. STUB.
    Action live_transcribe(const json& params = json::object()) {
        (void)params;
        return Action("live_transcribe");
    }

    /// Start live translation. STUB.
    Action live_translate(const json& params = json::object()) {
        (void)params;
        return Action("live_translate");
    }

    /// Start AI processing. STUB.
    Action ai(const json& params) {
        (void)params;
        return Action("ai");
    }

    /// Send a fax. STUB.
    Action send_fax(const std::string& document_url, const std::string& header = "") {
        (void)document_url; (void)header;
        return Action("send_fax");
    }

    /// Receive a fax. STUB.
    Action receive_fax() { return Action("receive_fax"); }

    /// Hold the call. STUB.
    Action hold() { return Action("hold"); }

    /// Unhold the call. STUB.
    Action unhold() { return Action("unhold"); }

    /// SIP refer. STUB.
    Action sip_refer(const std::string& to_uri) {
        (void)to_uri;
        return Action("sip_refer");
    }

    /// Join a conference. STUB.
    Action join_conference(const std::string& name, const json& params = json::object()) {
        (void)name; (void)params;
        return Action("join_conference");
    }

    /// Join a video room. STUB.
    Action join_room(const std::string& name) {
        (void)name;
        return Action("join_room");
    }

    /// Execute SWML on this call. STUB.
    Action execute_swml(const json& swml) {
        (void)swml;
        return Action("execute_swml");
    }

    // ========================================================================
    // Event handling
    // ========================================================================

    /// Register a handler for call events
    void on_event(CallEventHandler handler) {
        event_handlers_.push_back(std::move(handler));
    }

    /// Wait for the call to end. STUB.
    bool wait_for_ended(int timeout_ms = 0) {
        (void)timeout_ms;
        return true;
    }

    // ========================================================================
    // State updates (called internally)
    // ========================================================================

    void update_state(const std::string& state) { state_ = state; }
    void set_direction(const std::string& dir) { direction_ = dir; }
    void set_from(const std::string& f) { from_ = f; }
    void set_to(const std::string& t) { to_ = t; }

    void dispatch_event(const CallEvent& ev) {
        for (auto& h : event_handlers_) {
            h(ev);
        }
    }

private:
    std::string call_id_;
    std::string node_id_;
    std::string state_;
    std::string direction_;
    std::string from_;
    std::string to_;
    std::vector<CallEventHandler> event_handlers_;
};

} // namespace relay
} // namespace signalwire
