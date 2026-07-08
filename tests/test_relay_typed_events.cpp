// Typed RELAY event tests — parse_event dispatch + per-type from_payload fields
#include "signalwire/relay/typed_events.hpp"

namespace tev = signalwire::relay::events;
using json = nlohmann::json;

static json make_payload(const std::string& event_type, const json& params) {
    return json::object({{"event_type", event_type}, {"params", params}});
}

TEST(typed_event_base_from_payload) {
    json p = make_payload("calling.call.custom",
                          json::object({{"call_id", "c1"}, {"timestamp", 12.5}}));
    tev::RelayEvent ev = tev::RelayEvent::from_payload(p);
    ASSERT_EQ(ev.event_type, "calling.call.custom");
    ASSERT_EQ(ev.call_id, "c1");
    ASSERT_EQ(ev.timestamp, 12.5);
    return true;
}

TEST(typed_event_call_state_fields) {
    json p = make_payload("calling.call.state",
                          json::object({{"call_id", "c1"},
                                        {"call_state", "answered"},
                                        {"end_reason", ""},
                                        {"direction", "inbound"}}));
    tev::CallStateEvent ev = tev::CallStateEvent::from_payload(p);
    ASSERT_EQ(ev.call_state, "answered");
    ASSERT_EQ(ev.direction, "inbound");
    ASSERT_EQ(ev.call_id, "c1");
    return true;
}

TEST(typed_event_play_control_id) {
    json p = make_payload("calling.call.play",
                          json::object({{"control_id", "ctrl-9"}, {"state", "finished"}}));
    tev::PlayEvent ev = tev::PlayEvent::from_payload(p);
    ASSERT_EQ(ev.control_id, "ctrl-9");
    ASSERT_EQ(ev.state, "finished");
    return true;
}

TEST(typed_event_parse_event_dispatches_by_type) {
    json p = make_payload("calling.call.state",
                          json::object({{"call_state", "ended"}}));
    tev::RelayEvent ev = tev::parse_event(p);
    ASSERT_EQ(ev.event_type, "calling.call.state");
    // The concrete type carries call_state; dispatch produced a tev::CallStateEvent
    // whose base fields are populated.
    ASSERT_EQ(ev.params.value("call_state", ""), "ended");
    return true;
}

TEST(typed_event_parse_event_unknown_falls_back_to_base) {
    json p = make_payload("calling.call.unknown_verb", json::object({{"call_id", "z"}}));
    tev::RelayEvent ev = tev::parse_event(p);
    ASSERT_EQ(ev.event_type, "calling.call.unknown_verb");
    ASSERT_EQ(ev.call_id, "z");
    return true;
}

TEST(typed_event_message_receive_fields) {
    json p = make_payload("messaging.receive",
                          json::object({{"message_id", "m1"},
                                        {"from_number", "+1555"},
                                        {"to_number", "+1666"},
                                        {"body", "hi"}}));
    tev::MessageReceiveEvent ev = tev::MessageReceiveEvent::from_payload(p);
    ASSERT_EQ(ev.message_id, "m1");
    ASSERT_EQ(ev.from_number, "+1555");
    ASSERT_EQ(ev.body, "hi");
    return true;
}

TEST(typed_event_conference_fields) {
    json p = make_payload("calling.conference",
                          json::object({{"conference_id", "cf1"}, {"name", "room"},
                                        {"status", "started"}}));
    tev::ConferenceEvent ev = tev::ConferenceEvent::from_payload(p);
    ASSERT_EQ(ev.conference_id, "cf1");
    ASSERT_EQ(ev.name, "room");
    ASSERT_EQ(ev.status, "started");
    return true;
}

TEST(typed_event_error_fields) {
    json p = make_payload("calling.error",
                          json::object({{"code", 42}, {"message", "boom"}}));
    tev::CallingErrorEvent ev = tev::CallingErrorEvent::from_payload(p);
    ASSERT_EQ(ev.code, 42);
    ASSERT_EQ(ev.message, "boom");
    return true;
}
