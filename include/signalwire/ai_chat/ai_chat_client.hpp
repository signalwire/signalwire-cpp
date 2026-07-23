// Copyright (c) 2026 SignalWire
// SPDX-License-Identifier: MIT
#pragma once

#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <vector>

namespace signalwire {
namespace ai_chat {

using json = nlohmann::json;

// ── Errors ───────────────────────────────────────────────────────────
//
// Typed error family for the SignalWire AI Chat service, mirroring the python
// reference (signalwire.ai_chat.client). Callers catch the one base type
// (``AIChatError``) for every AI-Chat failure and can branch on ``code()`` or
// the concrete subclass.
//
// Success/failure is decided by the JSON-RPC BODY, not the HTTP status (the
// service's keepalive heartbeat commits 200 before the turn's outcome is
// known), so these are raised off the decoded envelope, never off the status.

/// Base error for AI Chat service failures. Carries the JSON-RPC error
/// ``code`` (or a sentinel when the failure rode the SUCCESS envelope, as with
/// ``SummaryError``) and the server ``message``. ``has_code()`` distinguishes a
/// real JSON-RPC code from the no-code (success-envelope) case; python models
/// that as ``code=None``, which C++ renders as ``has_code()==false``.
class AIChatError : public std::runtime_error {
 public:
  AIChatError(int code, const std::string& message)
      : std::runtime_error("[" + std::to_string(code) + "] " + message),
        code_(code),
        has_code_(true),
        message_(message) {}

  /// No-code overload — the failure rode the JSON-RPC SUCCESS envelope (no
  /// error object, hence no code), used by ``SummaryError``.
  explicit AIChatError(const std::string& message)
      : std::runtime_error("[None] " + message), code_(0), has_code_(false), message_(message) {}

  /// The JSON-RPC error code. Only meaningful when ``has_code()`` is true.
  int code() const { return code_; }
  /// Whether a real JSON-RPC error code is carried (false == success-envelope
  /// failure, python's ``code is None``).
  bool has_code() const { return has_code_; }
  /// The server-provided error message (without the ``[code]`` prefix).
  const std::string& server_message() const { return message_; }

 protected:
  int code_;
  bool has_code_;
  std::string message_;
};

/// Missing/rejected identity (HTTP 401 / JSON-RPC -32009).
class AuthenticationError : public AIChatError {
 public:
  using AIChatError::AIChatError;
};

/// The conversation does not exist in this project (-32001).
class ConversationNotFoundError : public AIChatError {
 public:
  using AIChatError::AIChatError;
};

/// Project or conversation rate limit hit (-32005 / -32006).
class RateLimitError : public AIChatError {
 public:
  using AIChatError::AIChatError;
};

/// Another message is being processed for this conversation (-32007).
class ChatInProgressError : public AIChatError {
 public:
  using AIChatError::AIChatError;
};

/// Summary generation failed. ``summarize`` returns EXACTLY ONE of ``{summary}``
/// (success) or ``{error}`` (generation failed), and the failure rides the
/// JSON-RPC *success* envelope — not an ``error`` object — so it never reaches
/// the error-code mapping. Surfaced here so a failed summary can't masquerade as
/// an empty string. ``has_code()`` is false (no JSON-RPC code).
class SummaryError : public AIChatError {
 public:
  using AIChatError::AIChatError;
};

// ── Response models ──────────────────────────────────────────────────

/// Result of ``AIChatClient::create_conversation``.
struct ConversationInfo {
  /// The conversation id (echoed back — the caller's own input).
  std::string id;
  /// Lifecycle status the service reported (e.g. ``"created"``).
  std::string status;
  /// Whether an opening assistant message was produced (python ``None``).
  bool has_initial_message = false;
  /// The opening assistant message, when ``has_initial_message`` is true.
  std::string initial_message;
};

/// Result of ``AIChatClient::chat``.
struct ChatResponse {
  /// The assistant's reply text (the wire ``response`` field).
  std::string text;
  /// The conversation id this reply belongs to.
  std::string conversation_id;
  /// An optional structured event the turn emitted; ``null`` when absent.
  json user_event = nullptr;
};

/// Result of ``AIChatClient::log``.
struct ChatLog {
  /// Full message history (the wire ``chat_log`` field).
  std::vector<json> messages;
  /// The call timeline (the wire ``call_timeline`` field).
  std::vector<json> call_timeline;
};

// ── Options ──────────────────────────────────────────────────────────

/// Per-turn options common to ``create_conversation`` and ``chat``. Unset
/// (empty / zero / false) fields are omitted from the wire params entirely,
/// matching the python reference's truthiness guards.
struct ConversationTurnOptions {
  /// Config URL locating the agent config (required on create; auto-creates on
  /// chat when present).
  std::string config_url;
  /// Conversation inactivity timeout in seconds (wire ``conversation_timeout``).
  /// 0 == unset (omitted), mirroring python's ``if timeout``.
  int timeout = 0;
  /// Reinitialize an existing conversation.
  bool reinit = false;
  /// Arbitrary caller metadata (wire ``user_meta_data``); omitted when null.
  json user_metadata = nullptr;
};

/// Options for ``create_conversation``.
struct CreateConversationOptions : ConversationTurnOptions {
  /// The opening user message to send with the create (wire ``user_message``).
  std::string user_message;
};

/// Options for ``chat``. ``role`` defaults to ``"user"``.
struct ChatOptions : ConversationTurnOptions {
  std::string role = "user";
};

/// Sampling / prompt options for ``summarize``. Every numeric field is optional
/// (its ``has_*`` flag gates whether it is sent), matching python's
/// ``**sampling`` filtered by ``v is not None``.
struct SummarizeOptions {
  /// Custom prompt steering the summary (wire ``summary_prompt``).
  std::string summary_prompt;
  double temperature = 0;
  bool has_temperature = false;
  double top_p = 0;
  bool has_top_p = false;
  double frequency_penalty = 0;
  bool has_frequency_penalty = false;
  double presence_penalty = 0;
  bool has_presence_penalty = false;
  int max_tokens = 0;
  bool has_max_tokens = false;
};

/// Constructor options for ``AIChatClient``.
struct AIChatClientOptions {
  /// Project id (Basic-auth username). Falls back to ``SIGNALWIRE_PROJECT_ID``.
  std::string project;
  /// API token (Basic-auth password). Falls back to ``SIGNALWIRE_API_TOKEN``.
  std::string token;
  /// Space name; builds ``https://{space}.signalwire.com/api/ai/chat``. Falls
  /// back to ``SIGNALWIRE_SPACE``.
  std::string space;
  /// Fully-qualified endpoint URL, used verbatim (highest precedence).
  std::string url;
  /// Idle read timeout in seconds (byte-silence, NOT total turn length).
  /// Mirrors the python reference's ``sock_read=60``. 0 disables it.
  int read_idle_timeout_seconds = 60;
  /// Bounded connect timeout in seconds (python ``connect=10``).
  int connect_timeout_seconds = 10;
};

// ── Client ───────────────────────────────────────────────────────────

/// Synchronous client for the SignalWire AI Chat service.
///
/// Speaks the standard SignalWire front-door protocol: HTTP Basic
/// ``project:api_token`` with the space in the hostname —
/// ``POST https://{space}.signalwire.com/api/ai/chat`` — carrying a JSON-RPC
/// 2.0 body whose params are pure payload (identity NEVER appears in the body;
/// it rides the Basic-auth header only).
///
/// A ``chat()`` call awaits a full LLM round trip (seconds). The service
/// streams keepalive whitespace ahead of a slow response body, so liveness is
/// byte-driven, not wall-clock: there is no total-request cap an idle-but-live
/// turn could trip. cpp-httplib's ``set_read_timeout`` is a PER-READ (per
/// socket recv) idle timeout — each keepalive whitespace read resets it — so it
/// is exactly the ``sock_read=60`` semantics of the python reference, rather
/// than a total transfer cap a heartbeat can't reset. Leading keepalive
/// whitespace is valid JSON, so the buffered parse is unaffected.
///
/// Mirrors the python reference ``signalwire.ai_chat.AIChatClient``.
class AIChatClient {
 public:
  /// @throws std::invalid_argument when no project resolves, or no URL can be
  ///   resolved from ``url`` / ``space`` / ``SIGNALWIRE_SPACE``.
  explicit AIChatClient(const AIChatClientOptions& options = {});
  ~AIChatClient();

  AIChatClient(const AIChatClient&) = delete;
  AIChatClient& operator=(const AIChatClient&) = delete;

  /// The fully-qualified endpoint URL requests are POSTed to.
  const std::string& url() const { return url_; }

  /// Release any client-owned transport resources. cpp-httplib is stateless (a
  /// fresh ``httplib::Client`` is created per request), so there is no persistent
  /// session to tear down -- this is a well-defined no-op that keeps the
  /// python reference's explicit-release / context-manager-exit shape
  /// (``client.close()`` / ``async with client:``) usable verbatim. Mirrors the
  /// python reference ``AIChatClient.close`` (and the TS no-op ``close()``).
  /// Idempotent; safe to call more than once. The destructor needs no extra work.
  void close();

  /// Create a conversation (or, with ``reinit``, reinitialize an existing one)
  /// and optionally send its opening user message.
  ConversationInfo create_conversation(const std::string& conversation_id,
                                       const CreateConversationOptions& options);

  /// Send a message and await a full LLM round trip. A ``config_url``
  /// auto-creates the conversation if it doesn't exist yet.
  ChatResponse chat(const std::string& conversation_id, const std::string& message,
                    const ChatOptions& options = {});

  /// End a conversation (triggers server-side post-processing / archival).
  /// Returns true when the service reported the conversation ended.
  bool end(const std::string& conversation_id);

  /// Permanently delete a conversation and its data. Idempotent. Returns true
  /// when the service reported the conversation deleted.
  bool del(const std::string& conversation_id);

  /// Return the full message history plus the call timeline.
  ChatLog log(const std::string& conversation_id);

  /// Return an AI summary of the conversation (rate limited server-side).
  ///
  /// The service returns EXACTLY ONE of ``{summary}`` or ``{error}`` — BOTH on
  /// the success envelope — so a failed generation surfaces as a thrown
  /// ``SummaryError``, never as an empty string.
  /// @throws SummaryError when the service reports summary generation failed.
  std::string summarize(const std::string& conversation_id, const SummarizeOptions& options = {});

 private:
  static std::string resolve_url(const std::string& url, const std::string& space);

  /// POST one JSON-RPC call and return its decoded ``result`` object (or an
  /// empty object). Throws a typed ``AIChatError`` when the body carries an
  /// ``error``. Never gates on the HTTP status.
  json request(const std::string& method, const json& params);

  std::string url_;
  std::string host_;         // scheme://host derived from url_
  std::string path_;         // request path derived from url_
  std::string auth_header_;  // "Basic base64(project:token)"
  std::string user_agent_;
  int read_idle_timeout_seconds_;
  int connect_timeout_seconds_;
  long request_counter_ = 0;
};

}  // namespace ai_chat
}  // namespace signalwire
