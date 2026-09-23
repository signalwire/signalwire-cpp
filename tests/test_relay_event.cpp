// RELAY Event parsing split tests
#include "signalwire/relay/relay_event.hpp"
using namespace signalwire::relay;
using json = nlohmann::json;

TEST(relay_event_basic_parse) {
  json j;
  j["event_type"] = "calling.call.state";
  j["params"] = {{"call_id", "c-1"}, {"call_state", "ringing"}};
  j["event_channel"] = "signalwire";
  auto ev = RelayEvent::from_json(j);
  ASSERT_EQ(ev.event_type, "calling.call.state");
  ASSERT_EQ(ev.event_channel, "signalwire");
  ASSERT_EQ(ev.params["call_id"], "c-1");
  return true;
}

TEST(relay_event_empty_json) {
  auto ev = RelayEvent::from_json(json::object());
  ASSERT_EQ(ev.event_type, "");
  ASSERT_TRUE(ev.params.is_object());
  return true;
}

TEST(relay_event_constructor_with_type) {
  RelayEvent ev("test.event");
  ASSERT_EQ(ev.event_type, "test.event");
  return true;
}

// ========================================================================
// CallEvent
// ========================================================================

TEST(relay_call_event_parse) {
  json j;
  j["event_type"] = "calling.call.state";
  j["params"] = {
      {"call_id", "c-1"}, {"node_id", "n-1"}, {"call_state", "answered"}, {"tag", "t-1"}};
  auto ce = CallEvent::from_relay_event(RelayEvent::from_json(j));
  ASSERT_EQ(ce.call_id, "c-1");
  ASSERT_EQ(ce.node_id, "n-1");
  ASSERT_EQ(ce.call_state, "answered");
  ASSERT_EQ(ce.tag, "t-1");
  ASSERT_FALSE(ce.peer_call_id.has_value());
  return true;
}

TEST(relay_call_event_with_peer_id) {
  json j;
  j["event_type"] = "calling.call.state";
  j["params"] = {{"call_id", "c-1"},
                 {"node_id", "n-1"},
                 {"call_state", "answered"},
                 {"peer", {{"call_id", "peer-1"}}}};
  auto ce = CallEvent::from_relay_event(RelayEvent::from_json(j));
  ASSERT_TRUE(ce.peer_call_id.has_value());
  ASSERT_EQ(ce.peer_call_id.value(), "peer-1");
  return true;
}

TEST(relay_call_event_missing_fields) {
  json j;
  j["event_type"] = "test";
  j["params"] = json::object();
  auto ce = CallEvent::from_relay_event(RelayEvent::from_json(j));
  ASSERT_EQ(ce.call_id, "");
  ASSERT_EQ(ce.call_state, "");
  return true;
}

// ========================================================================
// ComponentEvent
// ========================================================================

TEST(relay_component_event_parse) {
  json j;
  j["event_type"] = "calling.call.play";
  j["params"] = {{"call_id", "c-1"}, {"control_id", "ctl-1"}, {"state", "finished"}};
  auto ce = ComponentEvent::from_relay_event(RelayEvent::from_json(j));
  ASSERT_EQ(ce.call_id, "c-1");
  ASSERT_EQ(ce.control_id, "ctl-1");
  ASSERT_EQ(ce.state, "finished");
  return true;
}

TEST(relay_component_event_record) {
  json j;
  j["event_type"] = "calling.call.record";
  j["params"] = {{"call_id", "c-2"}, {"control_id", "ctl-2"}, {"state", "recording"}};
  auto ce = ComponentEvent::from_relay_event(RelayEvent::from_json(j));
  ASSERT_EQ(ce.state, "recording");
  return true;
}

// ========================================================================
// MessageEvent
// ========================================================================

TEST(relay_message_event_parse) {
  json j;
  j["event_type"] = "messaging.receive";
  j["params"] = {{"message_id", "m-1"},
                 {"message_state", "received"},
                 {"from_number", "+15551111111"},
                 {"to_number", "+15552222222"},
                 {"body", "Hello!"}};
  auto me = MessageEvent::from_relay_event(RelayEvent::from_json(j));
  ASSERT_EQ(me.message_id, "m-1");
  ASSERT_EQ(me.message_state, "received");
  ASSERT_EQ(me.from, "+15551111111");
  ASSERT_EQ(me.to, "+15552222222");
  ASSERT_EQ(me.body, "Hello!");
  return true;
}

// ========================================================================
// DialEvent
// ========================================================================

TEST(relay_dial_event_answered) {
  json j;
  j["event_type"] = "calling.call.dial";
  j["params"] = {{"tag", "dial-1"},
                 {"dial_state", "answered"},
                 {"call", {{"call_id", "winner"}, {"node_id", "n-w"}}}};
  auto de = DialEvent::from_relay_event(RelayEvent::from_json(j));
  ASSERT_EQ(de.tag, "dial-1");
  ASSERT_EQ(de.dial_state, "answered");
  ASSERT_EQ(de.call_info["call_id"], "winner");
  return true;
}

TEST(relay_dial_event_failed_no_call) {
  json j;
  j["event_type"] = "calling.call.dial";
  j["params"] = {{"tag", "dial-2"}, {"dial_state", "failed"}};
  auto de = DialEvent::from_relay_event(RelayEvent::from_json(j));
  ASSERT_EQ(de.dial_state, "failed");
  ASSERT_TRUE(de.call_info.empty());
  return true;
}
