// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
//
// RELAY WebSocket transport, implemented on top of IXWebSocket.
//
// IXWebSocket is cross-platform (Linux/macOS/Windows) and provides RFC 6455
// framing, ping/pong keepalive, and OpenSSL-backed TLS — so this file no
// longer hand-rolls any of that, and there is no Windows-only stub: the same
// code path now works on every platform.
//
// IXWebSocket's API is asynchronous (start() returns immediately and delivers
// Open/Message/Close/Error events on a background thread). relay::RelayClient
// depends on a *synchronous* connect() that returns success/failure, so this
// wrapper bridges the two with a condition variable that connect() waits on
// until the first Open (success) or Error/Close (failure) event arrives.

#include "signalwire/relay/websocket.hpp"

#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXSocketTLSOptions.h>
#include <ixwebsocket/IXWebSocket.h>
#include <ixwebsocket/IXWebSocketMessage.h>

#include <chrono>
#include <cstdlib>

#include "signalwire/logging.hpp"

namespace signalwire {
namespace relay {

namespace {

// IXWebSocket requires one-time network-subsystem init (a no-op on POSIX, but
// it initializes Winsock on Windows). Run it exactly once, process-wide.
void ensure_net_system() {
  static const bool initialized = []() {
    ix::initNetSystem();
    return true;
  }();
  (void)initialized;
}

// How long connect() blocks waiting for the WebSocket to open before giving up.
constexpr int kConnectTimeoutSecs = 15;

}  // namespace

WebSocketClient::WebSocketClient() { ensure_net_system(); }

WebSocketClient::~WebSocketClient() { close(); }

bool WebSocketClient::connect(const std::string& host, int port) {
  return connect_impl(host, port, /*tls=*/true);
}

bool WebSocketClient::connect_plain(const std::string& host, int port) {
  return connect_impl(host, port, /*tls=*/false);
}

bool WebSocketClient::connect_impl(const std::string& host, int port, bool tls) {
  if (connected_.load()) return true;

  ws_ = std::make_unique<ix::WebSocket>();

  const std::string scheme = tls ? "wss" : "ws";
  ws_->setUrl(scheme + "://" + host + ":" + std::to_string(port) + "/");

  // We manage reconnection at the RelayClient layer (exponential backoff +
  // re-authentication for session resumption), so disable IXWebSocket's own
  // auto-reconnect — otherwise a dropped socket would silently reopen here
  // and bypass RelayClient::reconnect()/authenticate().
  ws_->disableAutomaticReconnection();
  ws_->setHandshakeTimeout(kConnectTimeoutSecs);

  if (tls) {
    ix::SocketTLSOptions tlsOpts;
    tlsOpts.tls = true;
    // Trust a private/self-signed CA when SSL_CERT_FILE points at one
    // (the cross-port test idiom — see gen_certs.sh). Otherwise leave
    // caFile at its "SYSTEM" default so production verifies against the
    // OS trust store. Verification is NEVER disabled.
    if (const char* ca = std::getenv("SSL_CERT_FILE")) {
      if (ca && *ca) tlsOpts.caFile = ca;
    }
    ws_->setTLSOptions(tlsOpts);
  }

  closing_.store(false);
  {
    std::lock_guard<std::mutex> lk(connect_mutex_);
    connect_done_ = false;
    connect_ok_ = false;
  }

  ws_->setOnMessageCallback([this](const ix::WebSocketMessagePtr& msg) {
    switch (msg->type) {
      case ix::WebSocketMessageType::Open: {
        connected_.store(true);
        std::lock_guard<std::mutex> lk(connect_mutex_);
        connect_ok_ = true;
        connect_done_ = true;
        connect_cv_.notify_all();
        break;
      }
      case ix::WebSocketMessageType::Message: {
        if (on_message_) on_message_(msg->str);
        break;
      }
      case ix::WebSocketMessageType::Error: {
        // A handshake/TLS failure before Open: fail connect(). After
        // Open, surface via on_error_.
        bool was_connected = connected_.load();
        if (on_error_) on_error_(msg->errorInfo.reason);
        std::lock_guard<std::mutex> lk(connect_mutex_);
        if (!was_connected && !connect_done_) {
          connect_ok_ = false;
          connect_done_ = true;
          connect_cv_.notify_all();
        }
        break;
      }
      case ix::WebSocketMessageType::Close: {
        bool was_connected = connected_.exchange(false);
        int code = static_cast<int>(msg->closeInfo.code);
        const std::string& reason = msg->closeInfo.reason;
        // If we close before ever opening, that also fails connect().
        {
          std::lock_guard<std::mutex> lk(connect_mutex_);
          if (!connect_done_) {
            connect_ok_ = false;
            connect_done_ = true;
            connect_cv_.notify_all();
          }
        }
        if (was_connected && !closing_.load() && on_close_) {
          on_close_(code, reason);
        }
        break;
      }
      default:
        // Ping/Pong/Fragment are handled internally by IXWebSocket.
        break;
    }
  });

  ws_->start();

  // Block until the first Open (success) or Error/Close (failure), bounded
  // by the handshake timeout plus slack.
  std::unique_lock<std::mutex> lk(connect_mutex_);
  bool signaled = connect_cv_.wait_for(lk, std::chrono::seconds(kConnectTimeoutSecs + 2),
                                       [this] { return connect_done_; });

  if (!signaled || !connect_ok_) {
    lk.unlock();
    if (!signaled && on_error_) {
      on_error_("WebSocket connect timed out to " + host + ":" + std::to_string(port));
    }
    closing_.store(true);
    if (ws_) {
      ws_->stop();
      ws_.reset();
    }
    connected_.store(false);
    return false;
  }
  return true;
}

void WebSocketClient::close(int code, const std::string& reason) {
  if (!ws_) return;
  closing_.store(true);
  connected_.store(false);
  // stop() sends the close frame (with code/reason) and joins the background
  // thread, so it is safe to call from any thread other than the event
  // callback itself.
  ws_->stop(static_cast<uint16_t>(code), reason);
  ws_.reset();
}

bool WebSocketClient::send(const std::string& message) {
  if (!connected_.load() || !ws_) return false;
  auto info = ws_->sendText(message);
  return info.success;
}

}  // namespace relay
}  // namespace signalwire
