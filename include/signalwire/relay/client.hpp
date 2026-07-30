// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "signalwire/relay/call.hpp"
#include "signalwire/relay/constants.hpp"
#include "signalwire/relay/message.hpp"
#include "signalwire/relay/relay_event.hpp"
#include "signalwire/relay/typed_events.hpp"
#include "signalwire/relay/websocket.hpp"

namespace signalwire {
namespace relay {

using json = nlohmann::json;

/// Error returned by the RELAY server.
/// Carries the server-supplied JSON-RPC error ``code`` + ``message``.
class RelayError : public std::runtime_error {
 public:
  RelayError(int code, const std::string& message)
      : std::runtime_error("RELAY error " + std::to_string(code) + ": " + message) {
    code_ = code;
    message_ = message;
  }

  [[nodiscard]] int code() const { return code_; }
  [[nodiscard]] const std::string& message() const { return message_; }

 private:
  int code_ = 0;
  std::string message_;
};

/// Callback for inbound calls
using InboundCallHandler = std::function<void(Call&)>;

/// Callback for inbound messages
using InboundMessageHandler = std::function<void(const Message&)>;

/// Generic callback for any inbound `signalwire.event`. Fired in addition
/// to the typed handlers (on_call, on_message, action callbacks). Useful
/// for tracing and for tests/audits that need to assert event delivery.
using EventHandler = std::function<void(const RelayEvent&)>;

/// Configuration for the RELAY client
struct RelayConfig {
  std::string project;
  std::string token;
  /// JWT bearer credential — the alternative to project/token. When set, the
  /// connect frame authenticates with ``{"jwt_token": …}`` and project/token
  /// are not required (the project id is inside the token). Also settable via
  /// the ``SIGNALWIRE_JWT_TOKEN`` environment variable.
  std::string jwt_token;
  std::string host = DEFAULT_HOST;
  int port = DEFAULT_PORT;
  std::vector<std::string> contexts = {"default"};
  int max_active_calls = DEFAULT_MAX_ACTIVE_CALLS;
  int max_connections = DEFAULT_MAX_CONNECTIONS;
  // Per-request response deadline (ms). A request whose peer never answers
  // throws after this bound rather than hanging (F2.2 black-hole). Default 30s.
  int request_timeout_ms = DEFAULT_REQUEST_TIMEOUT_MS;
};

/// Real-time call control and messaging client over WebSocket.
/// Implements the full JSON-RPC 2.0 protocol with four correlation mechanisms:
/// 1. JSON-RPC id -> pending map for RPC response matching
/// 2. call_id -> Call object map for event routing
/// 3. control_id -> Action per Call for action event routing
/// 4. tag -> pending dials map for dial event matching
class RelayClient {
 public:
  /// Construct from explicit configuration
  explicit RelayClient(const RelayConfig& config = RelayConfig{});

  /// Construct from individual parameters
  RelayClient(const std::string& project, const std::string& token,
              const std::string& host = DEFAULT_HOST,
              const std::vector<std::string>& contexts = {"default"});

  ~RelayClient();

  RelayClient(const RelayClient&) = delete;
  RelayClient& operator=(const RelayClient&) = delete;

  /// Construct from environment variables:
  ///   SIGNALWIRE_PROJECT_ID, SIGNALWIRE_API_TOKEN, SIGNALWIRE_SPACE
  static RelayClient from_env();

  // Connection lifecycle
  bool connect();
  void disconnect();
  void run();
  bool is_connected() const { return connected_.load(); }

  // Call control
  void on_call(InboundCallHandler handler);

  /// Dial outbound. The `devices` argument is the nested
  /// "device-of-leg-of-leg" array (`[[{type:phone,...}]]`). Returns a Call once
  /// the server emits calling.call.dial(answered) for the dial's tag, or an
  /// empty Call on timeout / failure.
  ///
  /// PARAMETER ORDER CHANGED: this previously read
  /// ``(devices, tag, dial_timeout_ms, max_duration)``, so a positional third
  /// argument now means something different. Callers passing a positional
  /// 3rd/4th argument must swap them.
  ///
  /// `tag` lets callers pin an explicit dial tag for journal-based
  /// assertions; if blank, a UUID is generated.
  /// `max_duration` is the max call duration in MINUTES, forwarded into the
  /// calling.dial frame when non-zero.
  /// `dial_timeout` is how long, in SECONDS, dial() blocks waiting for the
  /// server's terminal dial event. ABSENT by default; the body substitutes
  /// 120s. Note the UNIT: this used to be `dial_timeout_ms` in milliseconds.
  Call dial(const json& devices, const std::string& tag = "", int max_duration = 0,
            std::optional<double> dial_timeout = std::nullopt);

  /// Register a generic event observer. Called for every dispatched
  /// `signalwire.event` after typed routing (on_call/on_message/action
  /// callbacks) has run. Multiple registrations are NOT supported —
  /// the most-recent registration wins.
  void on_event(EventHandler handler);

  /// Send a JSON-RPC request to the server. Public so harnesses and
  /// tests can drive arbitrary methods (e.g. an explicit
  /// `signalwire.subscribe` ack frame for the audit fixture).
  json send_raw_request(const std::string& method, const json& params);

  // Messaging
  void on_message(InboundMessageHandler handler);

  /// Send an SMS/MMS message via messaging.send.
  /// Returns a Message tracker whose state advances as the server
  /// pushes messaging.state events. Use `Message::wait()` to block
  /// until the terminal state (`delivered` / `undelivered` / `failed`).
  Message send_message(const std::string& from, const std::string& to, const std::string& body,
                       const std::vector<std::string>& media = {},
                       const std::vector<std::string>& tags = {}, const std::string& region = "",
                       const std::string& context = "");

  // Context management
  void subscribe(const std::vector<std::string>& contexts);
  void unsubscribe(const std::vector<std::string>& contexts);

  /// Subscribe to additional contexts for inbound events. Sends
  /// ``signalwire.receive`` on the assigned protocol so inbound calls on
  /// ``contexts`` start being delivered; can be called after ``connect()`` to
  /// add contexts without reconnecting. Thin alias of ``subscribe``.
  void receive(const std::vector<std::string>& contexts) { subscribe(contexts); }

  /// Unsubscribe from contexts for inbound events. Sends
  /// ``signalwire.unreceive`` to stop receiving inbound calls on ``contexts``.
  /// Thin alias of ``unsubscribe``.
  void unreceive(const std::vector<std::string>& contexts) { unsubscribe(contexts); }

  // Accessors
  const RelayConfig& config() const { return config_; }
  const std::string& relay_protocol() const { return protocol_; }

  // Construction parameters the reference keeps as public instance attributes
  // (`self.project` / `self.token` / `self.jwt_token` / `self.host` /
  // `self.contexts`). The port stores them in `config_`; these read them back
  // under the reference's flat names.
  /// The SignalWire project id (empty under JWT auth).
  const std::string& project() const { return config_.project; }
  /// The API token (unused under JWT auth).
  const std::string& token() const { return config_.token; }
  /// The JWT credential; when non-empty the connect frame authenticates with
  /// it instead of project/token.
  const std::string& jwt_token() const { return config_.jwt_token; }
  /// The RELAY host (a bare hostname, not a URL).
  const std::string& host() const { return config_.host; }
  /// The contexts subscribed at connect.
  const std::vector<std::string>& contexts() const { return config_.contexts; }

  /// Server-assigned session id captured from the `signalwire.connect`
  /// handshake result (`result.sessionid`). Empty until a successful
  /// connect. Production code never needs this — it exists so the test
  /// harness can scope the mock's journal/scenarios/pushes to this client's
  /// session and run safely under parallel execution. Exposed as a read-only
  /// accessor rather than a bare public field to keep it off the mutable
  /// surface.
  const std::string& session_id() const { return session_id_; }

  // JSON-RPC execution (used by Call and Action objects)
  json execute(const std::string& method, const json& params);

  // Call registry (used internally)
  void register_call(const std::string& call_id, Call* call);
  void unregister_call(const std::string& call_id);
  Call* find_call(const std::string& call_id);

 private:
  /// Generate a UUID v4 string
  static std::string generate_uuid();

  /// Build and send a JSON-RPC 2.0 request, wait for response
  json send_request(const std::string& method, const json& params);

  /// Send a JSON-RPC 2.0 response (for ACKs and pongs)
  void send_response(const std::string& id, const json& result = json::object());

  /// Handle incoming WebSocket message
  void on_ws_message(const std::string& message);

  /// Handle WebSocket close
  void on_ws_close(int code, const std::string& reason);

  /// Authenticate with signalwire.connect
  bool authenticate();

  /// Route a signalwire.event to the appropriate handler
  void route_event(const json& msg);

  /// Handle inbound call (calling.call.receive)
  void handle_inbound_call(const RelayEvent& ev);

  /// Handle dial completion (calling.call.dial)
  void handle_dial_event(const RelayEvent& ev);

  /// Handle call state changes (calling.call.state)
  void handle_call_state(const RelayEvent& ev);

  /// Handle component events (play, record, collect, etc.)
  void handle_component_event(const RelayEvent& ev);

  /// Handle messaging events
  void handle_messaging_event(const RelayEvent& ev);

  /// Open the underlying WebSocket to config_.host, honoring the
  /// SIGNALWIRE_RELAY_SCHEME override and splitting an embedded ":port".
  /// Shared by connect() and reconnect() so both parse host/scheme identically.
  bool open_ws_transport();

  /// Reconnect with exponential backoff
  bool reconnect();

  /// Reject all pending futures (on disconnect)
  void reject_all_pending();

  // Configuration
  RelayConfig config_;
  std::atomic<bool> connected_{false};
  std::atomic<bool> running_{false};
  std::string protocol_;
  std::string authorization_state_;
  // Server-assigned session id from the connect handshake result. Test-only
  // (exposed via session_id()); see the accessor's doc comment.
  std::string session_id_;

  // WebSocket transport
  std::unique_ptr<WebSocketClient> ws_;

  // Correlation mechanism 1: JSON-RPC id -> promise
  /// One in-flight JSON-RPC request awaiting its response frame.
  ///
  /// The sending thread parks on this promise's future while the WebSocket
  /// reader thread matches an inbound frame's `id` back to this entry and
  /// fulfils the promise with the result. Held by `shared_ptr` in
  /// `pending_requests_` so the entry stays alive even if the map is cleared
  /// while a waiter still holds it. On disconnect, `reject_all_pending` fulfils
  /// every outstanding promise with a `{code:"503", message:"Connection lost"}`
  /// result rather than leaving it unsatisfied — an abandoned promise would
  /// block its waiter forever.
  struct PendingRequest {
    std::promise<json> promise;
  };
  std::unordered_map<std::string, std::shared_ptr<PendingRequest>> pending_requests_;
  std::mutex pending_mutex_;

  // Correlation mechanism 2: call_id -> Call*
  std::unordered_map<std::string, Call*> calls_;
  std::mutex calls_mutex_;

  // Correlation mechanism 3: control_id -> Action (tracked per Call)

  // Correlation mechanism 4: tag -> promise<Call*> for dials
  /// One in-flight outbound dial awaiting the call it creates.
  ///
  /// A dial cannot be correlated by JSON-RPC id: the `Call` does not exist
  /// until the server reports it, so the dial is keyed by the caller-generated
  /// `tag` and the promise is fulfilled with the owned `Call*` when an event
  /// bearing that tag arrives. Held by `shared_ptr` in `pending_dials_` for the
  /// same lifetime reason as `PendingRequest`. On disconnect,
  /// `reject_all_pending` fulfils it with `nullptr`, so a waiting caller gets a
  /// null Call rather than hanging.
  struct PendingDial {
    std::promise<Call*> promise;
  };
  std::unordered_map<std::string, std::shared_ptr<PendingDial>> pending_dials_;
  std::mutex dials_mutex_;

  // Message tracking
  std::unordered_map<std::string, Message*> messages_;
  std::mutex messages_mutex_;

  // Event handlers
  InboundCallHandler call_handler_;
  InboundMessageHandler message_handler_;
  EventHandler event_handler_;
  std::mutex handler_mutex_;

  // Owned Call objects (for inbound and dial-created calls)
  std::vector<std::unique_ptr<Call>> owned_calls_;
  std::mutex owned_calls_mutex_;

  // Owned Message objects
  std::vector<std::unique_ptr<Message>> owned_messages_;
  std::mutex owned_messages_mutex_;

  // Reconnection state
  int reconnect_delay_ms_ = RECONNECT_BASE_DELAY_MS;
  int reconnect_attempts_ = 0;
};

}  // namespace relay
}  // namespace signalwire
