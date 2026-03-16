// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#include "signalwire/security/session_manager.hpp"
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <openssl/crypto.h>
#include <ctime>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <stdexcept>

namespace signalwire {
namespace security {

SessionManager::SessionManager() : secret_(32) {
    if (RAND_bytes(secret_.data(), 32) != 1) {
        throw std::runtime_error("Failed to generate random secret for SessionManager");
    }
}

SessionManager::SessionManager(const std::vector<uint8_t>& secret)
    : secret_(secret) {
    if (secret_.size() < 16) {
        throw std::invalid_argument("Secret must be at least 16 bytes");
    }
}

std::string SessionManager::base64_encode(const std::string& data) {
    static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    int val = 0, valb = -6;
    for (unsigned char c : data) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            out.push_back(table[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) out.push_back(table[((val << 8) >> (valb + 8)) & 0x3F]);
    while (out.size() % 4) out.push_back('=');
    return out;
}

std::string SessionManager::base64_decode(const std::string& encoded) {
    static const int T[256] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
        52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
        15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    };
    std::string out;
    int val = 0, valb = -8;
    for (unsigned char c : encoded) {
        if (T[c] == -1) break;
        val = (val << 6) + T[c];
        valb += 6;
        if (valb >= 0) {
            out.push_back(char((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

std::string SessionManager::hex_encode(const std::vector<uint8_t>& data) {
    std::ostringstream ss;
    ss << std::hex << std::setfill('0');
    for (auto b : data) {
        ss << std::setw(2) << static_cast<int>(b);
    }
    return ss.str();
}

int64_t SessionManager::current_timestamp() {
    return static_cast<int64_t>(std::time(nullptr));
}

std::string SessionManager::hmac_sha256(const std::string& data) const {
    unsigned int len = 0;
    unsigned char result[EVP_MAX_MD_SIZE];
    HMAC(EVP_sha256(), secret_.data(), static_cast<int>(secret_.size()),
         reinterpret_cast<const unsigned char*>(data.data()), data.size(),
         result, &len);
    return std::string(reinterpret_cast<char*>(result), len);
}

bool SessionManager::timing_safe_compare(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    return CRYPTO_memcmp(a.data(), b.data(), a.size()) == 0;
}

std::string SessionManager::create_token(const std::string& function_name,
                                          const std::string& call_id,
                                          int expiry_seconds) const {
    int64_t expiry = current_timestamp() + expiry_seconds;
    std::string payload = function_name + ":" + call_id + ":" + std::to_string(expiry);
    std::string encoded_payload = base64_encode(payload);

    std::string signature = hmac_sha256(encoded_payload);
    std::vector<uint8_t> sig_bytes(signature.begin(), signature.end());
    std::string hex_sig = hex_encode(sig_bytes);

    return encoded_payload + "." + hex_sig;
}

bool SessionManager::validate_token(const std::string& token,
                                     const std::string& function_name,
                                     const std::string& call_id) const {
    auto dot_pos = token.find('.');
    if (dot_pos == std::string::npos) return false;

    std::string encoded_payload = token.substr(0, dot_pos);
    std::string provided_sig = token.substr(dot_pos + 1);

    // Recompute signature
    std::string expected_sig_raw = hmac_sha256(encoded_payload);
    std::vector<uint8_t> sig_bytes(expected_sig_raw.begin(), expected_sig_raw.end());
    std::string expected_sig = hex_encode(sig_bytes);

    // Timing-safe comparison
    if (!timing_safe_compare(provided_sig, expected_sig)) return false;

    // Decode payload and check fields
    std::string payload = base64_decode(encoded_payload);
    // format: functionName:callID:expiryTimestamp
    auto first_colon = payload.find(':');
    if (first_colon == std::string::npos) return false;
    auto second_colon = payload.find(':', first_colon + 1);
    if (second_colon == std::string::npos) return false;

    std::string token_func = payload.substr(0, first_colon);
    std::string token_call_id = payload.substr(first_colon + 1, second_colon - first_colon - 1);
    std::string token_expiry_str = payload.substr(second_colon + 1);

    if (token_func != function_name) return false;
    if (token_call_id != call_id) return false;

    try {
        int64_t expiry = std::stoll(token_expiry_str);
        if (current_timestamp() > expiry) return false;
    } catch (...) {
        return false;
    }

    return true;
}

} // namespace security
} // namespace signalwire
