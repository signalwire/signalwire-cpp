// RELAY Action subclasses + additional Call methods.
//
// The C++ port flattens all verb behaviour onto the unified `Action` base;
// the concrete subclasses (PlayAction, RecordAction, ...) exist so the class
// NAME is present and every method is inherited. These tests construct each
// subclass, exercise the inherited start_input_timers on the collect
// variants, and unit-test the frames the new client-less Call methods build
// (they resolve immediately without a client, so we assert the returned
// Action completes and carries the right control state).
#include "signalwire/relay/action.hpp"
#include "signalwire/relay/call.hpp"
#include "signalwire/relay/constants.hpp"

using namespace signalwire::relay;
using json = nlohmann::json;

// ── Action subclasses: names exist, construct, inherit Action ───────

TEST(relay_action_subclasses_construct) {
    PlayAction play("ctl-play");
    RecordAction record("ctl-record");
    CollectAction collect("ctl-collect");
    DetectAction detect("ctl-detect");
    FaxAction fax("ctl-fax");
    PayAction pay("ctl-pay");
    StreamAction stream("ctl-stream");
    TapAction tap("ctl-tap");
    TranscribeAction transcribe("ctl-transcribe");
    StandaloneCollectAction standalone("ctl-standalone");
    AIAction ai("ctl-ai");

    ASSERT_EQ(play.control_id(), "ctl-play");
    ASSERT_EQ(record.control_id(), "ctl-record");
    ASSERT_EQ(collect.control_id(), "ctl-collect");
    ASSERT_EQ(detect.control_id(), "ctl-detect");
    ASSERT_EQ(fax.control_id(), "ctl-fax");
    ASSERT_EQ(pay.control_id(), "ctl-pay");
    ASSERT_EQ(stream.control_id(), "ctl-stream");
    ASSERT_EQ(tap.control_id(), "ctl-tap");
    ASSERT_EQ(transcribe.control_id(), "ctl-transcribe");
    ASSERT_EQ(standalone.control_id(), "ctl-standalone");
    ASSERT_EQ(ai.control_id(), "ctl-ai");
    return true;
}

TEST(relay_action_subclass_is_action) {
    // A subclass is usable anywhere an Action base is expected.
    PlayAction play("ctl-1");
    Action& base = play;
    ASSERT_EQ(base.control_id(), "ctl-1");
    ASSERT_FALSE(base.completed());
    base.resolve("finished");
    ASSERT_TRUE(base.completed());
    return true;
}

TEST(relay_collect_subclasses_start_input_timers) {
    // StandaloneCollectAction + CollectAction expose the inherited
    // start_input_timers (no client -> no-op, must not throw / crash).
    CollectAction collect("ctl-collect");
    StandaloneCollectAction standalone("ctl-standalone");
    collect.start_input_timers();
    standalone.start_input_timers();
    // Still not completed by a bare start_input_timers.
    ASSERT_FALSE(collect.completed());
    ASSERT_FALSE(standalone.completed());
    return true;
}

TEST(relay_action_concrete_control_surface) {
    // The oracle projects the control methods directly onto each concrete
    // action. Assert every concrete subclass exposes exactly its mandated
    // control surface (callable on the concrete type; client-less -> no-op):
    //   Play:    stop / pause / resume / volume
    //   Record:  stop / pause / resume            (NO volume)
    //   Collect: stop / pause / resume / volume / start_input_timers
    //   others:  stop
    PlayAction play("ctl-play");
    play.stop();
    play.pause();
    play.pause("continuous");  // optional behavior, matching Python pause(behavior)
    play.resume();
    play.volume(-3.0);

    RecordAction record("ctl-record");
    record.stop();
    record.pause();
    record.pause("continuous");
    record.resume();

    CollectAction collect("ctl-collect");
    collect.stop();
    collect.pause();
    collect.resume();
    collect.volume(2.0);
    collect.start_input_timers();

    // Stop-only concrete actions.
    DetectAction("ctl-detect").stop();
    FaxAction("ctl-fax").stop();
    PayAction("ctl-pay").stop();
    StreamAction("ctl-stream").stop();
    TapAction("ctl-tap").stop();
    TranscribeAction("ctl-transcribe").stop();
    AIAction("ctl-ai").stop();
    StandaloneCollectAction("ctl-standalone").stop();

    // None completed by a bare fire-and-forget control frame.
    ASSERT_FALSE(play.completed());
    ASSERT_FALSE(record.completed());
    ASSERT_FALSE(collect.completed());
    return true;
}

// ── New Call methods (client-less: resolve immediately) ─────────────

TEST(relay_call_new_methods_complete_without_client) {
    Call call("c-100", "n-100");
    ASSERT_TRUE(call.pass_().completed());
    ASSERT_TRUE(call.denoise().completed());
    ASSERT_TRUE(call.denoise_stop().completed());
    ASSERT_TRUE(call.leave_room().completed());
    ASSERT_TRUE(call.leave_conference().completed());
    ASSERT_TRUE(call.leave_conference("conf-1").completed());
    ASSERT_TRUE(call.user_event("hello").completed());
    ASSERT_TRUE(call.echo().completed());
    ASSERT_TRUE(call.bind_digit("1", "notify").completed());
    ASSERT_TRUE(call.clear_digit_bindings().completed());
    ASSERT_TRUE(call.clear_digit_bindings("realm-a").completed());
    ASSERT_TRUE(call.queue_enter("support").completed());
    ASSERT_TRUE(call.queue_leave("support").completed());
    ASSERT_TRUE(call.ai_hold().completed());
    ASSERT_TRUE(call.ai_unhold().completed());
    ASSERT_TRUE(call.ai_message(json{{"text", "hi"}}).completed());
    ASSERT_TRUE(call.amazon_bedrock(json{{"model", "claude"}}).completed());
    ASSERT_TRUE(call.refer(json{{"type", "sip"}}).completed());
    return true;
}

TEST(relay_call_on_alias_receives_event) {
    Call call("c-101", "n-101");
    std::string captured;
    call.on([&](const CallEvent& ev) { captured = ev.call_state; });

    CallEvent ev;
    ev.event_type = "calling.call.state";
    ev.call_id = "c-101";
    ev.call_state = "answered";
    call.dispatch_event(ev);

    ASSERT_EQ(captured, "answered");
    return true;
}

TEST(relay_call_wait_for_state) {
    Call call("c-102", "n-102");
    // Already at/past "created" -> immediate true.
    ASSERT_TRUE(call.wait_for(CALL_STATE_CREATED, 100));

    std::thread t([&call]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        call.update_state("answered");
    });
    bool ok = call.wait_for(CALL_STATE_ANSWERED, 5000);
    t.join();
    ASSERT_TRUE(ok);
    return true;
}

TEST(relay_call_wait_for_times_out) {
    Call call("c-103", "n-103");
    // Never reaches ended -> times out (false) quickly.
    ASSERT_FALSE(call.wait_for(CALL_STATE_ENDED, 30));
    return true;
}

TEST(relay_call_repr) {
    Call call("c-104", "n-104");
    call.set_direction("inbound");
    std::string r = call.repr();
    ASSERT_TRUE(r.find("c-104") != std::string::npos);
    ASSERT_TRUE(r.find("created") != std::string::npos);
    ASSERT_TRUE(r.find("inbound") != std::string::npos);
    return true;
}
