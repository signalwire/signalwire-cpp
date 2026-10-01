// ChatGateway + HandoffRouter (signalwire-python ai_chat/gateway.py and
// ai_chat/handoff.py; tests ported from tests/unit/ai_chat/test_gateway.py and
// test_handoff.py). The HTTP routes are covered against the shared mock by the
// AI-CHAT-GATEWAY gate (tools/ai_chat_gateway_dump.cpp); these pin the logic.

#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "signalwire/ai_chat/gateway.hpp"
#include "signalwire/ai_chat/handoff.hpp"

namespace gw_test {

using signalwire::ai_chat::AIChatClient;
using signalwire::ai_chat::AIChatClientOptions;
using signalwire::ai_chat::ChatGateway;
using signalwire::ai_chat::ChatGatewayOptions;
using signalwire::ai_chat::GatewayRejection;

inline std::shared_ptr<AIChatClient> client() {
  AIChatClientOptions o;
  o.project = "p";
  o.token = "t";
  o.url = "https://service.example.invalid/aichat";
  return std::make_shared<AIChatClient>(o);
}

inline ChatGatewayOptions options() {
  ChatGatewayOptions o;
  o.config_url = "https://agent.example.com/swml";
  o.key = "pk_test";
  o.secret = std::string(32, 's');
  o.allowed_origins = {"https://shop.example.com"};
  o.client = client();
  return o;
}

// The status of the GatewayRejection ``fn`` throws, or 0 when it does not.
template <typename F>
int rejection_status(F fn) {
  try {
    fn();
  } catch (const GatewayRejection& rej) {
    return rej.status();
  }
  return 0;
}

}  // namespace gw_test

using gw_test::ChatGateway;
using json = nlohmann::json;

TEST(gateway_a_handle_round_trips) {
  ChatGateway gw(gw_test::options());
  ASSERT_EQ(gw.read_handle(gw.mint_handle(std::string("conv-1"))), std::string("conv-1"));
  return true;
}

TEST(gateway_a_forged_or_foreign_handle_is_refused) {
  ChatGateway gw(gw_test::options());
  std::string handle = gw.mint_handle(std::string("conv-1"));
  std::string forged = handle.substr(0, handle.find('.')) + ".AAAA";
  ASSERT_EQ(gw_test::rejection_status([&] { (void)gw.read_handle(forged); }), 403);
  auto other_opts = gw_test::options();
  other_opts.secret = "another-secret";
  ChatGateway other(other_opts);
  ASSERT_EQ(gw_test::rejection_status([&] { (void)other.read_handle(handle); }), 403);
  return true;
}

TEST(gateway_an_expired_handle_is_refused) {
  auto o = gw_test::options();
  o.handle_ttl = -10;
  ChatGateway gw(o);
  ASSERT_EQ(gw_test::rejection_status([&] { (void)gw.read_handle(gw.mint_handle()); }), 403);
  return true;
}

TEST(gateway_garbage_is_refused_without_leaking_why) {
  ChatGateway gw(gw_test::options());
  for (const std::string& junk : {"", "nodot", "a.b.c", "x"}) {
    int status = gw_test::rejection_status([&] { (void)gw.read_handle(junk); });
    ASSERT_TRUE(status == 400 || status == 403);
  }
  return true;
}

TEST(gateway_origins_localhost_listed_missing_and_unlisted) {
  ChatGateway gw(gw_test::options());
  for (const std::string& origin :
       {"http://localhost:3000", "http://127.0.0.1", "http://[::1]:8080", "http://app.localhost"}) {
    ASSERT_EQ(gw_test::rejection_status([&] { gw.check_origin(origin); }), 0);
  }
  ASSERT_EQ(
      gw_test::rejection_status([&] { gw.check_origin(std::string("https://shop.example.com/")); }),
      0);
  ASSERT_EQ(gw_test::rejection_status([&] { gw.check_origin(std::nullopt); }), 0);
  ASSERT_EQ(
      gw_test::rejection_status([&] { gw.check_origin(std::string("https://evil.example")); }),
      403);
  return true;
}

TEST(gateway_the_key_is_required) {
  ChatGateway gw(gw_test::options());
  ASSERT_EQ(gw_test::rejection_status([&] { gw.check_key(std::nullopt); }), 401);
  ASSERT_EQ(gw_test::rejection_status([&] { gw.check_key(std::string("pk_wrong")); }), 401);
  ASSERT_EQ(gw_test::rejection_status([&] { gw.check_key(std::string("pk_test")); }), 0);
  return true;
}

TEST(gateway_prepare_config_url_and_conversation_are_ours) {
  ChatGateway gw(gw_test::options());
  auto [method, params, minted] =
      gw.prepare(json{{"message", "hi"}, {"config_url", "https://evil"}, {"id", "victim"}},
                 std::nullopt, std::string("pk_test"));
  ASSERT_EQ(method, std::string("chat"));
  ASSERT_EQ(params["config_url"], json("https://agent.example.com/swml"));
  ASSERT_TRUE(minted.has_value());
  ASSERT_EQ(params["id"], json(gw.read_handle(*minted)));
  ASSERT_NE(params["id"], json("victim"));
  return true;
}

TEST(gateway_prepare_methods_and_handles) {
  ChatGateway gw(gw_test::options());
  const auto key = std::optional<std::string>("pk_test");
  ASSERT_EQ(gw_test::rejection_status(
                [&] { (void)gw.prepare(json{{"method", "summarize"}}, std::nullopt, key); }),
            400);
  ASSERT_EQ(gw_test::rejection_status(
                [&] { (void)gw.prepare(json{{"method", "end"}}, std::nullopt, key); }),
            400);
  ASSERT_EQ(gw_test::rejection_status(
                [&] { (void)gw.prepare(json{{"method", "log"}}, std::nullopt, key); }),
            400);
  std::string handle = gw.mint_handle(std::string("conv-9"));
  auto [m1, p1, minted1] =
      gw.prepare(json{{"method", "end"}, {"handle", handle}}, std::nullopt, key);
  ASSERT_EQ(m1, std::string("end_conversation"));
  ASSERT_EQ(p1, json({{"id", "conv-9"}}));
  ASSERT_FALSE(minted1.has_value());
  auto [m2, p2, minted2] =
      gw.prepare(json{{"method", "log"}, {"handle", handle}}, std::nullopt, key);
  ASSERT_EQ(m2, std::string("chat_log"));
  ASSERT_EQ(p2, json({{"id", "conv-9"}}));
  auto [m3, p3, minted3] = gw.prepare(json{{"method", "start"}}, std::nullopt, key);
  ASSERT_EQ(m3, std::string("create_conversation"));
  ASSERT_TRUE(minted3.has_value());
  ASSERT_FALSE(p3.contains("message"));
  auto [m4, p4, minted4] =
      gw.prepare(json{{"message", "again"}, {"handle", handle}}, std::nullopt, key);
  ASSERT_EQ(m4, std::string("chat"));
  ASSERT_FALSE(minted4.has_value());
  ASSERT_EQ(gw_test::rejection_status([&] {
              (void)gw.prepare(json{{"message", "   "}, {"handle", handle}}, std::nullopt, key);
            }),
            400);
  return true;
}

TEST(gateway_minting_and_turns_are_capped) {
  auto o = gw_test::options();
  o.max_new_conversations = 2;
  o.max_turns = 2;
  ChatGateway gw(o);
  const auto key = std::optional<std::string>("pk_test");
  (void)gw.prepare(json{{"method", "start"}}, std::nullopt, key);
  (void)gw.prepare(json{{"method", "start"}}, std::nullopt, key);
  ASSERT_EQ(gw_test::rejection_status(
                [&] { (void)gw.prepare(json{{"method", "start"}}, std::nullopt, key); }),
            429);
  std::string a = gw.mint_handle(std::string("conv-a"));
  std::string b = gw.mint_handle(std::string("conv-b"));
  (void)gw.prepare(json{{"message", "1"}, {"handle", a}}, std::nullopt, key);
  (void)gw.prepare(json{{"message", "2"}, {"handle", a}}, std::nullopt, key);
  ASSERT_EQ(gw_test::rejection_status([&] {
              (void)gw.prepare(json{{"message", "3"}, {"handle", a}}, std::nullopt, key);
            }),
            429);
  // One conversation hitting its cap does not stop another.
  (void)gw.prepare(json{{"message", "1"}, {"handle", b}}, std::nullopt, key);
  return true;
}

TEST(gateway_page_context_is_validated_bounded_and_forwarded) {
  ChatGateway gw(gw_test::options());
  const auto key = std::optional<std::string>("pk_test");
  json page = {{"page", {{"url", "https://shop.example.com/pricing"}}}};
  auto [m, p, minted] =
      gw.prepare(json{{"method", "start"}, {"user_meta_data", page}}, std::nullopt, key);
  ASSERT_EQ(p["user_meta_data"], page);
  ASSERT_FALSE(gw.read_user_metadata(json{{"user_meta_data", json::object()}}).has_value());
  ASSERT_EQ(gw_test::rejection_status(
                [&] { (void)gw.read_user_metadata(json{{"user_meta_data", json::array({"x"})}}); }),
            400);
  json big = {{"blob", std::string(signalwire::ai_chat::MAX_USER_METADATA_BYTES, 'x')}};
  ASSERT_EQ(gw_test::rejection_status(
                [&] { (void)gw.read_user_metadata(json{{"user_meta_data", big}}); }),
            413);
  return true;
}

TEST(gateway_message_limit_counts_utf8_bytes_and_mints_nothing) {
  auto o = gw_test::options();
  o.max_new_conversations = 1;
  ChatGateway gw(o);
  const auto key = std::optional<std::string>("pk_test");
  std::string over(signalwire::ai_chat::MAX_MESSAGE_BYTES + 1, 'x');
  ASSERT_EQ(gw_test::rejection_status(
                [&] { (void)gw.prepare(json{{"message", over}}, std::nullopt, key); }),
            413);
  // 3-byte characters: under the limit in characters, over it in bytes.
  std::string wide;
  for (size_t i = 0; i < signalwire::ai_chat::MAX_MESSAGE_BYTES / 3 + 1; ++i) {
    wide += "\xE2\x82\xAC";  // U+20AC
  }
  ASSERT_EQ(gw_test::rejection_status(
                [&] { (void)gw.prepare(json{{"message", wide}}, std::nullopt, key); }),
            413);
  // Neither refusal consumed the single conversation slot.
  std::string at(signalwire::ai_chat::MAX_MESSAGE_BYTES, 'x');
  (void)gw.prepare(json{{"message", at}}, std::nullopt, key);
  return true;
}

TEST(gateway_transcript_shows_only_dialogue_in_seconds) {
  std::vector<json> log = std::vector<json>({
      {{"role", "system"}, {"content", "secret prompt"}, {"timestamp", 1000000}},
      {{"role", "user"}, {"content", "hi"}, {"timestamp", 2000000}},
      {{"role", "assistant"}, {"content", ""}, {"tool_calls", json::array()}},
      {{"role", "tool"}, {"content", "result"}, {"timestamp", 9000000}},
      {{"role", "assistant"}, {"content", "hello"}, {"timestamp", 3000000}, {"id", "m1"}},
      "junk",
  });
  json visible = ChatGateway::visible_messages(log);
  ASSERT_EQ(visible,
            json::array({{{"role", "user"}, {"content", "hi"}, {"timestamp", 2.0}},
                         {{"role", "assistant"}, {"content", "hello"}, {"timestamp", 3.0}}}));
  // last_activity counts every role.
  auto last = ChatGateway::last_activity(log);
  ASSERT_TRUE(last.has_value() && *last == 9.0);
  ASSERT_FALSE(ChatGateway::last_activity(std::vector<json>{json{{"role", "user"}}}).has_value());
  ChatGateway gw(gw_test::options());
  ASSERT_EQ(gw.effective_timeout(), signalwire::ai_chat::SERVICE_DEFAULT_CONVERSATION_TIMEOUT);
  return true;
}

// ---- HandoffRouter -----------------------------------------------------------

namespace handoff_test {

using signalwire::ai_chat::HandoffRouter;
using signalwire::ai_chat::HandoffRouterOptions;

struct Fixture {
  std::shared_ptr<std::vector<std::vector<std::string>>> events =
      std::make_shared<std::vector<std::vector<std::string>>>();
  ChatGateway gateway{gw_test::options()};

  HandoffRouterOptions options() {
    auto ev = events;
    HandoffRouterOptions o;
    o.capture_leg = [ev](const std::string& conversation_id, const std::string& medium) {
      ev->push_back({"capture", conversation_id, medium});
      return true;
    };
    o.end_call = [ev](const std::string& call_id) { ev->push_back({"end_call", call_id}); };
    o.send_message = [ev](const std::string& call_id, const std::string& text) {
      ev->push_back({"say", call_id, text});
      return true;
    };
    return o;
  }
};

}  // namespace handoff_test

TEST(handoff_redeem_ends_the_call_before_capture_and_mints_a_fresh_leg) {
  handoff_test::Fixture f;
  handoff_test::HandoffRouter h(f.gateway, f.options());
  h.register_("n1", "conv-root", std::string("call-9"));
  auto handle = h.redeem("n1");
  ASSERT_TRUE(handle.has_value());
  ASSERT_EQ(f.gateway.read_handle(*handle), std::string("conv-root.1"));
  ASSERT_EQ(*f.events, (std::vector<std::vector<std::string>>{{"end_call", "call-9"},
                                                              {"capture", "conv-root", "voice"}}));
  return true;
}

TEST(handoff_leg_ids_increment) {
  handoff_test::Fixture f;
  handoff_test::HandoffRouter h(f.gateway, f.options());
  ASSERT_EQ(h.next_conversation_id()("root"), std::string("root.1"));
  ASSERT_EQ(h.next_conversation_id()("root.2"), std::string("root.3"));
  ASSERT_EQ(h.next_conversation_id()("root.x"), std::string("root.x.1"));
  return true;
}

TEST(handoff_a_nonce_is_single_use_and_unknown_looks_the_same) {
  handoff_test::Fixture f;
  handoff_test::HandoffRouter h(f.gateway, f.options());
  h.register_("n1", "conv-root", std::string("call-9"));
  ASSERT_TRUE(h.redeem("n1").has_value());
  ASSERT_FALSE(h.redeem("n1").has_value());
  ASSERT_FALSE(h.redeem("never").has_value());
  // A redeemed nonce cannot be registered and redeemed again, or type.
  h.register_("n1", "conv-root", std::string("call-9"));
  ASSERT_FALSE(h.redeem("n1").has_value());
  ASSERT_FALSE(h.say("n1", "hello"));
  return true;
}

TEST(handoff_expired_nonces_are_not_redeemable) {
  handoff_test::Fixture f;
  auto o = f.options();
  o.nonce_ttl = -1;
  handoff_test::HandoffRouter h(f.gateway, o);
  h.register_("n1", "conv-root", std::string("call-9"));
  ASSERT_FALSE(h.redeem("n1").has_value());
  return true;
}

TEST(handoff_a_live_nonce_cannot_be_moved_to_another_call) {
  handoff_test::Fixture f;
  handoff_test::HandoffRouter h(f.gateway, f.options());
  h.register_("n1", "conv-root", std::string("call-9"));
  h.register_("n1", "conv-root", std::string("call-evil"));
  ASSERT_TRUE(h.say("n1", "hi"));
  ASSERT_EQ(f.events->back(), (std::vector<std::string>{"say", "call-9", "hi"}));
  return true;
}

TEST(handoff_say_trims_repeats_and_is_capped) {
  handoff_test::Fixture f;
  auto o = f.options();
  o.max_messages_per_call = 2;
  handoff_test::HandoffRouter h(f.gateway, o);
  h.register_("n2", "conv-root", std::string("call-9"));
  ASSERT_TRUE(h.say("n2", "  hello there  "));
  ASSERT_EQ(f.events->back(), (std::vector<std::string>{"say", "call-9", "hello there"}));
  ASSERT_FALSE(h.say("n2", "   "));
  ASSERT_TRUE(h.say("n2", "again"));
  ASSERT_FALSE(h.say("n2", "third"));
  ASSERT_FALSE(h.say("guessed", "hello"));
  ASSERT_FALSE(h.say("n2", std::string(signalwire::ai_chat::MAX_MESSAGE_BYTES + 1, 'x')));
  return true;
}

TEST(handoff_a_failed_delivery_gives_its_slot_back) {
  handoff_test::Fixture f;
  auto o = f.options();
  o.max_messages_per_call = 1;
  bool fail = true;
  o.send_message = [&fail](const std::string&, const std::string&) -> bool {
    if (fail) {
      throw std::runtime_error("down");
    }
    return true;
  };
  handoff_test::HandoffRouter h(f.gateway, o);
  h.register_("n2", "conv-root", std::string("call-9"));
  ASSERT_FALSE(h.say("n2", "hi"));
  fail = false;
  ASSERT_TRUE(h.say("n2", "hi"));
  return true;
}

TEST(handoff_disabled_when_no_sender_is_configured) {
  handoff_test::Fixture f;
  auto o = f.options();
  o.send_message = nullptr;
  handoff_test::HandoffRouter h(f.gateway, o);
  h.register_("n2", "conv-root", std::string("call-9"));
  ASSERT_FALSE(h.say("n2", "hi"));
  return true;
}

TEST(handoff_escalate_captures_the_chat_leg_and_refuses_forgeries) {
  handoff_test::Fixture f;
  handoff_test::HandoffRouter h(f.gateway, f.options());
  ASSERT_TRUE(h.escalate(f.gateway.mint_handle(std::string("conv-root.5"))));
  ASSERT_EQ(f.events->back(), (std::vector<std::string>{"capture", "conv-root.5", "chat"}));
  ASSERT_FALSE(h.escalate("forged"));
  return true;
}

TEST(handoff_a_capture_timeout_or_failure_does_not_block_the_switch) {
  handoff_test::Fixture f;
  auto o = f.options();
  o.capture_timeout = 0.05;
  o.capture_leg = [](const std::string&, const std::string&) -> bool {
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    return true;
  };
  handoff_test::HandoffRouter slow(f.gateway, o);
  slow.register_("n1", "conv-root", std::string("call-9"));
  ASSERT_TRUE(slow.redeem("n1").has_value());
  o.capture_leg = [](const std::string&, const std::string&) -> bool {
    throw std::runtime_error("boom");
  };
  handoff_test::HandoffRouter raising(f.gateway, o);
  raising.register_("n3", "conv-root", std::string("call-9"));
  ASSERT_TRUE(raising.redeem("n3").has_value());
  // Let the timed-out capture thread finish before the test returns.
  std::this_thread::sleep_for(std::chrono::milliseconds(350));
  return true;
}
