// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT

#include "signalwire/relay/websocket.hpp"
#include "signalwire/logging.hpp"

#ifdef _WIN32
// Windows: RELAY WebSocket transport not yet implemented.
// Provide stub implementations so the library links.

namespace signalwire {
namespace relay {

WebSocketClient::WebSocketClient() = default;
WebSocketClient::~WebSocketClient() { close(); }

bool WebSocketClient::connect(const std::string&, int) { return false; }
void WebSocketClient::close(int, const std::string&) {}
bool WebSocketClient::send(const std::string&) { return false; }
bool WebSocketClient::do_tls_handshake(const std::string&) { return false; }
bool WebSocketClient::do_ws_upgrade(const std::string&) { return false; }
std::vector<uint8_t> WebSocketClient::encode_frame(const std::string&) { return {}; }
bool WebSocketClient::read_frame(std::string&, uint8_t&) { return false; }
bool WebSocketClient::raw_read(void*, size_t) { return false; }
bool WebSocketClient::raw_write(const void*, size_t) { return false; }
void WebSocketClient::read_loop() {}
void WebSocketClient::cleanup() {}

} // namespace relay
} // namespace signalwire

#else // POSIX

#include <openssl/ssl.h>
#include <openssl/err.h>

#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <poll.h>

#include <cstring>
#include <random>
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace signalwire {
namespace relay {

// Base64 encode for WebSocket key
static std::string base64_encode(const unsigned char* data, size_t len) {
    static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    result.reserve(4 * ((len + 2) / 3));
    for (size_t i = 0; i < len; i += 3) {
        unsigned int n = (static_cast<unsigned int>(data[i]) << 16);
        if (i + 1 < len) n |= (static_cast<unsigned int>(data[i + 1]) << 8);
        if (i + 2 < len) n |= static_cast<unsigned int>(data[i + 2]);
        result.push_back(table[(n >> 18) & 0x3F]);
        result.push_back(table[(n >> 12) & 0x3F]);
        result.push_back((i + 1 < len) ? table[(n >> 6) & 0x3F] : '=');
        result.push_back((i + 2 < len) ? table[n & 0x3F] : '=');
    }
    return result;
}

WebSocketClient::WebSocketClient() = default;

WebSocketClient::~WebSocketClient() {
    close();
    if (read_thread_.joinable()) {
        read_thread_.join();
    }
}

bool WebSocketClient::connect(const std::string& host, int port) {
    // DNS resolution
    struct addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    std::string port_str = std::to_string(port);

    int rc = ::getaddrinfo(host.c_str(), port_str.c_str(), &hints, &res);
    if (rc != 0 || !res) {
        if (on_error_) on_error_("DNS resolution failed for " + host + ": " + gai_strerror(rc));
        return false;
    }

    sock_fd_ = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock_fd_ < 0) {
        ::freeaddrinfo(res);
        if (on_error_) on_error_("Socket creation failed");
        return false;
    }

    // Set socket timeout for connect
    struct timeval tv;
    tv.tv_sec = 10;
    tv.tv_usec = 0;
    ::setsockopt(sock_fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    ::setsockopt(sock_fd_, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    if (::connect(sock_fd_, res->ai_addr, res->ai_addrlen) < 0) {
        ::freeaddrinfo(res);
        ::close(sock_fd_);
        sock_fd_ = -1;
        if (on_error_) on_error_("TCP connect failed to " + host + ":" + port_str);
        return false;
    }
    ::freeaddrinfo(res);

    // TLS handshake
    if (!do_tls_handshake(host)) {
        ::close(sock_fd_);
        sock_fd_ = -1;
        return false;
    }

    // WebSocket upgrade
    if (!do_ws_upgrade(host)) {
        cleanup();
        return false;
    }

    connected_.store(true);
    closing_.store(false);

    // Start read thread
    read_thread_ = std::thread([this]() { read_loop(); });

    return true;
}

void WebSocketClient::close(int code, const std::string& reason) {
    if (!connected_.load()) return;
    closing_.store(true);
    connected_.store(false);

    // Send close frame (opcode 0x8)
    if (ssl_) {
        std::vector<uint8_t> close_payload;
        close_payload.push_back(static_cast<uint8_t>((code >> 8) & 0xFF));
        close_payload.push_back(static_cast<uint8_t>(code & 0xFF));
        for (char c : reason) close_payload.push_back(static_cast<uint8_t>(c));

        // Build close frame
        std::vector<uint8_t> frame;
        frame.push_back(0x88); // FIN + opcode 8 (close)
        uint8_t mask_bit = 0x80;
        if (close_payload.size() <= 125) {
            frame.push_back(mask_bit | static_cast<uint8_t>(close_payload.size()));
        } else {
            frame.push_back(mask_bit | 126);
            frame.push_back(static_cast<uint8_t>((close_payload.size() >> 8) & 0xFF));
            frame.push_back(static_cast<uint8_t>(close_payload.size() & 0xFF));
        }

        // Masking key
        std::random_device rd;
        uint8_t mask[4];
        for (int i = 0; i < 4; i++) mask[i] = static_cast<uint8_t>(rd());
        frame.insert(frame.end(), mask, mask + 4);
        for (size_t i = 0; i < close_payload.size(); i++) {
            frame.push_back(close_payload[i] ^ mask[i % 4]);
        }

        std::lock_guard<std::mutex> lock(write_mutex_);
        raw_write(frame.data(), frame.size());
    }

    cleanup();

    if (read_thread_.joinable() && read_thread_.get_id() != std::this_thread::get_id()) {
        read_thread_.join();
        read_thread_ = std::thread();
    }
}

bool WebSocketClient::send(const std::string& message) {
    if (!connected_.load()) return false;
    auto frame = encode_frame(message);
    std::lock_guard<std::mutex> lock(write_mutex_);
    return raw_write(frame.data(), frame.size());
}

bool WebSocketClient::do_tls_handshake(const std::string& host) {
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();

    const SSL_METHOD* method = TLS_client_method();
    ssl_ctx_ = SSL_CTX_new(method);
    if (!ssl_ctx_) {
        if (on_error_) on_error_("SSL_CTX_new failed");
        return false;
    }

    SSL_CTX_set_default_verify_paths(static_cast<SSL_CTX*>(ssl_ctx_));
    SSL_CTX_set_verify(static_cast<SSL_CTX*>(ssl_ctx_), SSL_VERIFY_PEER, nullptr);

    ssl_ = SSL_new(static_cast<SSL_CTX*>(ssl_ctx_));
    if (!ssl_) {
        if (on_error_) on_error_("SSL_new failed");
        return false;
    }

    SSL_set_fd(static_cast<SSL*>(ssl_), sock_fd_);
    SSL_set_tlsext_host_name(static_cast<SSL*>(ssl_), host.c_str());

    int ret = SSL_connect(static_cast<SSL*>(ssl_));
    if (ret != 1) {
        unsigned long err = ERR_get_error();
        char buf[256];
        ERR_error_string_n(err, buf, sizeof(buf));
        if (on_error_) on_error_(std::string("SSL_connect failed: ") + buf);
        return false;
    }

    return true;
}

bool WebSocketClient::do_ws_upgrade(const std::string& host) {
    // Generate random 16-byte key
    std::random_device rd;
    unsigned char key_bytes[16];
    for (int i = 0; i < 16; i++) key_bytes[i] = static_cast<unsigned char>(rd());
    std::string ws_key = base64_encode(key_bytes, 16);

    // Build HTTP upgrade request
    std::ostringstream req;
    req << "GET / HTTP/1.1\r\n"
        << "Host: " << host << "\r\n"
        << "Upgrade: websocket\r\n"
        << "Connection: Upgrade\r\n"
        << "Sec-WebSocket-Key: " << ws_key << "\r\n"
        << "Sec-WebSocket-Version: 13\r\n"
        << "\r\n";

    std::string req_str = req.str();
    if (!raw_write(req_str.data(), req_str.size())) {
        if (on_error_) on_error_("Failed to send WebSocket upgrade request");
        return false;
    }

    // Read HTTP response (up to 4KB)
    std::string response;
    char buf[1];
    while (response.size() < 4096) {
        if (!raw_read(buf, 1)) {
            if (on_error_) on_error_("Failed to read WebSocket upgrade response");
            return false;
        }
        response.push_back(buf[0]);
        if (response.size() >= 4 &&
            response.substr(response.size() - 4) == "\r\n\r\n") {
            break;
        }
    }

    // Verify 101 Switching Protocols
    if (response.find("HTTP/1.1 101") == std::string::npos) {
        if (on_error_) on_error_("WebSocket upgrade failed: " + response.substr(0, 80));
        return false;
    }

    return true;
}

std::vector<uint8_t> WebSocketClient::encode_frame(const std::string& payload) {
    std::vector<uint8_t> frame;
    // FIN bit + text opcode (0x1)
    frame.push_back(0x81);

    // Payload length with mask bit set (client must mask)
    uint64_t len = payload.size();
    if (len <= 125) {
        frame.push_back(0x80 | static_cast<uint8_t>(len));
    } else if (len <= 65535) {
        frame.push_back(0x80 | 126);
        frame.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
        frame.push_back(static_cast<uint8_t>(len & 0xFF));
    } else {
        frame.push_back(0x80 | 127);
        for (int i = 7; i >= 0; i--) {
            frame.push_back(static_cast<uint8_t>((len >> (8 * i)) & 0xFF));
        }
    }

    // Masking key (4 random bytes)
    std::random_device rd;
    uint8_t mask[4];
    for (int i = 0; i < 4; i++) mask[i] = static_cast<uint8_t>(rd());
    frame.insert(frame.end(), mask, mask + 4);

    // Masked payload
    for (size_t i = 0; i < payload.size(); i++) {
        frame.push_back(static_cast<uint8_t>(payload[i]) ^ mask[i % 4]);
    }

    return frame;
}

bool WebSocketClient::read_frame(std::string& out_payload, uint8_t& out_opcode) {
    uint8_t header[2];
    if (!raw_read(header, 2)) return false;

    out_opcode = header[0] & 0x0F;
    bool masked = (header[1] & 0x80) != 0;
    uint64_t payload_len = header[1] & 0x7F;

    if (payload_len == 126) {
        uint8_t ext[2];
        if (!raw_read(ext, 2)) return false;
        payload_len = (static_cast<uint64_t>(ext[0]) << 8) | ext[1];
    } else if (payload_len == 127) {
        uint8_t ext[8];
        if (!raw_read(ext, 8)) return false;
        payload_len = 0;
        for (int i = 0; i < 8; i++) {
            payload_len = (payload_len << 8) | ext[i];
        }
    }

    uint8_t mask_key[4] = {0};
    if (masked) {
        if (!raw_read(mask_key, 4)) return false;
    }

    out_payload.resize(static_cast<size_t>(payload_len));
    if (payload_len > 0) {
        if (!raw_read(&out_payload[0], static_cast<size_t>(payload_len))) return false;
        if (masked) {
            for (size_t i = 0; i < out_payload.size(); i++) {
                out_payload[i] ^= static_cast<char>(mask_key[i % 4]);
            }
        }
    }

    return true;
}

void WebSocketClient::read_loop() {
    while (connected_.load()) {
        std::string payload;
        uint8_t opcode;

        if (!read_frame(payload, opcode)) {
            if (connected_.load() && !closing_.load()) {
                connected_.store(false);
                if (on_close_) on_close_(1006, "Connection lost");
            }
            break;
        }

        switch (opcode) {
            case 0x1: // Text frame
                if (on_message_) on_message_(payload);
                break;
            case 0x8: { // Close frame
                int code = 1000;
                std::string reason;
                if (payload.size() >= 2) {
                    code = (static_cast<unsigned char>(payload[0]) << 8) |
                            static_cast<unsigned char>(payload[1]);
                    if (payload.size() > 2) reason = payload.substr(2);
                }
                connected_.store(false);
                if (on_close_) on_close_(code, reason);
                return;
            }
            case 0x9: { // Ping - respond with pong
                std::vector<uint8_t> pong;
                pong.push_back(0x8A); // FIN + pong opcode
                uint8_t mask_bit = 0x80;
                if (payload.size() <= 125) {
                    pong.push_back(mask_bit | static_cast<uint8_t>(payload.size()));
                } else {
                    pong.push_back(mask_bit | 126);
                    pong.push_back(static_cast<uint8_t>((payload.size() >> 8) & 0xFF));
                    pong.push_back(static_cast<uint8_t>(payload.size() & 0xFF));
                }
                std::random_device rd;
                uint8_t mask[4];
                for (int i = 0; i < 4; i++) mask[i] = static_cast<uint8_t>(rd());
                pong.insert(pong.end(), mask, mask + 4);
                for (size_t i = 0; i < payload.size(); i++) {
                    pong.push_back(static_cast<uint8_t>(payload[i]) ^ mask[i % 4]);
                }
                std::lock_guard<std::mutex> lock(write_mutex_);
                raw_write(pong.data(), pong.size());
                break;
            }
            case 0xA: // Pong - ignore
                break;
            default:
                break;
        }
    }
}

bool WebSocketClient::raw_read(void* buf, size_t len) {
    size_t total = 0;
    auto* p = static_cast<char*>(buf);
    while (total < len) {
        int n = SSL_read(static_cast<SSL*>(ssl_), p + total, static_cast<int>(len - total));
        if (n <= 0) return false;
        total += static_cast<size_t>(n);
    }
    return true;
}

bool WebSocketClient::raw_write(const void* buf, size_t len) {
    size_t total = 0;
    auto* p = static_cast<const char*>(buf);
    while (total < len) {
        int n = SSL_write(static_cast<SSL*>(ssl_), p + total, static_cast<int>(len - total));
        if (n <= 0) return false;
        total += static_cast<size_t>(n);
    }
    return true;
}

void WebSocketClient::cleanup() {
    if (ssl_) {
        SSL_shutdown(static_cast<SSL*>(ssl_));
        SSL_free(static_cast<SSL*>(ssl_));
        ssl_ = nullptr;
    }
    if (ssl_ctx_) {
        SSL_CTX_free(static_cast<SSL_CTX*>(ssl_ctx_));
        ssl_ctx_ = nullptr;
    }
    if (sock_fd_ >= 0) {
        ::close(sock_fd_);
        sock_fd_ = -1;
    }
}

} // namespace relay
} // namespace signalwire

#endif // _WIN32
