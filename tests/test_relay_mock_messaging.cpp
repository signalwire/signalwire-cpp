// Mock-relay-backed tests for messaging (send_message + inbound)
//
// Translated from
//   signalwire-python/tests/unit/relay/test_messaging_mock.py
//
// Drives the real RelayClient against the shared mock-relay server.
// Assertions check both the SDK's exposed Message state and the on-wire
// messaging.send / pushed messaging.state frames.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <random>
#include <sstream>
#include <thread>

#include "relay_mocktest.hpp"
#include "signalwire/relay/client.hpp"
#include "signalwire/relay/message.hpp"

using namespace signalwire::relay;
namespace mt = signalwire::relay::mocktest;
using json = nlohmann::json;

namespace {

std::string fresh_uuid_msg() {
  static std::random_device rd;
  static std::mt19937_64 gen(rd());
  std::ostringstream ss;
  ss << std::hex << gen();
  return ss.str();
}

template <class P>
bool spin_until_msg(P pred, int timeout_ms = 5000) {
  auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (std::chrono::steady_clock::now() < deadline) {
    if (pred()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return false;
}

json messaging_state_frame(const std::string& message_id, const std::string& state,
                           const std::string& reason = "") {
  json frame;
  frame["jsonrpc"] = "2.0";
  frame["id"] = fresh_uuid_msg();
  frame["method"] = "signalwire.event";
  frame["params"]["event_type"] = "messaging.state";
  json& p = frame["params"]["params"];
  p["message_id"] = message_id;
  p["message_state"] = state;
  if (!reason.empty()) {
    p["reason"] = reason;
  }
  return frame;
}

}  // namespace

// ---------------------------------------------------------------------------
// send_message — outbound
// ---------------------------------------------------------------------------

TEST(relay_mock_send_message_journals_messaging_send) {
  auto client = mt::make_client();
  Message msg = client->send_message("+15553334444", "+15551112222", "hello", {}, {"t1", "t2"});
  ASSERT_FALSE(msg.message_id.empty());
  ASSERT_EQ(msg.body, "hello");

  auto sends = mt::journal_recv("messaging.send");
  ASSERT_EQ(sends.size(), static_cast<size_t>(1));
  json p = sends.back().frame["params"];
  ASSERT_EQ(p.value("to_number", ""), "+15551112222");
  ASSERT_EQ(p.value("from_number", ""), "+15553334444");
  ASSERT_EQ(p.value("body", ""), "hello");
  ASSERT_TRUE(p["tags"].is_array());
  ASSERT_EQ(p["tags"].size(), static_cast<size_t>(2));
  ASSERT_EQ(p["tags"][0].get<std::string>(), "t1");
  ASSERT_EQ(p["tags"][1].get<std::string>(), "t2");

  client->disconnect();
  return true;
}

TEST(relay_mock_send_message_with_media_only) {
  auto client = mt::make_client();
  Message msg =
      client->send_message("+15553334444", "+15551112222", "", {"https://media.example/cat.jpg"});
  ASSERT_FALSE(msg.message_id.empty());

  auto sends = mt::journal_recv("messaging.send");
  ASSERT_EQ(sends.size(), static_cast<size_t>(1));
  json p = sends.back().frame["params"];
  ASSERT_TRUE(p["media"].is_array());
  ASSERT_EQ(p["media"][0].get<std::string>(), "https://media.example/cat.jpg");
  // body should be absent or empty string
  if (p.contains("body")) {
    ASSERT_TRUE(p["body"].is_null() || p["body"].get<std::string>().empty());
  }
  client->disconnect();
  return true;
}

TEST(relay_mock_send_message_includes_explicit_context) {
  auto client = mt::make_client();
  client->send_message("+15553334444", "+15551112222", "hi", {}, {}, "", "custom-ctx");
  auto sends = mt::journal_recv("messaging.send");
  ASSERT_EQ(sends.size(), static_cast<size_t>(1));
  ASSERT_EQ(sends.back().frame["params"].value("context", ""), "custom-ctx");
  client->disconnect();
  return true;
}

TEST(relay_mock_send_message_returns_initial_state_queued) {
  auto client = mt::make_client();
  Message msg = client->send_message("+15553334444", "+15551112222", "hi");
  // After send, before any state event, state is "queued"
  ASSERT_EQ(msg.state(), "queued");
  ASSERT_FALSE(msg.is_terminal());
  client->disconnect();
  return true;
}

TEST(relay_mock_send_message_resolves_on_delivered) {
  auto client = mt::make_client();
  Message msg = client->send_message("+15553334444", "+15551112222", "hi");
  // Push the terminal delivered state.
  mt::push(messaging_state_frame(msg.message_id, "delivered"));

  bool ok = msg.wait(5000);
  ASSERT_TRUE(ok);
  ASSERT_EQ(msg.state(), "delivered");
  ASSERT_TRUE(msg.is_delivered());
  ASSERT_TRUE(msg.is_terminal());
  client->disconnect();
  return true;
}

TEST(relay_mock_send_message_resolves_on_undelivered) {
  auto client = mt::make_client();
  Message msg = client->send_message("+15553334444", "+15551112222", "hi");
  mt::push(messaging_state_frame(msg.message_id, "undelivered", "carrier_blocked"));

  bool ok = msg.wait(5000);
  ASSERT_TRUE(ok);
  ASSERT_EQ(msg.state(), "undelivered");
  ASSERT_TRUE(msg.is_failed());
  ASSERT_EQ(msg.reason(), "carrier_blocked");
  client->disconnect();
  return true;
}

TEST(relay_mock_send_message_resolves_on_failed) {
  auto client = mt::make_client();
  Message msg = client->send_message("+15553334444", "+15551112222", "hi");
  mt::push(messaging_state_frame(msg.message_id, "failed", "spam"));

  bool ok = msg.wait(5000);
  ASSERT_TRUE(ok);
  ASSERT_EQ(msg.state(), "failed");
  ASSERT_TRUE(msg.is_failed());
  client->disconnect();
  return true;
}

TEST(relay_mock_send_message_intermediate_state_does_not_resolve) {
  auto client = mt::make_client();
  Message msg = client->send_message("+15553334444", "+15551112222", "hi");
  mt::push(messaging_state_frame(msg.message_id, "sent"));

  bool propagated = spin_until_msg([&] { return msg.state() == "sent"; }, 2000);
  ASSERT_TRUE(propagated);
  ASSERT_FALSE(msg.is_terminal());
  client->disconnect();
  return true;
}

// ---------------------------------------------------------------------------
// Inbound messages
// ---------------------------------------------------------------------------

TEST(relay_mock_inbound_message_fires_on_message_handler) {
  auto client = mt::make_client();
  Message seen;
  std::atomic<bool> got{false};
  std::mutex m;
  std::condition_variable cv;

  client->on_message([&](const Message& msg) {
    std::lock_guard<std::mutex> lock(m);
    seen = msg;
    got.store(true);
    cv.notify_all();
  });

  json frame;
  frame["jsonrpc"] = "2.0";
  frame["id"] = fresh_uuid_msg();
  frame["method"] = "signalwire.event";
  frame["params"]["event_type"] = "messaging.receive";
  json& p = frame["params"]["params"];
  p["message_id"] = "in-msg-1";
  p["context"] = "default";
  p["direction"] = "inbound";
  p["from_number"] = "+15551110000";
  p["to_number"] = "+15552220000";
  p["body"] = "hello back";
  p["media"] = json::array();
  p["segments"] = 1;
  p["message_state"] = "received";
  p["tags"] = json::array({"incoming"});

  mt::push(frame);

  {
    std::unique_lock<std::mutex> lock(m);
    cv.wait_for(lock, std::chrono::seconds(5), [&] { return got.load(); });
  }
  ASSERT_TRUE(got.load());
  ASSERT_EQ(seen.message_id, "in-msg-1");
  ASSERT_EQ(seen.direction, "inbound");
  ASSERT_EQ(seen.from_number, "+15551110000");
  ASSERT_EQ(seen.to_number, "+15552220000");
  ASSERT_EQ(seen.body, "hello back");
  ASSERT_EQ(seen.tags.size(), static_cast<size_t>(1));
  ASSERT_EQ(seen.tags[0], "incoming");
  // The mock has always pushed `context` + `segments` on messaging.receive;
  // the port dropped both, so an inbound handler could not tell which context
  // the message arrived on nor how many segments the carrier used.
  ASSERT_EQ(seen.context, "default");
  ASSERT_EQ(seen.segments, 1);
  client->disconnect();
  return true;
}

// ---------------------------------------------------------------------------
// State progression — full pipeline
// ---------------------------------------------------------------------------

TEST(relay_mock_full_message_state_progression) {
  auto client = mt::make_client();
  Message msg = client->send_message("+15553334444", "+15551112222", "full pipeline");

  mt::push(messaging_state_frame(msg.message_id, "sent"));
  bool sent_seen = spin_until_msg([&] { return msg.state() == "sent"; }, 2000);
  ASSERT_TRUE(sent_seen);

  mt::push(messaging_state_frame(msg.message_id, "delivered"));
  bool ok = msg.wait(5000);
  ASSERT_TRUE(ok);
  ASSERT_EQ(msg.state(), "delivered");
  client->disconnect();
  return true;
}
