// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#pragma once

#include <string>
#include <vector>
#include <map>
#include <functional>
#include <memory>
#include <nlohmann/json.hpp>
#include "signalwire/relay/constants.hpp"
#include "signalwire/relay/relay_event.hpp"
#include "signalwire/relay/call.hpp"
#include "signalwire/relay/message.hpp"

namespace signalwire {
namespace relay {

using json = nlohmann::json;

/// Callback for inbound calls
using InboundCallHandler = std::function<void(Call&)>;

/// Callback for inbound messages
using InboundMessageHandler = std::function<void(const Message&)>;

/// Configuration for the RELAY client
struct RelayConfig {
    std::string project;       // Project ID
    std::string token;         // API token
    std::string host = DEFAULT_HOST;
    int port = DEFAULT_PORT;
    std::vector<std::string> contexts = {"default"};
    int max_active_calls = DEFAULT_MAX_ACTIVE_CALLS;
    int max_connections = DEFAULT_MAX_CONNECTIONS;
};

/// Real-time call control and messaging client over WebSocket.
///
/// NOTE: This is a STUB. The WebSocket transport (Blade protocol / JSON-RPC 2.0)
/// is not yet implemented. The class defines the full public API surface matching
/// the Python SDK's RelayClient so that downstream code can compile and link.
/// All I/O methods are no-ops that return immediately.
class RelayClient {
public:
    /// Construct from explicit parameters
    explicit RelayClient(const RelayConfig& config = RelayConfig{})
        : config_(config), connected_(false) {}

    /// Construct from individual parameters (convenience)
    RelayClient(const std::string& project,
                const std::string& token,
                const std::string& host = DEFAULT_HOST,
                const std::vector<std::string>& contexts = {"default"})
        : connected_(false) {
        config_.project = project;
        config_.token = token;
        config_.host = host;
        config_.contexts = contexts;
    }

    /// Construct from environment variables:
    ///   SIGNALWIRE_PROJECT_ID, SIGNALWIRE_API_TOKEN, SIGNALWIRE_SPACE
    static RelayClient from_env();

    // ========================================================================
    // Connection lifecycle (stubs)
    // ========================================================================

    /// Connect to the RELAY server. STUB: no-op.
    bool connect() {
        // TODO: establish WebSocket, authenticate, subscribe to contexts
        connected_ = true;
        return true;
    }

    /// Disconnect from the RELAY server. STUB: no-op.
    void disconnect() {
        connected_ = false;
    }

    /// Run the event loop (blocking). STUB: returns immediately.
    void run() {
        connect();
        // TODO: run event loop
    }

    bool is_connected() const { return connected_; }

    // ========================================================================
    // Call control
    // ========================================================================

    /// Register a handler for inbound calls
    void on_call(InboundCallHandler handler) {
        call_handler_ = std::move(handler);
    }

    /// Dial an outbound call. STUB: returns an empty Call.
    Call dial(const json& devices) {
        (void)devices;
        return Call("stub-call-id", "stub-node-id");
    }

    // ========================================================================
    // Messaging
    // ========================================================================

    /// Register a handler for inbound messages
    void on_message(InboundMessageHandler handler) {
        message_handler_ = std::move(handler);
    }

    /// Send an SMS/MMS message. STUB: returns empty message ID.
    std::string send_message(const std::string& from, const std::string& to,
                              const std::string& body,
                              const std::vector<std::string>& media = {},
                              const std::vector<std::string>& tags = {},
                              const std::string& region = "") {
        (void)from; (void)to; (void)body; (void)media; (void)tags; (void)region;
        return "stub-message-id";
    }

    // ========================================================================
    // Context management
    // ========================================================================

    /// Subscribe to additional contexts. STUB: no-op.
    void subscribe(const std::vector<std::string>& contexts) {
        for (const auto& c : contexts) {
            config_.contexts.push_back(c);
        }
    }

    /// Unsubscribe from contexts. STUB: no-op.
    void unsubscribe(const std::vector<std::string>& contexts) {
        (void)contexts;
    }

    // ========================================================================
    // Accessors
    // ========================================================================

    const RelayConfig& config() const { return config_; }
    const std::string& relay_protocol() const { return protocol_; }

private:
    RelayConfig config_;
    bool connected_;
    std::string protocol_ = "signalwire";
    InboundCallHandler call_handler_;
    InboundMessageHandler message_handler_;
};

// Inline implementation of from_env
inline RelayClient RelayClient::from_env() {
    RelayConfig cfg;
    const char* pid = std::getenv("SIGNALWIRE_PROJECT_ID");
    if (pid) cfg.project = pid;
    const char* tok = std::getenv("SIGNALWIRE_API_TOKEN");
    if (tok) cfg.token = tok;
    const char* host = std::getenv("SIGNALWIRE_SPACE");
    if (host) cfg.host = host;
    return RelayClient(cfg);
}

} // namespace relay
} // namespace signalwire
