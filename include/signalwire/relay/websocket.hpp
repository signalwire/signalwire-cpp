// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#pragma once

#include <string>
#include <functional>
#include <memory>
#include <mutex>
#include <atomic>
#include <condition_variable>

// Forward-declare the IXWebSocket type so this header carries no dependency
// on the vendored library's headers (which live under the build tree). The
// implementation (.cpp) owns the concrete ix::WebSocket.
namespace ix { class WebSocket; }

namespace signalwire {
namespace relay {

/// WebSocket client for the RELAY JSON-RPC transport.
///
/// Wraps IXWebSocket (cross-platform, OpenSSL-backed TLS) while preserving a
/// minimal synchronous interface: connect() blocks until the handshake
/// completes (or fails/times out), send() pushes a text frame, and the
/// on_message / on_close / on_error callbacks are invoked from IXWebSocket's
/// background event thread. RFC 6455 framing, ping/pong, and TLS are handled
/// by IXWebSocket — this class only adapts its async event model to the
/// synchronous connect() contract relay::RelayClient depends on.
///
/// TLS certificate verification is ALWAYS on for the connect() (wss://) path.
/// To trust a private/self-signed CA (e.g. the porting-sdk test CA), set the
/// SSL_CERT_FILE environment variable to the CA bundle — the same cross-port
/// idiom the other ports honor; it is wired into ix::SocketTLSOptions::caFile.
/// When unset, the system trust store is used (production / public CAs).
class WebSocketClient {
public:
    using MessageCallback = std::function<void(const std::string&)>;
    using CloseCallback = std::function<void(int code, const std::string& reason)>;
    using ErrorCallback = std::function<void(const std::string& error)>;

    WebSocketClient();
    ~WebSocketClient();

    WebSocketClient(const WebSocketClient&) = delete;
    WebSocketClient& operator=(const WebSocketClient&) = delete;

    /// Connect to wss://host:port/ with TLS. Blocks until the WebSocket is
    /// open or the attempt fails/times out. Verifies the server certificate
    /// against the system store (or SSL_CERT_FILE when set).
    bool connect(const std::string& host, int port = 443);

    /// Connect to ws://host:port/ without TLS (plain TCP). Used by audit
    /// fixtures and local dev servers that don't speak TLS. Production
    /// always uses TLS via the connect() overload above.
    bool connect_plain(const std::string& host, int port);

    /// Close the WebSocket connection gracefully
    void close(int code = 1000, const std::string& reason = "");

    /// Send a text frame
    bool send(const std::string& message);

    /// Check if connected
    bool is_connected() const { return connected_.load(); }

    /// Set callback for received text messages
    void on_message(MessageCallback cb) { on_message_ = std::move(cb); }

    /// Set callback for connection close
    void on_close(CloseCallback cb) { on_close_ = std::move(cb); }

    /// Set callback for errors
    void on_error(ErrorCallback cb) { on_error_ = std::move(cb); }

private:
    // Shared connect path for both TLS (wss) and plain (ws) transports.
    bool connect_impl(const std::string& host, int port, bool tls);

    std::unique_ptr<ix::WebSocket> ws_;
    std::atomic<bool> connected_{false};
    std::atomic<bool> closing_{false};

    // Synchronization for the synchronous connect() handshake: the IXWebSocket
    // event thread signals open/error/close through these.
    std::mutex connect_mutex_;
    std::condition_variable connect_cv_;
    bool connect_done_ = false;
    bool connect_ok_ = false;

    MessageCallback on_message_;
    CloseCallback on_close_;
    ErrorCallback on_error_;
};

} // namespace relay
} // namespace signalwire
