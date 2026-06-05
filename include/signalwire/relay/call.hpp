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
#include "signalwire/relay/tts_gender.hpp"

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
    Action play(const json& media, double volume = 0.0,
                const std::string& control_id = "");
    // Typed play convenience wrappers (mirror Python's play_tts/play_audio/
    // play_silence/play_ringtone). Each builds the RELAY play-media shape and
    // delegates to play(). Optional knobs default to a sentinel that omits
    // them from the wire frame: empty string for text fields, 0.0 for volume,
    // a negative value for duration.
    Action play_tts(const std::string& text,
                    const std::string& language = "",
                    const std::string& gender = "",
                    const std::string& voice = "",
                    double volume = 0.0);
    // Typed-enum overload for the TTS gender closed set. Declared AFTER the
    // std::string overload (above) so the enumerator's equal-arity dedup keeps
    // the string version as the canonical signature; this overload just adds
    // call-site typo checking. It normalizes Gender -> its wire string and
    // delegates to the std::string play_tts, so the enum and the bare string
    // emit the IDENTICAL TTS frame. Idiomatic C++ addition (PORT_ADDITIONS.md).
    Action play_tts(const std::string& text,
                    const std::string& language,
                    Gender gender,
                    const std::string& voice = "",
                    double volume = 0.0);
    Action play_audio(const std::string& url, double volume = 0.0);
    Action play_silence(double duration);
    Action play_ringtone(const std::string& name,
                         double duration = -1.0,
                         double volume = 0.0);
    Action record(const json& params = json::object(),
                  const std::string& control_id = "");
    Action record_call(const json& params = json::object());
    Action prompt(const json& play_media, const json& collect_params,
                  const std::string& control_id = "");
    Action play_and_collect(const json& play_media, const json& collect_params,
                            const std::string& control_id = "");
    // Typed prompt convenience wrappers (mirror Python's prompt_tts/
    // prompt_audio). Each builds the RELAY play-media shape and delegates to
    // play_and_collect() with the caller-supplied collect descriptor.
    Action prompt_tts(const std::string& text, const json& collect,
                      const std::string& language = "",
                      const std::string& gender = "",
                      const std::string& voice = "",
                      double volume = 0.0);
    // Typed-enum overload for the TTS gender closed set — same rationale as
    // the play_tts(Gender) overload above. Declared after the std::string
    // prompt_tts so equal-arity dedup keeps the string signature; delegates
    // to it via tts_gender_value() so both emit the identical TTS frame.
    Action prompt_tts(const std::string& text, const json& collect,
                      const std::string& language,
                      Gender gender,
                      const std::string& voice = "",
                      double volume = 0.0);
    Action prompt_audio(const std::string& url, const json& collect,
                        double volume = 0.0);
    Action collect(const json& params,
                   const std::string& control_id = "");
    Action connect(const json& devices);
    Action disconnect();
    Action detect(const json& params,
                  const std::string& control_id = "");
    // Typed detect convenience wrappers (mirror Python's detect_digit/
    // detect_answering_machine/detect_fax). Each builds the RELAY detect
    // descriptor {"type":..., "params":{...only-provided...}} and delegates
    // to detect(). An optional top-level timeout (negative = omit) is folded
    // into the detect frame. detect_answering_machine takes a json of AMD
    // params so only the keys the caller supplies hit the wire, matching
    // Python's only-provided-keys behaviour.
    Action detect_digit(const std::string& digits = "", double timeout = -1.0);
    Action detect_answering_machine(const json& amd_params = json::object(),
                                    double timeout = -1.0);
    Action detect_fax(const std::string& tone = "", double timeout = -1.0);
    Action tap_audio(const json& params,
                     const std::string& control_id = "");
    Action tap(const json& params,
               const std::string& control_id = "");
    Action stop_tap(const std::string& control_id);
    Action send_digits(const std::string& digits);
    Action transfer(const json& params);
    Action live_transcribe(const json& params = json::object());
    Action transcribe(const json& params = json::object(),
                      const std::string& control_id = "");
    Action live_translate(const json& params = json::object());
    Action ai(const json& params,
              const std::string& control_id = "");
    Action pay(const json& params,
               const std::string& control_id = "");
    Action send_fax(const std::string& document_url, const std::string& header = "",
                    const std::string& identity = "",
                    const std::string& control_id = "");
    Action receive_fax(const std::string& control_id = "");
    Action stream(const json& params,
                  const std::string& control_id = "");
    Action hold();
    Action unhold();
    Action sip_refer(const std::string& to_uri);
    Action join_conference(const std::string& name, const json& params = json::object());
    Action join_room(const std::string& name);
    Action execute_swml(const json& swml);

    // Event handling
    void on_event(CallEventHandler handler);
    // [[nodiscard]]: the return value is the whole point of a wait — it tells
    // you whether the call actually reached the terminal state vs. timed out.
    // Silently dropping it (then acting as if the call ended) is a bug.
    [[nodiscard]] bool wait_for_ended(int timeout_ms = 0);

    // Typed state-wait convenience (mirror Python's wait_for_answered/
    // wait_for_ringing/wait_for_ending). Block until the call reaches the
    // named state, returning immediately if it is already at or past that
    // state in the lifecycle order created<ringing<answered<ending<ended.
    // Return true on reaching the state, false on timeout. timeout_ms<=0
    // waits indefinitely. (Python returns the state RelayEvent; the C++
    // idiom returns bool to match wait_for_ended — see
    // PORT_SIGNATURE_OMISSIONS.md cpp_wait_returns_bool.)
    // [[nodiscard]] for the same reason as wait_for_ended: ignoring the
    // reached-vs-timed-out result is always a bug.
    [[nodiscard]] bool wait_for_answered(int timeout_ms = 0);
    [[nodiscard]] bool wait_for_ringing(int timeout_ms = 0);
    [[nodiscard]] bool wait_for_ending(int timeout_ms = 0);

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
    // Block until the call's lifecycle rank reaches `target` (or timeout).
    // Short-circuits true when already at/past the target. Backs the
    // wait_for_answered/ringing/ending convenience methods.
    bool wait_for_state(const std::string& target, int timeout_ms);
    // Issue a single play_and_collect frame (with optional volume) and apply
    // collect-only resolution. Backs prompt_tts/prompt_audio.
    Action prompt_with_media(const json& media, const json& collect,
                             double volume);

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
        // Notified on EVERY state transition (not just ended) so the
        // wait_for_answered/ringing/ending helpers can wake on intermediate
        // states. Guarded by ended_mutex (which already serialises `state`).
        std::condition_variable state_cv;
    };

    std::shared_ptr<SharedState> s_;
};

} // namespace relay
} // namespace signalwire
