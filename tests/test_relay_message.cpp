// RELAY Message split tests — state transitions, parsing, callbacks
#include "signalwire/relay/message.hpp"
using namespace signalwire::relay;
using json = nlohmann::json;

TEST(relay_msg_default_state) {
  Message msg;
  ASSERT_EQ(msg.message_id, "");
  ASSERT_EQ(msg.state(), "");
  ASSERT_FALSE(msg.is_delivered());
  ASSERT_FALSE(msg.is_failed());
  ASSERT_FALSE(msg.is_terminal());
  return true;
}

TEST(relay_msg_from_params_full) {
  json p;
  p["message_id"] = "m-1";
  p["message_state"] = "queued";
  p["from_number"] = "+15551111111";
  p["to_number"] = "+15552222222";
  p["body"] = "Hello!";
  p["direction"] = "outbound";
  p["media"] = json::array({"img.jpg", "doc.pdf"});
  p["tags"] = json::array({"vip"});
  auto msg = Message::from_params(p);
  ASSERT_EQ(msg.message_id, "m-1");
  ASSERT_EQ(msg.state(), "queued");
  ASSERT_EQ(msg.from_number, "+15551111111");
  ASSERT_EQ(msg.body, "Hello!");
  ASSERT_EQ(msg.media.size(), static_cast<size_t>(2));
  ASSERT_EQ(msg.tags.size(), static_cast<size_t>(1));
  return true;
}

// Reference parity: `_handle_inbound_message` builds the Message with
// `context=params["context"]` and `segments=params["segments"]`. Both are
// caller-readable state on the reference Message; the port read neither off
// the wire, so an inbound multi-segment message lost its context and segment
// count.
TEST(relay_msg_from_params_context_and_segments) {
  json p;
  p["message_id"] = "m-ctx";
  p["message_state"] = "received";
  p["direction"] = "inbound";
  p["context"] = "support";
  p["segments"] = 3;
  auto msg = Message::from_params(p);
  ASSERT_EQ(msg.context, "support");
  ASSERT_EQ(msg.segments, 3);
  return true;
}

// Absent on the wire, both fall back to the reference's defaults ("" / 0).
TEST(relay_msg_from_params_context_and_segments_absent) {
  json p;
  p["message_id"] = "m-bare";
  auto msg = Message::from_params(p);
  ASSERT_EQ(msg.context, "");
  ASSERT_EQ(msg.segments, 0);
  return true;
}

TEST(relay_msg_from_params_state_fallback) {
  json p;
  p["message_id"] = "m-2";
  p["state"] = "sent";
  auto msg = Message::from_params(p);
  ASSERT_EQ(msg.state(), "sent");
  return true;
}

TEST(relay_msg_lifecycle_queued_to_delivered) {
  Message msg;
  msg.set_state("queued");
  ASSERT_FALSE(msg.is_terminal());

  msg.update_state("initiated");
  ASSERT_FALSE(msg.is_terminal());

  msg.update_state("sent");
  ASSERT_FALSE(msg.is_terminal());

  msg.update_state("delivered");
  ASSERT_TRUE(msg.is_delivered());
  ASSERT_TRUE(msg.is_terminal());
  return true;
}

TEST(relay_msg_failed_terminal) {
  Message msg;
  msg.update_state("failed");
  ASSERT_TRUE(msg.is_failed());
  ASSERT_TRUE(msg.is_terminal());
  return true;
}

TEST(relay_msg_undelivered_terminal) {
  Message msg;
  msg.update_state("undelivered");
  ASSERT_TRUE(msg.is_failed());
  ASSERT_TRUE(msg.is_terminal());
  return true;
}

TEST(relay_msg_callback_on_delivered) {
  Message msg;
  bool fired = false;
  msg.on_completed([&](const Message&) { fired = true; });

  msg.update_state("queued");
  ASSERT_FALSE(fired);

  msg.update_state("delivered");
  ASSERT_TRUE(fired);
  return true;
}

TEST(relay_msg_callback_on_failed) {
  Message msg;
  bool fired = false;
  msg.on_completed([&](const Message&) { fired = true; });
  msg.update_state("failed");
  ASSERT_TRUE(fired);
  return true;
}

TEST(relay_msg_wait_already_terminal) {
  Message msg;
  msg.update_state("delivered");
  ASSERT_TRUE(msg.wait(100));
  return true;
}

TEST(relay_msg_wait_timeout) {
  Message msg;
  msg.set_state("queued");
  ASSERT_FALSE(msg.wait(50));
  return true;
}
