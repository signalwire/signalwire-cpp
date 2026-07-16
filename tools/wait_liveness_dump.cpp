// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
//
// wait_liveness_dump.cpp — the C++ port's WAIT-LIVENESS (§2.4) runner for the
// cross-port liveness differ (porting-sdk/scripts/diff_port_wait_liveness.py).
//
// Contract: for each wait_liveness_corpus case, connect a REAL RelayClient to a
// live mock_relay, start an Action-returning Call verb (play / record) with the
// corpus control_id, ARM the completing event as a DEFERRED push delivered
// delay_ms AFTER the RPC response (through the same socket-read -> event-dispatch
// path the real server drives), call Action::wait(), and record the liveness
// classification derived from three measured instants (t_wait_start,
// t_event_armed_for = delay_ms, t_return).
//
// The artifact is a CLASSIFICATION, not raw ms, so the golden is deterministic
// while the timing that produces it is real and unfakeable:
//   * a wait() that is a NO-OP returns at t~=0 -> blocked_until_event=false (RED)
//   * a wait() that HANGS blows the deadline    -> timed_out=true         (RED)
//   * a correct wait() blocks until the event   -> blocked_until_event=true (GREEN)
//
// cpp's Action::wait() is a real condition_variable wait (src/relay/action.cpp),
// so all three cases classify GREEN. Prints ONE JSON object {case-id -> artifact}
// to stdout; only stdout carries JSON (logs -> stderr / SIGNALWIRE_LOG_MODE=off).
//
// Uses the shared relay_mocktest harness (make_client / drive_inbound_call /
// arm_method) — the SAME helpers the relay mock tests use — so the mock lifecycle,
// session scoping, and deferred-event delivery match the tested behavior exactly.

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>

#include "relay_mocktest.hpp"
#include "signalwire/logging.hpp"
#include "signalwire/relay/action.hpp"
#include "signalwire/relay/call.hpp"
#include "signalwire/relay/client.hpp"

using json = nlohmann::json;
namespace mt = signalwire::relay::mocktest;
using signalwire::relay::Action;
using signalwire::relay::Call;
using signalwire::relay::RelayClient;
using Clock = std::chrono::steady_clock;

namespace {

// Mirror wait_liveness_corpus.py's constants + classification tolerance.
constexpr int kDelayMs = 150;      // when the completing event fires, after the RPC reply
constexpr int kDeadlineMs = 5000;  // a wait() outliving this is HUNG (timed_out)
constexpr double kBlockTolMs = 40.0;  // how much earlier than delay a return may be and still "blocked"

const char* kCallId = "call-xyz";
const char* kCid = "ctl-live-1";  // explicit control_id so the completing event targets it

json play_media() {
    return json::array({{{"type", "audio"}, {"params", {{"url", "https://x/a.mp3"}}}}});
}

// The deferred-completion arming: queue a post-RPC {state:finished} event for the
// verb's method, delivered kDelayMs after the RPC response. arm_method's FIFO
// scripted events drive the SAME read-loop -> dispatch path the real server uses,
// so a wait() that never pumps the receive loop never sees it.
void arm_finished(const std::string& method) {
    json events = json::array();
    events.push_back({{"emit", {{"state", "finished"}}}, {"delay_ms", kDelayMs}});
    mt::arm_method(method, events);
}

struct DriveResult {
    Clock::time_point t_wait_start;
    Clock::time_point t_return;
    std::string completed_state;
    bool timed_out;
};

// Start the action (already created via the verb), wait, and measure.
DriveResult drive(Action& action) {
    DriveResult r;
    r.t_wait_start = Clock::now();
    bool ok = action.wait(kDeadlineMs);
    r.t_return = Clock::now();
    r.timed_out = !ok && !action.is_done();
    r.completed_state = action.state();
    return r;
}

json classify(const DriveResult& r) {
    if (r.timed_out) {
        return {{"blocked_until_event", false},
                {"returned_after_event", false},
                {"completed_state", ""},
                {"timed_out", true}};
    }
    double elapsed_ms =
        std::chrono::duration<double, std::milli>(r.t_return - r.t_wait_start).count();
    bool blocked = elapsed_ms >= (static_cast<double>(kDelayMs) - kBlockTolMs);
    return {{"blocked_until_event", blocked},
            {"returned_after_event", true},
            {"completed_state", r.completed_state},
            {"timed_out", false}};
}

// Build an "answered inbound call" the way the action tests do: drive an inbound,
// wait for the SDK Call, force local state to answered so verbs may issue.
Call* answered_call(RelayClient& client) {
    Call* call = mt::drive_inbound_call(client, kCallId, {"created"});
    if (call != nullptr) {
        call->update_state("answered");
    }
    return call;
}

}  // namespace

int main() {
    // Silence the SDK logger programmatically so ONLY the JSON reaches stdout (the
    // logger writes INFO to std::cout; an env-only guard is racy against the RELAY
    // connect log). Mirrors wire_relay_dump.
    signalwire::Logger::instance().suppress();
    json out = json::object();

    // ---- live_play_wait -----------------------------------------------------
    {
        auto client = mt::make_client();
        Call* call = answered_call(*client);
        arm_finished("calling.play");
        Action action = call->play(play_media(), 0.0, kCid);
        out["live_play_wait"] = classify(drive(action));
        client->disconnect();
    }

    // ---- live_record_wait ---------------------------------------------------
    {
        auto client = mt::make_client();
        Call* call = answered_call(*client);
        arm_finished("calling.record");
        Action action = call->record({{"audio", {{"format", "mp3"}}}}, kCid);
        out["live_record_wait"] = classify(drive(action));
        client->disconnect();
    }

    // ---- live_nested_wait ---------------------------------------------------
    // The "wait inside on_completed" re-entrancy pattern. Like the python oracle
    // (diff_port_wait_liveness.py::_drive_nested) and the rust dump, the inner wait
    // is driven right AFTER the outer wait returns (not synchronously inside the
    // completion callback, which fires on the read-loop thread and would deadlock a
    // thread-blocking wait). It still exercises re-entrancy of the receive path: the
    // inner wait pumps the same connection the outer just used. FOLD: timed_out if
    // EITHER hung, blocked only if BOTH blocked, completed_state from the inner.
    {
        auto client = mt::make_client();
        Call* call = answered_call(*client);

        arm_finished("calling.play");
        Action outer = call->play(play_media(), 0.0, kCid);
        json outer_cls = classify(drive(outer));

        json folded;
        if (outer_cls.value("timed_out", false)) {
            folded = {{"blocked_until_event", false},
                      {"returned_after_event", false},
                      {"completed_state", ""},
                      {"timed_out", true}};
        } else {
            arm_finished("calling.record");
            Action inner = call->record({{"audio", {{"format", "mp3"}}}}, kCid);
            json inner_cls = classify(drive(inner));
            if (inner_cls.value("timed_out", false)) {
                folded = {{"blocked_until_event", false},
                          {"returned_after_event", false},
                          {"completed_state", ""},
                          {"timed_out", true}};
            } else {
                bool both_blocked = outer_cls.value("blocked_until_event", false) &&
                                    inner_cls.value("blocked_until_event", false);
                folded = {{"blocked_until_event", both_blocked},
                          {"returned_after_event", true},
                          {"completed_state", inner_cls.value("completed_state", "")},
                          {"timed_out", false}};
            }
        }
        out["live_nested_wait"] = folded;
        client->disconnect();
    }

    std::cout << out.dump() << "\n";
    return 0;
}
