// Copyright (c) 2026 SignalWire
// SPDX-License-Identifier: MIT
//
// Unit tests for signalwire::ai_chat::AIChatClient. Mirrors the TypeScript
// template (tests/ai-chat/AIChatClient.test.ts) and the python reference: URL
// resolution, HTTP Basic auth with the project as the username, identity never
// in params, wire-method + param mapping, decoded result shapes, JSON-RPC error
// mapping, and — critically — that summarize's {error} one_of branch RAISES
// SummaryError instead of returning an empty string.
//
// Each wire test stands up a tiny in-process httplib::Server on a free port that
// behaves like porting-sdk's mock_ai_chat: it records every request (method,
// params, Authorization header) and returns a canned success envelope, a
// sentinel-driven JSON-RPC error, or the summarize {error} success-envelope
// branch. This keeps the tests hermetic (no porting-sdk mock dependency at unit
// time; the AI-CHAT gate covers the real shared mock).

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "httplib.h"
#include "signalwire/ai_chat/ai_chat_client.hpp"
#include "signalwire/common.hpp"

namespace {

using signalwire::ai_chat::AIChatClient;
using signalwire::ai_chat::AIChatClientOptions;
using signalwire::ai_chat::AIChatError;
using signalwire::ai_chat::AuthenticationError;
using signalwire::ai_chat::ChatInProgressError;
using signalwire::ai_chat::ChatLog;
using signalwire::ai_chat::ChatOptions;
using signalwire::ai_chat::ChatResponse;
using signalwire::ai_chat::ConversationInfo;
using signalwire::ai_chat::ConversationNotFoundError;
using signalwire::ai_chat::CreateConversationOptions;
using signalwire::ai_chat::RateLimitError;
using signalwire::ai_chat::SummarizeOptions;
using signalwire::ai_chat::SummaryError;
using json = nlohmann::json;

// One recorded wire request: the JSON-RPC method + params the client put on the
// wire, plus the decoded Basic-auth username (so tests can assert the project
// is the username and identity never leaked into params).
struct RecordedRequest {
  std::string method;
  json params;
  std::string authorization;
  std::string basic_user;
};

// A tiny in-process AI-Chat mock. Records each request and responds like
// porting-sdk's mock_ai_chat: canned success per method, "__err_<code>" ids
// yield a JSON-RPC error, and the "__summarize_error" id yields the {error}
// success-envelope branch.
class AiChatMock {
 public:
  AiChatMock() {
    canned_["create_conversation"] = {
        {"status", "created"}, {"id", "conv-1"}, {"initial_message", "hello"}};
    canned_["chat"] = {{"response", "hi there"},
                       {"user_event", {{"event_type", "demo"}, {"n", 1}}}};
    canned_["end_conversation"] = {{"status", "ended"}, {"id", "conv-1"}};
    canned_["delete"] = {{"status", "deleted"}, {"id", "conv-1"}};
    canned_["chat_log"] = {{"chat_log", json::array({{{"role", "user"}, {"content", "m"}}})},
                           {"call_timeline", json::array({{{"t", 1}}})}};
    canned_["summarize"] = {{"summary", "a concise summary"}};

    server_.Post("/api/ai/chat",
                 [this](const httplib::Request& req, httplib::Response& res) { handle(req, res); });

    port_ = server_.bind_to_any_port("127.0.0.1");
    thread_ = std::thread([this]() { server_.listen_after_bind(); });
    server_.wait_until_ready();
  }

  ~AiChatMock() {
    server_.stop();
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  std::string url() const { return "http://127.0.0.1:" + std::to_string(port_) + "/api/ai/chat"; }

  std::vector<RecordedRequest> requests() {
    std::lock_guard<std::mutex> lk(mu_);
    return requests_;
  }

 private:
  static std::string basic_user(const std::string& auth) {
    if (auth.rfind("Basic ", 0) != 0) {
      return "";
    }
    std::string decoded = signalwire::base64_decode(auth.substr(6));
    auto colon = decoded.find(':');
    return colon == std::string::npos ? decoded : decoded.substr(0, colon);
  }

  void send(httplib::Response& res, const json& body, int status = 200) {
    res.status = status;
    res.set_content(body.dump(), "application/json");
  }

  void handle(const httplib::Request& req, httplib::Response& res) {
    std::string auth = req.get_header_value("Authorization");
    std::string user = basic_user(auth);
    json data = json::parse(req.body, nullptr, false);
    json rid = (data.is_object() && data.contains("id")) ? data["id"] : json(nullptr);

    if (user.empty()) {
      send(res,
           {{"jsonrpc", "2.0"},
            {"error", {{"code", -32009}, {"message", "auth required"}}},
            {"id", rid}},
           401);
      return;
    }

    std::string method = data.value("method", "");
    json params = (data.is_object() && data.contains("params") && data["params"].is_object())
                      ? data["params"]
                      : json::object();

    {
      std::lock_guard<std::mutex> lk(mu_);
      requests_.push_back({method, params, auth, user});
    }

    std::string cid = params.value("id", "");
    if (cid.rfind("__err_", 0) == 0) {
      int code = std::stoi(cid.substr(std::string("__err_").size()));
      send(res,
           {{"jsonrpc", "2.0"},
            {"error", {{"code", code}, {"message", "forced error"}}},
            {"id", rid}},
           code == -32009 ? 401 : 200);
      return;
    }

    if (method == "summarize" && cid == "__summarize_error") {
      send(
          res,
          {{"jsonrpc", "2.0"}, {"result", {{"error", "Failed to generate summary"}}}, {"id", rid}});
      return;
    }

    json result = canned_.count(method) ? canned_[method] : json::object();
    send(res, {{"jsonrpc", "2.0"}, {"result", result}, {"id", rid}});
  }

  httplib::Server server_;
  int port_ = 0;
  std::thread thread_;
  std::mutex mu_;
  std::vector<RecordedRequest> requests_;
  std::map<std::string, json> canned_;
};

AIChatClient make_client(const std::string& url) {
  AIChatClientOptions opts;
  opts.project = "proj-1";
  opts.token = "tok-1";
  opts.url = url;
  return AIChatClient(opts);
}

const std::vector<std::string> kForbiddenInParams = {"project_id", "project",  "token",
                                                     "api_token",  "space_id", "space"};

}  // namespace

// ── construction / URL resolution ──────────────────────────────────────

TEST(ai_chat_requires_project) {
  AIChatClientOptions opts;
  opts.url = "http://x";
  // Ensure the env fallback can't satisfy it.
  ::unsetenv("SIGNALWIRE_PROJECT_ID");
  ASSERT_THROWS(AIChatClient{opts});
  return true;
}

TEST(ai_chat_builds_space_url) {
  AIChatClientOptions opts;
  opts.project = "p";
  opts.token = "t";
  opts.space = "myspace";
  AIChatClient c(opts);
  ASSERT_EQ(c.url(), std::string("https://myspace.signalwire.com/api/ai/chat"));
  return true;
}

TEST(ai_chat_uses_explicit_url_verbatim) {
  AIChatClientOptions opts;
  opts.project = "p";
  opts.token = "t";
  opts.url = "http://local/api/ai/chat";
  AIChatClient c(opts);
  ASSERT_EQ(c.url(), std::string("http://local/api/ai/chat"));
  return true;
}

TEST(ai_chat_throws_when_no_url_or_space) {
  AIChatClientOptions opts;
  opts.project = "p";
  opts.token = "t";
  ::unsetenv("SIGNALWIRE_SPACE");
  ::unsetenv("RAILS_DEV_MODE");
  ASSERT_THROWS(AIChatClient{opts});
  return true;
}

// ── wire behavior ──────────────────────────────────────────────────────

TEST(ai_chat_basic_auth_project_username_identity_not_in_params) {
  AiChatMock mock;
  AIChatClient client = make_client(mock.url());
  CreateConversationOptions co;
  co.config_url = "http://cfg";
  co.timeout = 30;
  co.reinit = true;
  (void)client.create_conversation("conv-1", co);

  auto reqs = mock.requests();
  ASSERT_EQ(reqs.size(), static_cast<size_t>(1));
  ASSERT_EQ(reqs[0].basic_user, std::string("proj-1"));
  for (const auto& key : kForbiddenInParams) {
    ASSERT_FALSE(reqs[0].params.contains(key));
  }
  return true;
}

TEST(ai_chat_create_maps_timeout_and_decodes) {
  AiChatMock mock;
  AIChatClient client = make_client(mock.url());
  CreateConversationOptions co;
  co.config_url = "http://cfg";
  co.timeout = 30;
  co.reinit = true;
  ConversationInfo info = client.create_conversation("conv-1", co);

  auto reqs = mock.requests();
  ASSERT_EQ(reqs[0].method, std::string("create_conversation"));
  ASSERT_EQ(reqs[0].params.value("id", ""), std::string("conv-1"));
  ASSERT_EQ(reqs[0].params.value("config_url", ""), std::string("http://cfg"));
  ASSERT_EQ(reqs[0].params.value("conversation_timeout", 0), 30);
  ASSERT_TRUE(reqs[0].params.value("reinit", false));
  ASSERT_EQ(info.id, std::string("conv-1"));
  ASSERT_EQ(info.status, std::string("created"));
  ASSERT_TRUE(info.has_initial_message);
  ASSERT_EQ(info.initial_message, std::string("hello"));
  return true;
}

TEST(ai_chat_chat_default_role_and_decode) {
  AiChatMock mock;
  AIChatClient client = make_client(mock.url());
  ChatOptions ch;
  ch.timeout = 30;
  ch.reinit = true;
  ChatResponse reply = client.chat("conv-1", "hello", ch);

  auto reqs = mock.requests();
  ASSERT_EQ(reqs[0].method, std::string("chat"));
  ASSERT_EQ(reqs[0].params.value("id", ""), std::string("conv-1"));
  ASSERT_EQ(reqs[0].params.value("message", ""), std::string("hello"));
  ASSERT_EQ(reqs[0].params.value("role", ""), std::string("user"));
  ASSERT_EQ(reqs[0].params.value("conversation_timeout", 0), 30);
  ASSERT_TRUE(reqs[0].params.value("reinit", false));
  ASSERT_EQ(reply.text, std::string("hi there"));
  ASSERT_EQ(reply.conversation_id, std::string("conv-1"));
  ASSERT_TRUE(reply.user_event.is_object());
  ASSERT_EQ(reply.user_event.value("event_type", ""), std::string("demo"));
  return true;
}

TEST(ai_chat_end_true_on_ended) {
  AiChatMock mock;
  AIChatClient client = make_client(mock.url());
  ASSERT_TRUE(client.end("conv-1"));
  ASSERT_EQ(mock.requests()[0].method, std::string("end_conversation"));
  return true;
}

TEST(ai_chat_delete_true_on_deleted) {
  AiChatMock mock;
  AIChatClient client = make_client(mock.url());
  ASSERT_TRUE(client.del("conv-1"));
  ASSERT_EQ(mock.requests()[0].method, std::string("delete"));
  return true;
}

TEST(ai_chat_log_decodes_messages_and_timeline) {
  AiChatMock mock;
  AIChatClient client = make_client(mock.url());
  ChatLog log = client.log("conv-1");
  ASSERT_EQ(mock.requests()[0].method, std::string("chat_log"));
  ASSERT_EQ(log.messages.size(), static_cast<size_t>(1));
  ASSERT_EQ(log.messages[0].value("role", ""), std::string("user"));
  ASSERT_EQ(log.call_timeline.size(), static_cast<size_t>(1));
  ASSERT_EQ(log.call_timeline[0].value("t", 0), 1);
  return true;
}

TEST(ai_chat_summarize_returns_summary) {
  AiChatMock mock;
  AIChatClient client = make_client(mock.url());
  ASSERT_EQ(client.summarize("conv-1"), std::string("a concise summary"));
  return true;
}

TEST(ai_chat_summarize_passes_sampling_params) {
  AiChatMock mock;
  AIChatClient client = make_client(mock.url());
  SummarizeOptions so;
  so.summary_prompt = "be brief";
  so.has_temperature = true;
  so.temperature = 0.2;
  so.has_max_tokens = true;
  so.max_tokens = 64;
  (void)client.summarize("conv-1", so);

  auto reqs = mock.requests();
  ASSERT_EQ(reqs[0].params.value("summary_prompt", ""), std::string("be brief"));
  ASSERT_TRUE(reqs[0].params.contains("temperature"));
  ASSERT_EQ(reqs[0].params.value("max_tokens", 0), 64);
  return true;
}

// ── summarize one_of {error} branch: MUST raise, never empty string ────

TEST(ai_chat_summarize_error_branch_raises_summary_error) {
  AiChatMock mock;
  AIChatClient client = make_client(mock.url());
  bool raised_summary_error = false;
  try {
    (void)client.summarize("__summarize_error");
  } catch (const SummaryError& e) {
    raised_summary_error = true;
    ASSERT_FALSE(e.has_code());  // rode the success envelope: no JSON-RPC code
    ASSERT_EQ(e.server_message(), std::string("Failed to generate summary"));
  }
  ASSERT_TRUE(raised_summary_error);
  return true;
}

// ── JSON-RPC error mapping ──────────────────────────────────────────────

TEST(ai_chat_maps_notfound) {
  AiChatMock mock;
  AIChatClient client = make_client(mock.url());
  bool ok = false;
  try {
    (void)client.chat("__err_-32001", "x");
  } catch (const ConversationNotFoundError& e) {
    ok = e.code() == -32001;
  }
  ASSERT_TRUE(ok);
  return true;
}

TEST(ai_chat_maps_ratelimit) {
  AiChatMock mock;
  AIChatClient client = make_client(mock.url());
  bool ok = false;
  try {
    (void)client.chat("__err_-32005", "x");
  } catch (const RateLimitError& e) {
    ok = e.code() == -32005;
  }
  ASSERT_TRUE(ok);
  return true;
}

TEST(ai_chat_maps_inprogress) {
  AiChatMock mock;
  AIChatClient client = make_client(mock.url());
  bool ok = false;
  try {
    (void)client.chat("__err_-32007", "x");
  } catch (const ChatInProgressError& e) {
    ok = e.code() == -32007;
  }
  ASSERT_TRUE(ok);
  return true;
}

TEST(ai_chat_maps_auth) {
  AiChatMock mock;
  AIChatClient client = make_client(mock.url());
  bool ok = false;
  try {
    (void)client.chat("__err_-32009", "x");
  } catch (const AuthenticationError& e) {
    ok = e.code() == -32009;
  }
  ASSERT_TRUE(ok);
  return true;
}

TEST(ai_chat_maps_unmapped_to_base) {
  AiChatMock mock;
  AIChatClient client = make_client(mock.url());
  bool base_only = false;
  try {
    (void)client.chat("__err_-32602", "x");
  } catch (const ConversationNotFoundError&) {
    base_only = false;  // must NOT be a mapped subclass
  } catch (const AIChatError& e) {
    base_only = e.code() == -32602;
  }
  ASSERT_TRUE(base_only);
  return true;
}

TEST(ai_chat_summarize_error_is_not_empty_string) {
  // Regression guard for the RED-proof: a swallowed {error} branch would return
  // "" and this test would go red — proving the surface, not just the gate.
  AiChatMock mock;
  AIChatClient client = make_client(mock.url());
  ASSERT_THROWS(client.summarize("__summarize_error"));
  return true;
}
