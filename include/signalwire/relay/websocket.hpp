// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#pragma once

#include <string>
#include <vector>
#include <functional>
#include <mutex>
#include <thread>
#include <atomic>
#include <condition_variable>

namespace signalwire {
namespace relay {

/// Minimal WebSocket client using raw TCP sockets + OpenSSL TLS.
/// Implements RFC 6455 text frame encoding/decoding for JSON-RPC transport.
class WebSocketClient {
public:
    using MessageCallback = std::function<void(const std::string&)>;
    using CloseCallback = std::function<void(int code, const std::string& reason)>;
    using ErrorCallback = std::function<void(const std::string& error)>;

    WebSocketClient();
    ~WebSocketClient();

    WebSocketClient(const WebSocketClient&) = delete;
    WebSocketClient& operator=(const WebSocketClient&) = delete;

    /// Connect to wss://host:port/ with TLS
    bool connect(const std::string& host, int port = 443);

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
    void read_loop();
    bool do_tls_handshake(const std::string& host);
    bool do_ws_upgrade(const std::string& host);
    std::vector<uint8_t> encode_frame(const std::string& payload);
    bool read_frame(std::string& out_payload, uint8_t& out_opcode);
    bool raw_read(void* buf, size_t len);
    bool raw_write(const void* buf, size_t len);
    void cleanup();

    int sock_fd_ = -1;
    void* ssl_ctx_ = nullptr;
    void* ssl_ = nullptr;
    std::atomic<bool> connected_{false};
    std::atomic<bool> closing_{false};

    std::thread read_thread_;
    std::mutex write_mutex_;

    MessageCallback on_message_;
    CloseCallback on_close_;
    ErrorCallback on_error_;
};

} // namespace relay
} // namespace signalwire
