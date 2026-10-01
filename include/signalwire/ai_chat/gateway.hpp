// Copyright (c) 2026 SignalWire
// SPDX-License-Identifier: MIT
//
// A browser-facing gateway for the SignalWire AI Chat service.
//
// A chat widget running in a page cannot hold a SignalWire API token: the token
// carries the whole project, and every turn bills. So the widget talks to a
// gateway mounted in your own app, which holds the credential server-side and
// forwards on the widget's behalf:
//
//   browser --(publishable key)--> your app --(project:token)--> chat service
//
// The browser learns exactly two things: the gateway's URL and a publishable
// key. Not the project, the space, the token, or which agent config runs -- the
// gateway injects ``config_url`` itself, so a key can only ever reach the one
// script it was issued for.
//
//   signalwire::ai_chat::ChatGatewayOptions opts;
//   opts.config_url = "https://my-agent.example.com/swml";
//   opts.key = "pk_live_...";
//   opts.allowed_origins = {"https://shop.example.com"};
//   signalwire::ai_chat::ChatGateway gateway(opts);
//   agent.mount(gateway.router(), "/chat");
//
// A stolen key gets nothing to read (``chat_log`` is filtered and a
// conversation handle is signed by the gateway); what it gets is the ability to
// talk, which costs money -- so the caps (max_new_conversations, max_turns) are
// the primary control. The origin allowlist stops a key pasted into someone
// else's page; it does not stop curl. Treat it as leak containment.
//
// Exactly one browser field is forwarded rather than overwritten:
// ``user_meta_data``, the page context a widget collects about itself. It is a
// visitor's CLAIM, bounded by MAX_USER_METADATA_BYTES and kept nested under its
// own key; never treat it as authority.
//
// Every field sized by whoever holds the key is bounded and answered with 413
// past its limit: the request body (MAX_REQUEST_BODY_BYTES, refused before it is
// parsed), a chat message (MAX_MESSAGE_BYTES of UTF-8) and user_meta_data.
#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

#include "signalwire/ai_chat/ai_chat_client.hpp"
#include "signalwire/server/router.hpp"

namespace signalwire {
namespace ai_chat {

using json = nlohmann::json;

/// A handle outlives a page refresh but not a session left open overnight.
constexpr int DEFAULT_HANDLE_TTL = 24 * 60 * 60;
/// The chat service's own default conversation timeout (seconds).
constexpr int SERVICE_DEFAULT_CONVERSATION_TIMEOUT = 3600;
/// New conversations per window, per gateway.
constexpr int DEFAULT_MAX_NEW_CONVERSATIONS = 60;
/// Turns per conversation, ever.
constexpr int DEFAULT_MAX_TURNS = 200;
/// Window (seconds) for DEFAULT_MAX_NEW_CONVERSATIONS.
constexpr int DEFAULT_WINDOW_SECONDS = 60;
/// Bound on the browser-volunteered ``user_meta_data`` bag, serialized.
constexpr size_t MAX_USER_METADATA_BYTES = size_t{8} * 1024;
/// Bound on one typed message, UTF-8 encoded (a chat turn, a handoff /say).
constexpr size_t MAX_MESSAGE_BYTES = size_t{8} * 1024;
/// Bound on a whole request body, checked before it is parsed.
constexpr size_t MAX_REQUEST_BODY_BYTES = size_t{64} * 1024;

/// A request the gateway refused, with the status the browser should see.
/// Deliberately coarse: the browser is told THAT it was refused and, at most,
/// which of a handful of buckets it fell into -- never the caps' values or the
/// allowlist.
class GatewayRejection : public std::runtime_error {
 public:
  /// ``status``: 401 bad key, 403 origin/handle, 400 disallowed method or
  /// malformed input, 413 a request/message/metadata over its limit, 429 a cap
  /// was hit. ``reason``: the short fixed explanation that reaches the browser.
  GatewayRejection(int status, const std::string& reason)
      : std::runtime_error(std::to_string(status) + ": " + reason),
        status_(status),
        reason_(reason) {}

  /// HTTP status to send back.
  [[nodiscard]] int status() const { return status_; }
  /// Short, fixed explanation sent to the browser.
  [[nodiscard]] const std::string& reason() const { return reason_; }

 private:
  int status_;
  std::string reason_;
};

/// Construction options for ChatGateway.
struct ChatGatewayOptions {
  /// The agent this key may talk to. Injected on every call, never accepted
  /// from the request. Required.
  std::string config_url;
  /// The publishable key the widget carries. Empty: SIGNALWIRE_CHAT_GATEWAY_KEY,
  /// else a generated ``pk_...`` key.
  std::string key;
  /// Origins permitted to use this key. Localhost is always allowed.
  std::vector<std::string> allowed_origins;
  /// The AIChatClient to forward through. Null: one is built from the
  /// environment and owned (closed by close()).
  std::shared_ptr<AIChatClient> client;
  /// HMAC key for signing handles. Empty: SIGNALWIRE_CHAT_GATEWAY_SECRET, else
  /// random per process (outstanding handles then stop verifying on restart).
  std::string secret;
  /// Seconds a handle stays valid.
  int handle_ttl = DEFAULT_HANDLE_TTL;
  /// Idle seconds before the service ends a conversation, passed on every
  /// create; unset leaves the service default.
  std::optional<int> conversation_timeout;
  /// New conversations per ``window_seconds``.
  int max_new_conversations = DEFAULT_MAX_NEW_CONVERSATIONS;
  /// Turns a single conversation may run.
  int max_turns = DEFAULT_MAX_TURNS;
  /// Window for ``max_new_conversations``.
  int window_seconds = DEFAULT_WINDOW_SECONDS;
};

/// Server-side proxy that lets a browser chat without holding a token.
/// Counters live in this process: behind several replicas each holds its own.
/// Thread-safe: the routes may be served concurrently.
class ChatGateway {
 public:
  /// Build a gateway that fronts one agent for browser traffic. Throws
  /// std::invalid_argument when ``config_url`` is empty.
  explicit ChatGateway(const ChatGatewayOptions& options);

  /// The agent config the gateway always sends upstream.
  [[nodiscard]] const std::string& config_url() const { return config_url_; }
  /// The publishable key the browser presents.
  [[nodiscard]] const std::string& key() const { return key_; }
  /// The listed origins (trailing slash stripped).
  [[nodiscard]] const std::set<std::string>& allowed_origins() const { return allowed_origins_; }
  /// Seconds a signed handle stays valid.
  [[nodiscard]] int handle_ttl() const { return handle_ttl_; }
  /// The conversation timeout passed upstream, when set.
  [[nodiscard]] std::optional<int> conversation_timeout() const { return conversation_timeout_; }
  /// Cap on conversations minted per window.
  [[nodiscard]] int max_new_conversations() const { return max_new_conversations_; }
  /// Cap on turns per conversation.
  [[nodiscard]] int max_turns() const { return max_turns_; }
  /// Length of the window the mint cap counts over.
  [[nodiscard]] int window_seconds() const { return window_seconds_; }

  /// Epoch SECONDS of the newest message (the service stamps microseconds), or
  /// nullopt if nothing is dated. Every role counts.
  [[nodiscard]] static std::optional<double> last_activity(const std::vector<json>& messages);

  /// Idle seconds a conversation actually gets: conversation_timeout, else the
  /// service default.
  [[nodiscard]] int effective_timeout() const;

  /// Release the upstream client, if this gateway built (and so owns) it.
  void close();

  /// Issue a signed handle for a conversation (a new ``chat-...`` id when none
  /// is given). The browser never names a conversation.
  [[nodiscard]] std::string mint_handle(
      const std::optional<std::string>& conversation_id = std::nullopt) const;

  /// Return the conversation id inside a handle, or throw GatewayRejection:
  /// 400 "malformed handle", 403 "invalid handle", 403 "expired handle".
  [[nodiscard]] std::string read_handle(const std::string& handle) const;

  /// Localhost always; a missing Origin is allowed (a non-browser caller);
  /// anything else must be listed. Throws GatewayRejection(403).
  void check_origin(const std::optional<std::string>& origin) const;

  /// Verify the publishable key (constant-time). Throws GatewayRejection(401).
  void check_key(const std::optional<std::string>& presented) const;

  /// The transcript a browser may redraw: user/assistant turns with text,
  /// reduced to role, content and (epoch-seconds) timestamp.
  [[nodiscard]] static json visible_messages(const std::vector<json>& messages);

  /// Validate the page context a browser volunteered (``user_meta_data``), or
  /// nullopt when absent/null/empty. Throws GatewayRejection 400 (not an
  /// object) or 413 (over MAX_USER_METADATA_BYTES serialized).
  [[nodiscard]] std::optional<json> read_user_metadata(
      const std::map<std::string, json>& body) const;

  /// Validate a browser request and build the upstream JSON-RPC call:
  /// ``(method, params, minted_handle)``. ``minted_handle`` is set only on the
  /// call that created the conversation. Throws GatewayRejection.
  [[nodiscard]] std::tuple<std::string, json, std::optional<std::string>> prepare(
      const std::map<std::string, json>& body, const std::optional<std::string>& origin,
      const std::optional<std::string>& key);

  /// The routes exposing this gateway: ``OPTIONS {prefix}/`` (CORS preflight)
  /// and ``POST {prefix}/`` taking ``{"method": "start"|"chat"|"log"|"end",
  /// "handle"?, "message"?, "user_meta_data"?}`` with the key in
  /// ``Authorization: Bearer``. A chat streams the service's JSON-RPC body
  /// through unbuffered; a newly minted handle rides back in ``X-Chat-Handle``.
  /// The gateway must outlive the server the routes are mounted on.
  [[nodiscard]] server::Router router();

 private:
  void charge_mint();
  void charge_turn(const std::string& conversation_id);

  std::string config_url_;
  std::string key_;
  std::set<std::string> allowed_origins_;
  int handle_ttl_;
  std::optional<int> conversation_timeout_;
  int max_new_conversations_;
  int max_turns_;
  int window_seconds_;
  std::shared_ptr<AIChatClient> client_;
  bool owns_client_;
  std::string secret_;

  std::mutex mutex_;
  std::vector<double> mints_;
  std::map<std::string, std::pair<int, double>> turns_;
};

}  // namespace ai_chat
}  // namespace signalwire
