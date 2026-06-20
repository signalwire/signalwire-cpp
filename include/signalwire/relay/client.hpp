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
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "signalwire/relay/call.hpp"
#include "signalwire/relay/constants.hpp"
#include "signalwire/relay/message.hpp"
#include "signalwire/relay/relay_event.hpp"
#include "signalwire/relay/websocket.hpp"

namespace signalwire {
namespace relay {

using json = nlohmann::json;

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
  std::string host = DEFAULT_HOST;
  int port = DEFAULT_PORT;
  std::vector<std::string> contexts = {"default"};
  int max_active_calls = DEFAULT_MAX_ACTIVE_CALLS;
  int max_connections = DEFAULT_MAX_CONNECTIONS;
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
  /// "device-of-leg-of-leg" array used by the Python SDK
  /// (`[[{type:phone,...}]]`). Returns a Call once the server emits
  /// calling.call.dial(answered) for the dial's tag, or an empty Call
  /// on timeout / failure.
  ///
  /// `tag` lets callers pin an explicit dial tag for journal-based
  /// assertions; if blank, a UUID is generated.
  /// `dial_timeout_ms` caps how long dial() blocks waiting for the
  /// server's terminal dial event.
  /// `max_duration` (seconds) is forwarded into the calling.dial frame
  /// when non-zero.
  Call dial(const json& devices, const std::string& tag = "", int dial_timeout_ms = 120000,
            int max_duration = 0);

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

  // Accessors
  const RelayConfig& config() const { return config_; }
  const std::string& relay_protocol() const { return protocol_; }

  /// Server-assigned session id captured from the `signalwire.connect`
  /// handshake result (`result.sessionid`). Empty until a successful
  /// connect. Production code never needs this — it exists so the test
  /// harness can scope the mock's journal/scenarios/pushes to this client's
  /// session and run safely under parallel execution. Python's RelayClient
  /// keeps the equivalent internal too; exposing a read-only accessor here
  /// (rather than a bare public field) keeps it off the mutable surface.
  /// Documented in PORT_ADDITIONS.md as cpp_relay_session_id_accessor.
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
