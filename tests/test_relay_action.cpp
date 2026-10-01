// RELAY Action split tests — state transitions, callbacks, threading
#include <thread>

#include "signalwire/relay/action.hpp"
using namespace signalwire::relay;
using json = nlohmann::json;

TEST(relay_action_default_empty) {
  Action action;
  ASSERT_EQ(action.control_id(), "");
  ASSERT_EQ(action.state(), "");
  ASSERT_FALSE(action.completed());
  return true;
}

TEST(relay_action_with_control_id) {
  Action action("ctl-abc");
  ASSERT_EQ(action.control_id(), "ctl-abc");
  return true;
}

TEST(relay_action_full_construction) {
  Action action("ctl-1", nullptr, "call-1", "node-1");
  ASSERT_EQ(action.control_id(), "ctl-1");
  ASSERT_EQ(action.call_id(), "call-1");
  ASSERT_EQ(action.node_id(), "node-1");
  return true;
}

TEST(relay_action_playing_not_completed) {
  Action action("ctl-2");
  action.update_state("playing");
  ASSERT_EQ(action.state(), "playing");
  ASSERT_FALSE(action.completed());
  return true;
}

TEST(relay_action_finished_is_completed) {
  Action action("ctl-3");
  action.update_state("finished");
  ASSERT_TRUE(action.completed());
  return true;
}

TEST(relay_action_error_is_completed) {
  Action action("ctl-4");
  action.update_state("error", json::object({{"reason", "timeout"}}));
  ASSERT_TRUE(action.completed());
  ASSERT_EQ(action.result()["reason"].get<std::string>(), "timeout");
  return true;
}

TEST(relay_action_no_input_completed) {
  Action action("ctl-5");
  action.update_state("no_input");
  ASSERT_TRUE(action.completed());
  return true;
}

TEST(relay_action_no_match_completed) {
  Action action("ctl-6");
  action.update_state("no_match");
  ASSERT_TRUE(action.completed());
  return true;
}

TEST(relay_action_resolve_with_data) {
  Action action("ctl-7");
  action.resolve("finished", json::object({{"url", "recording.mp3"}}));
  ASSERT_TRUE(action.completed());
  ASSERT_EQ(action.state(), "finished");
  ASSERT_EQ(action.result()["url"].get<std::string>(), "recording.mp3");
  return true;
}

TEST(relay_action_callback_on_complete) {
  Action action("ctl-8");
  bool fired = false;
  action.on_completed([&](const Action& a) {
    fired = true;
    (void)a;
  });
  action.update_state("finished");
  ASSERT_TRUE(fired);
  return true;
}

TEST(relay_action_callback_immediate_if_already_done) {
  Action action("ctl-9");
  action.resolve("error");
  bool fired = false;
  action.on_completed([&](const Action&) { fired = true; });
  ASSERT_TRUE(fired);
  return true;
}

TEST(relay_action_callback_exception_safe) {
  Action action("ctl-10");
  action.on_completed([](const Action&) { throw std::runtime_error("boom"); });
  action.update_state("finished");
  ASSERT_TRUE(action.completed());
  return true;
}

TEST(relay_action_wait_already_done) {
  Action action("ctl-11");
  action.resolve("finished");
  ASSERT_TRUE(action.wait(100));
  return true;
}

TEST(relay_action_wait_timeout_expires) {
  Action action("ctl-12");
  ASSERT_FALSE(action.wait(50));
  return true;
}

TEST(relay_action_wait_from_thread) {
  Action action("ctl-13");
  std::thread t([&action]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    action.resolve("finished", json::object({{"ok", true}}));
  });
  ASSERT_TRUE(action.wait(5000));
  ASSERT_TRUE(action.completed());
  t.join();
  return true;
}

TEST(relay_action_stop_pause_resume_without_client) {
  Action action("ctl-14");
  action.stop();
  action.pause();
  action.resume();
  return true;
}

// Reference parity: Action.__init__(call, control_id, …) stores `self.call`
// and `self.control_id` as public instance attributes — a handler holding an
// Action reaches its Call through them. The port carried control_id but had no
// way back to the Call at all.
TEST(relay_action_call_backreference_and_control_id) {
  Action bare("ctl-42");
  ASSERT_EQ(bare.control_id(), "ctl-42");
  // No client attached: there is no registry to resolve through.
  ASSERT_TRUE(bare.call() == nullptr);
  return true;
}
