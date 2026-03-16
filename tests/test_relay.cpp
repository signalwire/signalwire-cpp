// RELAY client tests

#include "signalwire/relay/constants.hpp"
#include "signalwire/relay/relay_event.hpp"
#include "signalwire/relay/action.hpp"
#include "signalwire/relay/call.hpp"
#include "signalwire/relay/message.hpp"
#include "signalwire/relay/client.hpp"

using namespace signalwire::relay;
using json = nlohmann::json;

// ========================================================================
// Constants tests
// ========================================================================

TEST(relay_constants_default_host) {
    ASSERT_EQ(std::string(DEFAULT_HOST), "relay.signalwire.com");
    return true;
}

TEST(relay_constants_default_port) {
    ASSERT_EQ(DEFAULT_PORT, 443);
    return true;
}

TEST(relay_constants_call_states) {
    ASSERT_EQ(std::string(CALL_STATE_CREATED), "created");
    ASSERT_EQ(std::string(CALL_STATE_RINGING), "ringing");
    ASSERT_EQ(std::string(CALL_STATE_ANSWERED), "answered");
    ASSERT_EQ(std::string(CALL_STATE_ENDING), "ending");
    ASSERT_EQ(std::string(CALL_STATE_ENDED), "ended");
    return true;
}

TEST(relay_constants_connect_states) {
    ASSERT_EQ(std::string(CONNECT_STATE_CONNECTING), "connecting");
    ASSERT_EQ(std::string(CONNECT_STATE_CONNECTED), "connected");
    ASSERT_EQ(std::string(CONNECT_STATE_FAILED), "failed");
    ASSERT_EQ(std::string(CONNECT_STATE_DISCONNECTED), "disconnected");
    return true;
}

TEST(relay_constants_component_states) {
    ASSERT_EQ(std::string(COMPONENT_STATE_PLAYING), "playing");
    ASSERT_EQ(std::string(COMPONENT_STATE_FINISHED), "finished");
    ASSERT_EQ(std::string(COMPONENT_STATE_ERROR), "error");
    ASSERT_EQ(std::string(COMPONENT_STATE_RECORDING), "recording");
    ASSERT_EQ(std::string(COMPONENT_STATE_NO_INPUT), "no_input");
    return true;
}

TEST(relay_constants_event_types) {
    ASSERT_EQ(std::string(EVENT_CALL_RECEIVED), "calling.call.received");
    ASSERT_EQ(std::string(EVENT_CALL_STATE), "calling.call.state");
    ASSERT_EQ(std::string(EVENT_CALL_PLAY), "calling.call.play");
    ASSERT_EQ(std::string(EVENT_CALL_RECORD), "calling.call.record");
    ASSERT_EQ(std::string(EVENT_CALL_COLLECT), "calling.call.collect");
    ASSERT_EQ(std::string(EVENT_CALL_TAP), "calling.call.tap");
    ASSERT_EQ(std::string(EVENT_CALL_DETECT), "calling.call.detect");
    ASSERT_EQ(std::string(EVENT_CALL_CONNECT), "calling.call.connect");
    ASSERT_EQ(std::string(EVENT_CALL_FAX), "calling.call.fax");
    ASSERT_EQ(std::string(EVENT_CALL_SEND_DIGITS), "calling.call.send_digits");
    ASSERT_EQ(std::string(EVENT_MESSAGING_RECEIVE), "messaging.receive");
    ASSERT_EQ(std::string(EVENT_MESSAGING_STATE), "messaging.state");
    return true;
}

TEST(relay_constants_reconnect) {
    ASSERT_EQ(RECONNECT_BASE_DELAY_MS, 1000);
    ASSERT_EQ(RECONNECT_MAX_DELAY_MS, 30000);
    ASSERT_TRUE(RECONNECT_BACKOFF_FACTOR == 2.0);
    ASSERT_EQ(MAX_RECONNECT_ATTEMPTS, 50);
    return true;
}

TEST(relay_constants_concurrency) {
    ASSERT_EQ(DEFAULT_MAX_ACTIVE_CALLS, 1000);
    ASSERT_EQ(DEFAULT_MAX_CONNECTIONS, 1);
    return true;
}

// ========================================================================
// Event parsing tests
// ========================================================================

TEST(relay_event_from_json_basic) {
    json j;
    j["event_type"] = "calling.call.state";
    j["params"] = {{"call_id", "abc-123"}, {"call_state", "ringing"}};
    j["event_channel"] = "signalwire";

    auto ev = RelayEvent::from_json(j);
    ASSERT_EQ(ev.event_type, "calling.call.state");
    ASSERT_EQ(ev.params["call_id"], "abc-123");
    ASSERT_EQ(ev.event_channel, "signalwire");
    return true;
}

TEST(relay_event_from_json_empty) {
    json j = json::object();
    auto ev = RelayEvent::from_json(j);
    ASSERT_EQ(ev.event_type, "");
    ASSERT_TRUE(ev.params.is_object());
    ASSERT_EQ(ev.event_channel, "");
    return true;
}

TEST(relay_call_event_from_relay_event) {
    json j;
    j["event_type"] = "calling.call.state";
    j["params"] = {
        {"call_id", "call-001"},
        {"node_id", "node-001"},
        {"call_state", "answered"},
        {"tag", "my-tag"}
    };

    auto ev = RelayEvent::from_json(j);
    auto ce = CallEvent::from_relay_event(ev);
    ASSERT_EQ(ce.call_id, "call-001");
    ASSERT_EQ(ce.node_id, "node-001");
    ASSERT_EQ(ce.call_state, "answered");
    ASSERT_EQ(ce.tag, "my-tag");
    ASSERT_FALSE(ce.peer_call_id.has_value());
    return true;
}

TEST(relay_call_event_with_peer) {
    json j;
    j["event_type"] = "calling.call.state";
    j["params"] = {
        {"call_id", "call-001"},
        {"node_id", "node-001"},
        {"call_state", "answered"},
        {"peer", {{"call_id", "peer-call-002"}}}
    };

    auto ev = RelayEvent::from_json(j);
    auto ce = CallEvent::from_relay_event(ev);
    ASSERT_TRUE(ce.peer_call_id.has_value());
    ASSERT_EQ(ce.peer_call_id.value(), "peer-call-002");
    return true;
}

TEST(relay_component_event_parsing) {
    json j;
    j["event_type"] = "calling.call.play";
    j["params"] = {
        {"call_id", "call-001"},
        {"control_id", "ctl-001"},
        {"state", "finished"}
    };

    auto ev = RelayEvent::from_json(j);
    auto ce = ComponentEvent::from_relay_event(ev);
    ASSERT_EQ(ce.call_id, "call-001");
    ASSERT_EQ(ce.control_id, "ctl-001");
    ASSERT_EQ(ce.state, "finished");
    return true;
}

TEST(relay_message_event_parsing) {
    json j;
    j["event_type"] = "messaging.receive";
    j["params"] = {
        {"message_id", "msg-001"},
        {"message_state", "received"},
        {"from_number", "+15551234567"},
        {"to_number", "+15559876543"},
        {"body", "Hello World"}
    };

    auto ev = RelayEvent::from_json(j);
    auto me = MessageEvent::from_relay_event(ev);
    ASSERT_EQ(me.message_id, "msg-001");
    ASSERT_EQ(me.message_state, "received");
    ASSERT_EQ(me.from, "+15551234567");
    ASSERT_EQ(me.to, "+15559876543");
    ASSERT_EQ(me.body, "Hello World");
    return true;
}

TEST(relay_dial_event_parsing) {
    json j;
    j["event_type"] = "calling.call.dial";
    j["params"] = {
        {"tag", "dial-tag-001"},
        {"dial_state", "answered"},
        {"call", {
            {"call_id", "winner-uuid"},
            {"node_id", "node-uuid"},
            {"dial_winner", true}
        }}
    };

    auto ev = RelayEvent::from_json(j);
    auto de = DialEvent::from_relay_event(ev);
    ASSERT_EQ(de.tag, "dial-tag-001");
    ASSERT_EQ(de.dial_state, "answered");
    ASSERT_EQ(de.call_info["call_id"], "winner-uuid");
    ASSERT_EQ(de.call_info["node_id"], "node-uuid");
    ASSERT_TRUE(de.call_info["dial_winner"].get<bool>());
    return true;
}

TEST(relay_dial_event_failed) {
    json j;
    j["event_type"] = "calling.call.dial";
    j["params"] = {
        {"tag", "dial-tag-002"},
        {"dial_state", "failed"}
    };

    auto ev = RelayEvent::from_json(j);
    auto de = DialEvent::from_relay_event(ev);
    ASSERT_EQ(de.tag, "dial-tag-002");
    ASSERT_EQ(de.dial_state, "failed");
    ASSERT_TRUE(de.call_info.is_object());
    ASSERT_TRUE(de.call_info.empty());
    return true;
}

// ========================================================================
// Action tests
// ========================================================================

TEST(relay_action_default_construction) {
    Action action;
    ASSERT_EQ(action.control_id(), "");
    ASSERT_EQ(action.state(), "");
    ASSERT_FALSE(action.completed());
    ASSERT_TRUE(action.result().is_null());
    return true;
}

TEST(relay_action_construction_with_control_id) {
    Action action("ctl-123");
    ASSERT_EQ(action.control_id(), "ctl-123");
    ASSERT_FALSE(action.completed());
    return true;
}

TEST(relay_action_construction_full) {
    Action action("ctl-123", nullptr, "call-001", "node-001");
    ASSERT_EQ(action.control_id(), "ctl-123");
    ASSERT_EQ(action.call_id(), "call-001");
    ASSERT_EQ(action.node_id(), "node-001");
    ASSERT_FALSE(action.completed());
    return true;
}

TEST(relay_action_wait_already_completed) {
    Action action("ctl-001");
    action.resolve("finished", {{"url", "https://example.com/recording.mp3"}});
    ASSERT_TRUE(action.completed());
    ASSERT_TRUE(action.wait(100));
    ASSERT_EQ(action.state(), "finished");
    ASSERT_EQ(action.result()["url"], "https://example.com/recording.mp3");
    return true;
}

TEST(relay_action_wait_timeout) {
    Action action("ctl-002");
    // Not completed, should timeout
    bool done = action.wait(50);
    ASSERT_FALSE(done);
    ASSERT_FALSE(action.completed());
    return true;
}

TEST(relay_action_update_state_finished) {
    Action action("ctl-003");
    action.update_state("playing");
    ASSERT_EQ(action.state(), "playing");
    ASSERT_FALSE(action.completed());

    action.update_state("finished", {{"duration", 5.2}});
    ASSERT_EQ(action.state(), "finished");
    ASSERT_TRUE(action.completed());
    return true;
}

TEST(relay_action_update_state_error) {
    Action action("ctl-004");
    action.update_state("error", {{"reason", "timeout"}});
    ASSERT_TRUE(action.completed());
    ASSERT_EQ(action.state(), "error");
    return true;
}

TEST(relay_action_update_state_no_input) {
    Action action("ctl-005");
    action.update_state("no_input");
    ASSERT_TRUE(action.completed());
    return true;
}

TEST(relay_action_update_state_no_match) {
    Action action("ctl-006");
    action.update_state("no_match");
    ASSERT_TRUE(action.completed());
    return true;
}

TEST(relay_action_resolve) {
    Action action("ctl-007");
    action.resolve("finished", {{"test", true}});
    ASSERT_TRUE(action.completed());
    ASSERT_EQ(action.state(), "finished");
    ASSERT_TRUE(action.result()["test"].get<bool>());
    return true;
}

TEST(relay_action_on_completed_callback) {
    Action action("ctl-008");
    bool callback_fired = false;
    std::string callback_state;

    action.on_completed([&](const Action& a) {
        callback_fired = true;
        callback_state = a.state();
    });

    ASSERT_FALSE(callback_fired);
    action.update_state("finished");
    ASSERT_TRUE(callback_fired);
    ASSERT_EQ(callback_state, "finished");
    return true;
}

TEST(relay_action_on_completed_callback_already_done) {
    Action action("ctl-009");
    action.resolve("error");

    bool callback_fired = false;
    action.on_completed([&](const Action&) {
        callback_fired = true;
    });

    ASSERT_TRUE(callback_fired);
    return true;
}

TEST(relay_action_on_completed_callback_exception_handled) {
    Action action("ctl-010");
    action.on_completed([](const Action&) {
        throw std::runtime_error("test exception");
    });
    // Should not throw - error is caught and logged
    action.update_state("finished");
    ASSERT_TRUE(action.completed());
    return true;
}

TEST(relay_action_wait_with_resolve_from_thread) {
    Action action("ctl-011");

    std::thread resolver([&action]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        action.resolve("finished", {{"data", "resolved"}});
    });

    bool done = action.wait(5000);
    ASSERT_TRUE(done);
    ASSERT_TRUE(action.completed());
    ASSERT_EQ(action.state(), "finished");

    resolver.join();
    return true;
}

TEST(relay_action_stop_without_client) {
    Action action("ctl-012");
    // Should not crash when no client is set
    action.stop();
    action.pause();
    action.resume();
    return true;
}

// ========================================================================
// Call tests
// ========================================================================

TEST(relay_call_default_construction) {
    Call call;
    ASSERT_EQ(call.call_id(), "");
    ASSERT_EQ(call.node_id(), "");
    ASSERT_EQ(call.state(), "");
    return true;
}

TEST(relay_call_construction_with_ids) {
    Call call("call-001", "node-001");
    ASSERT_EQ(call.call_id(), "call-001");
    ASSERT_EQ(call.node_id(), "node-001");
    ASSERT_EQ(call.state(), "created");
    ASSERT_FALSE(call.is_answered());
    ASSERT_FALSE(call.is_ended());
    return true;
}

TEST(relay_call_construction_with_client) {
    Call call("call-002", "node-002", nullptr);
    ASSERT_EQ(call.call_id(), "call-002");
    ASSERT_EQ(call.state(), "created");
    return true;
}

TEST(relay_call_state_transitions) {
    Call call("call-003", "node-003");
    ASSERT_EQ(call.state(), "created");
    ASSERT_FALSE(call.is_answered());

    call.update_state("ringing");
    ASSERT_EQ(call.state(), "ringing");
    ASSERT_FALSE(call.is_answered());

    call.update_state("answered");
    ASSERT_EQ(call.state(), "answered");
    ASSERT_TRUE(call.is_answered());
    ASSERT_FALSE(call.is_ended());

    call.update_state("ending");
    ASSERT_FALSE(call.is_answered());

    call.update_state("ended");
    ASSERT_TRUE(call.is_ended());
    return true;
}

TEST(relay_call_properties) {
    Call call("call-004", "node-004");
    call.set_direction("inbound");
    call.set_from("+15551234567");
    call.set_to("+15559876543");
    call.set_tag("my-tag");

    ASSERT_EQ(call.direction(), "inbound");
    ASSERT_EQ(call.from(), "+15551234567");
    ASSERT_EQ(call.to(), "+15559876543");
    ASSERT_EQ(call.tag(), "my-tag");
    return true;
}

TEST(relay_call_event_dispatch) {
    Call call("call-005", "node-005");
    bool handler_called = false;
    std::string received_event_type;

    call.on_event([&](const CallEvent& ev) {
        handler_called = true;
        received_event_type = ev.event_type;
    });

    CallEvent ev;
    ev.event_type = "calling.call.state";
    ev.call_id = "call-005";
    ev.call_state = "answered";
    call.dispatch_event(ev);

    ASSERT_TRUE(handler_called);
    ASSERT_EQ(received_event_type, "calling.call.state");
    return true;
}

TEST(relay_call_multiple_event_handlers) {
    Call call("call-006", "node-006");
    int handler_count = 0;

    call.on_event([&](const CallEvent&) { handler_count++; });
    call.on_event([&](const CallEvent&) { handler_count++; });
    call.on_event([&](const CallEvent&) { handler_count++; });

    CallEvent ev;
    ev.event_type = "test";
    call.dispatch_event(ev);

    ASSERT_EQ(handler_count, 3);
    return true;
}

TEST(relay_call_wait_for_ended_already_ended) {
    Call call("call-007", "node-007");
    call.update_state("ended");
    ASSERT_TRUE(call.wait_for_ended(100));
    return true;
}

TEST(relay_call_wait_for_ended_timeout) {
    Call call("call-008", "node-008");
    bool ended = call.wait_for_ended(50);
    ASSERT_FALSE(ended);
    return true;
}

TEST(relay_call_wait_for_ended_from_thread) {
    Call call("call-009", "node-009");
    std::thread ender([&call]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        call.update_state("ended");
    });

    bool ended = call.wait_for_ended(5000);
    ASSERT_TRUE(ended);
    ASSERT_TRUE(call.is_ended());

    ender.join();
    return true;
}

TEST(relay_call_action_registry) {
    Call call("call-010", "node-010");
    Action action("ctl-100");

    call.register_action("ctl-100", &action);
    Action* found = call.find_action("ctl-100");
    ASSERT_TRUE(found != nullptr);
    ASSERT_EQ(found->control_id(), "ctl-100");

    Action* not_found = call.find_action("nonexistent");
    ASSERT_TRUE(not_found == nullptr);

    call.unregister_action("ctl-100");
    Action* after_unregister = call.find_action("ctl-100");
    ASSERT_TRUE(after_unregister == nullptr);
    return true;
}

TEST(relay_call_resolve_all_actions) {
    Call call("call-011", "node-011");
    Action action1("ctl-101");
    Action action2("ctl-102");

    call.register_action("ctl-101", &action1);
    call.register_action("ctl-102", &action2);

    ASSERT_FALSE(action1.completed());
    ASSERT_FALSE(action2.completed());

    call.resolve_all_actions("finished");

    ASSERT_TRUE(action1.completed());
    ASSERT_TRUE(action2.completed());
    ASSERT_EQ(action1.state(), "finished");
    ASSERT_EQ(action2.state(), "finished");
    return true;
}

TEST(relay_call_ended_resolves_actions) {
    Call call("call-012", "node-012");
    Action action("ctl-103");
    call.register_action("ctl-103", &action);

    call.update_state("ended");

    ASSERT_TRUE(action.completed());
    return true;
}

TEST(relay_call_methods_without_client) {
    // All call control methods should complete without crashing when no client
    Call call("call-013", "node-013");

    auto a1 = call.answer();
    ASSERT_TRUE(a1.completed());

    auto a2 = call.hangup();
    ASSERT_TRUE(a2.completed());

    auto a3 = call.play(json::array({{{"type", "tts"}, {"params", {{"text", "hi"}}}}}));
    ASSERT_TRUE(a3.completed());

    auto a4 = call.record();
    ASSERT_TRUE(a4.completed());

    auto a5 = call.connect(json::array());
    ASSERT_TRUE(a5.completed());

    auto a6 = call.disconnect();
    ASSERT_TRUE(a6.completed());

    auto a7 = call.hold();
    ASSERT_TRUE(a7.completed());

    auto a8 = call.unhold();
    ASSERT_TRUE(a8.completed());

    auto a9 = call.detect(json::object());
    ASSERT_TRUE(a9.completed());

    auto a10 = call.tap_audio(json::object());
    ASSERT_TRUE(a10.completed());

    auto a11 = call.send_digits("1234");
    ASSERT_TRUE(a11.completed());

    auto a12 = call.transfer(json::object());
    ASSERT_TRUE(a12.completed());

    auto a13 = call.ai(json::object());
    ASSERT_TRUE(a13.completed());

    auto a14 = call.send_fax("https://example.com/doc.pdf");
    ASSERT_TRUE(a14.completed());

    auto a15 = call.receive_fax();
    ASSERT_TRUE(a15.completed());

    auto a16 = call.sip_refer("sip:user@example.com");
    ASSERT_TRUE(a16.completed());

    auto a17 = call.join_conference("my-conf");
    ASSERT_TRUE(a17.completed());

    auto a18 = call.join_room("my-room");
    ASSERT_TRUE(a18.completed());

    auto a19 = call.prompt(json::array(), json::object());
    ASSERT_TRUE(a19.completed());

    auto a20 = call.collect(json::object());
    ASSERT_TRUE(a20.completed());

    auto a21 = call.live_transcribe();
    ASSERT_TRUE(a21.completed());

    auto a22 = call.live_translate();
    ASSERT_TRUE(a22.completed());

    auto a23 = call.execute_swml(json::object());
    ASSERT_TRUE(a23.completed());

    auto a24 = call.stop_tap("ctl-999");
    ASSERT_TRUE(a24.completed());

    auto a25 = call.record_call();
    ASSERT_TRUE(a25.completed());

    return true;
}

// ========================================================================
// Message tests
// ========================================================================

TEST(relay_message_default_construction) {
    Message msg;
    ASSERT_EQ(msg.message_id, "");
    ASSERT_EQ(msg.state, "");
    ASSERT_FALSE(msg.is_delivered());
    ASSERT_FALSE(msg.is_failed());
    ASSERT_FALSE(msg.is_terminal());
    return true;
}

TEST(relay_message_from_params) {
    json params;
    params["message_id"] = "msg-001";
    params["message_state"] = "queued";
    params["from_number"] = "+15551234567";
    params["to_number"] = "+15559876543";
    params["body"] = "Hello!";
    params["direction"] = "outbound";
    params["media"] = json::array({"https://example.com/image.jpg"});
    params["tags"] = json::array({"support", "urgent"});

    auto msg = Message::from_params(params);
    ASSERT_EQ(msg.message_id, "msg-001");
    ASSERT_EQ(msg.state, "queued");
    ASSERT_EQ(msg.from, "+15551234567");
    ASSERT_EQ(msg.to, "+15559876543");
    ASSERT_EQ(msg.body, "Hello!");
    ASSERT_EQ(msg.direction, "outbound");
    ASSERT_EQ(msg.media.size(), static_cast<size_t>(1));
    ASSERT_EQ(msg.media[0], "https://example.com/image.jpg");
    ASSERT_EQ(msg.tags.size(), static_cast<size_t>(2));
    ASSERT_EQ(msg.tags[0], "support");
    ASSERT_EQ(msg.tags[1], "urgent");
    return true;
}

TEST(relay_message_state_transitions) {
    Message msg;
    msg.message_id = "msg-002";
    msg.state = "queued";

    ASSERT_FALSE(msg.is_delivered());
    ASSERT_FALSE(msg.is_failed());
    ASSERT_FALSE(msg.is_terminal());

    msg.update_state("initiated");
    ASSERT_EQ(msg.state, "initiated");
    ASSERT_FALSE(msg.is_terminal());

    msg.update_state("sent");
    ASSERT_EQ(msg.state, "sent");
    ASSERT_FALSE(msg.is_terminal());

    msg.update_state("delivered");
    ASSERT_EQ(msg.state, "delivered");
    ASSERT_TRUE(msg.is_delivered());
    ASSERT_TRUE(msg.is_terminal());
    return true;
}

TEST(relay_message_failed_state) {
    Message msg;
    msg.message_id = "msg-003";
    msg.update_state("failed");
    ASSERT_TRUE(msg.is_failed());
    ASSERT_TRUE(msg.is_terminal());
    return true;
}

TEST(relay_message_undelivered_state) {
    Message msg;
    msg.message_id = "msg-004";
    msg.update_state("undelivered");
    ASSERT_TRUE(msg.is_failed());
    ASSERT_TRUE(msg.is_terminal());
    return true;
}

TEST(relay_message_wait_already_terminal) {
    Message msg;
    msg.update_state("delivered");
    ASSERT_TRUE(msg.wait(100));
    return true;
}

TEST(relay_message_wait_timeout) {
    Message msg;
    msg.state = "queued";
    // Not terminal, should timeout
    bool done = msg.wait(50);
    ASSERT_FALSE(done);
    return true;
}

TEST(relay_message_on_completed_callback) {
    Message msg;
    msg.message_id = "msg-005";
    bool callback_fired = false;

    msg.on_completed([&](const Message& m) {
        callback_fired = true;
        (void)m;
    });

    msg.update_state("queued");
    ASSERT_FALSE(callback_fired);

    msg.update_state("delivered");
    ASSERT_TRUE(callback_fired);
    return true;
}

TEST(relay_message_from_params_with_state_fallback) {
    // Test fallback from "state" when "message_state" is absent
    json params;
    params["message_id"] = "msg-006";
    params["state"] = "sent";

    auto msg = Message::from_params(params);
    ASSERT_EQ(msg.state, "sent");
    return true;
}

// ========================================================================
// Client construction tests
// ========================================================================

TEST(relay_client_construction_default) {
    RelayClient client;
    ASSERT_EQ(client.config().host, DEFAULT_HOST);
    ASSERT_EQ(client.config().port, DEFAULT_PORT);
    ASSERT_FALSE(client.is_connected());
    return true;
}

TEST(relay_client_construction_with_config) {
    RelayConfig cfg;
    cfg.project = "test-project";
    cfg.token = "test-token";
    cfg.host = "test.signalwire.com";
    cfg.contexts = {"support", "sales"};

    RelayClient client(cfg);
    ASSERT_EQ(client.config().project, "test-project");
    ASSERT_EQ(client.config().token, "test-token");
    ASSERT_EQ(client.config().host, "test.signalwire.com");
    ASSERT_EQ(client.config().contexts.size(), static_cast<size_t>(2));
    ASSERT_EQ(client.config().contexts[0], "support");
    ASSERT_EQ(client.config().contexts[1], "sales");
    return true;
}

TEST(relay_client_construction_with_params) {
    RelayClient client("proj-123", "tok-456", "my.signalwire.com", {"ctx1", "ctx2"});
    ASSERT_EQ(client.config().project, "proj-123");
    ASSERT_EQ(client.config().token, "tok-456");
    ASSERT_EQ(client.config().host, "my.signalwire.com");
    ASSERT_EQ(client.config().contexts.size(), static_cast<size_t>(2));
    return true;
}

TEST(relay_client_from_env) {
    // from_env reads env vars, should not crash even if unset
    auto client = RelayClient::from_env();
    ASSERT_FALSE(client.is_connected());
    return true;
}

TEST(relay_client_relay_protocol_default) {
    RelayClient client;
    // Protocol is empty until authenticated
    ASSERT_EQ(client.relay_protocol(), "");
    return true;
}

TEST(relay_client_subscribe) {
    RelayClient client("proj", "tok");
    // Subscribe locally (no server connection)
    client.subscribe({"new-context"});
    bool found = false;
    for (const auto& c : client.config().contexts) {
        if (c == "new-context") found = true;
    }
    ASSERT_TRUE(found);
    return true;
}

TEST(relay_client_unsubscribe) {
    RelayConfig cfg;
    cfg.contexts = {"ctx1", "ctx2", "ctx3"};
    RelayClient client(cfg);

    client.unsubscribe({"ctx2"});
    ASSERT_EQ(client.config().contexts.size(), static_cast<size_t>(2));
    for (const auto& c : client.config().contexts) {
        ASSERT_NE(c, "ctx2");
    }
    return true;
}

TEST(relay_client_on_call_handler) {
    RelayClient client;
    bool handler_set = false;
    client.on_call([&](Call&) { handler_set = true; });
    // Handler is set but won't fire without a connection
    // Verify no crash
    return true;
}

TEST(relay_client_on_message_handler) {
    RelayClient client;
    bool handler_set = false;
    client.on_message([&](const Message&) { handler_set = true; });
    // Handler is set but won't fire without a connection
    // Verify no crash
    return true;
}

TEST(relay_client_disconnect_when_not_connected) {
    RelayClient client;
    // Disconnecting when not connected should complete without error
    client.disconnect();
    ASSERT_FALSE(client.is_connected());
    return true;
}

TEST(relay_client_config_defaults) {
    RelayConfig cfg;
    ASSERT_EQ(cfg.host, DEFAULT_HOST);
    ASSERT_EQ(cfg.port, DEFAULT_PORT);
    ASSERT_EQ(cfg.max_active_calls, DEFAULT_MAX_ACTIVE_CALLS);
    ASSERT_EQ(cfg.max_connections, DEFAULT_MAX_CONNECTIONS);
    ASSERT_EQ(cfg.contexts.size(), static_cast<size_t>(1));
    ASSERT_EQ(cfg.contexts[0], "default");
    return true;
}
