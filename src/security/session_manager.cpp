// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#include "signalwire/security/session_manager.hpp"

#include <openssl/crypto.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>

#include <cstring>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace signalwire {
namespace security {

namespace {
/// Truncate to 8 chars + "..." when longer (matches the authoritative
/// reference's debug_token component redaction).
std::string truncate8(const std::string& s) { return s.size() > 8 ? s.substr(0, 8) + "..." : s; }

/// Format a Unix timestamp as an ISO-8601 UTC string (matches Python's
/// datetime.fromtimestamp(...).isoformat() closely enough for the debug view).
std::string iso8601_utc(int64_t ts) {
  std::time_t t = static_cast<std::time_t>(ts);
  std::tm tm_buf{};
#if defined(_WIN32)
  gmtime_s(&tm_buf, &t);
#else
  gmtime_r(&t, &tm_buf);
#endif
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm_buf);
  return std::string(buf);
}
}  // namespace

SessionManager::SessionManager() : secret_(32) {
  if (RAND_bytes(secret_.data(), 32) != 1) {
    throw std::runtime_error("Failed to generate random secret for SessionManager");
  }
}

SessionManager::SessionManager(const std::vector<uint8_t>& secret) : secret_(secret) {
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
  if (valb > -6) {
    out.push_back(table[((val << 8) >> (valb + 8)) & 0x3F]);
  }
  while (out.size() % 4) {
    out.push_back('=');
  }
  return out;
}

std::string SessionManager::base64_decode(const std::string& encoded) {
  static const int T[256] = {
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 62,
      -1, -1, -1, 63, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, -1, -1, -1, -1, -1, -1, -1, 0,
      1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22,
      23, 24, 25, -1, -1, -1, -1, -1, -1, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38,
      39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  };
  std::string out;
  int val = 0, valb = -8;
  for (unsigned char c : encoded) {
    if (T[c] == -1) {
      break;
    }
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

int64_t SessionManager::current_timestamp() { return static_cast<int64_t>(std::time(nullptr)); }

std::string SessionManager::hmac_sha256(const std::string& data) const {
  unsigned int len = 0;
  unsigned char result[EVP_MAX_MD_SIZE];
  HMAC(EVP_sha256(), secret_.data(), static_cast<int>(secret_.size()),
       reinterpret_cast<const unsigned char*>(data.data()), data.size(), result, &len);
  return std::string(reinterpret_cast<char*>(result), len);
}

bool SessionManager::timing_safe_compare(const std::string& a, const std::string& b) {
  if (a.size() != b.size()) {
    return false;
  }
  return CRYPTO_memcmp(a.data(), b.data(), a.size()) == 0;
}

std::string SessionManager::create_token(const std::string& function_name,
                                         const std::string& call_id, int expiry_seconds) const {
  int64_t expiry = current_timestamp() + expiry_seconds;
  std::string payload = function_name + ":" + call_id + ":" + std::to_string(expiry);
  std::string encoded_payload = base64_encode(payload);

  std::string signature = hmac_sha256(encoded_payload);
  std::vector<uint8_t> sig_bytes(signature.begin(), signature.end());
  std::string hex_sig = hex_encode(sig_bytes);

  return encoded_payload + "." + hex_sig;
}

bool SessionManager::validate_token(std::string_view token, std::string_view function_name,
                                    std::string_view call_id) const {
  auto dot_pos = token.find('.');
  if (dot_pos == std::string_view::npos) {
    return false;
  }

  // token.substr now yields a string_view; materialise the slices we hand
  // to the std::string-taking helpers (hmac_sha256 / timing_safe_compare /
  // base64_decode) into owning strings.
  std::string encoded_payload(token.substr(0, dot_pos));
  std::string provided_sig(token.substr(dot_pos + 1));

  // Recompute signature
  std::string expected_sig_raw = hmac_sha256(encoded_payload);
  std::vector<uint8_t> sig_bytes(expected_sig_raw.begin(), expected_sig_raw.end());
  std::string expected_sig = hex_encode(sig_bytes);

  // Timing-safe comparison
  if (!timing_safe_compare(provided_sig, expected_sig)) {
    return false;
  }

  // Decode payload and check fields
  std::string payload = base64_decode(encoded_payload);
  // format: functionName:callID:expiryTimestamp
  auto first_colon = payload.find(':');
  if (first_colon == std::string::npos) {
    return false;
  }
  auto second_colon = payload.find(':', first_colon + 1);
  if (second_colon == std::string::npos) {
    return false;
  }

  std::string token_func = payload.substr(0, first_colon);
  std::string token_call_id = payload.substr(first_colon + 1, second_colon - first_colon - 1);
  std::string token_expiry_str = payload.substr(second_colon + 1);

  if (token_func != function_name) {
    return false;
  }
  if (token_call_id != call_id) {
    return false;
  }

  try {
    int64_t expiry = std::stoll(token_expiry_str);
    if (current_timestamp() > expiry) {
      return false;
    }
  } catch (...) {
    return false;
  }

  return true;
}

// ── Python-surface token aliases ───────────────────────────────────

std::string SessionManager::generate_token(const std::string& function_name,
                                           const std::string& call_id) const {
  return create_token(function_name, call_id, default_expiry_secs_);
}

std::string SessionManager::create_tool_token(const std::string& function_name,
                                              const std::string& call_id) const {
  return generate_token(function_name, call_id);
}

bool SessionManager::validate_tool_token(std::string_view function_name, std::string_view token,
                                         std::string_view call_id) const {
  // Reference order is (function_name, token, call_id); validate_token takes
  // (token, function_name, call_id).
  return validate_token(token, function_name, call_id);
}

// ── Session lifecycle ──────────────────────────────────────────────

std::string SessionManager::create_session(const std::string& call_id) {
  std::string id = call_id;
  if (id.empty()) {
    // secrets.token_urlsafe(16): 16 random bytes, URL-safe base64, no padding.
    std::vector<uint8_t> buf(16);
    if (RAND_bytes(buf.data(), 16) != 1) {
      throw std::runtime_error("Failed to generate random session id");
    }
    std::string raw(buf.begin(), buf.end());
    id = base64_encode(raw);
    // Make it URL-safe and drop padding (matches token_urlsafe).
    for (char& c : id) {
      if (c == '+') {
        c = '-';
      } else if (c == '/') {
        c = '_';
      }
    }
    while (!id.empty() && id.back() == '=') {
      id.pop_back();
    }
  }
  std::lock_guard<std::mutex> lock(metadata_mutex_);
  session_metadata_.emplace(id, json::object());
  return id;
}

bool SessionManager::activate_session(const std::string& call_id) {
  if (!call_id.empty()) {
    std::lock_guard<std::mutex> lock(metadata_mutex_);
    session_metadata_.emplace(call_id, json::object());
  }
  return true;
}

bool SessionManager::end_session(const std::string& call_id) {
  std::lock_guard<std::mutex> lock(metadata_mutex_);
  session_metadata_.erase(call_id);
  return true;
}

json SessionManager::get_session_metadata(const std::string& call_id) const {
  std::lock_guard<std::mutex> lock(metadata_mutex_);
  auto it = session_metadata_.find(call_id);
  if (it == session_metadata_.end()) {
    return json::object();
  }
  return it->second;  // copy
}

bool SessionManager::set_session_metadata(const std::string& call_id, const std::string& key,
                                          const json& value) {
  std::lock_guard<std::mutex> lock(metadata_mutex_);
  json& entry = session_metadata_[call_id];
  if (!entry.is_object()) {
    entry = json::object();
  }
  entry[key] = value;
  return true;
}

void SessionManager::set_debug_mode(bool enabled) { debug_mode_ = enabled; }

json SessionManager::debug_token(const std::string& token) const {
  if (!debug_mode_) {
    return json{{"error", "debug mode not enabled"}};
  }
  auto dot_pos = token.find('.');
  if (dot_pos == std::string::npos) {
    return json{{"valid_format", false}, {"parts_count", 1}, {"token_length", token.size()}};
  }
  std::string encoded_payload = token.substr(0, dot_pos);
  std::string signature = token.substr(dot_pos + 1);

  std::string payload = base64_decode(encoded_payload);
  // format: functionName:callID:expiryTimestamp
  auto first_colon = payload.find(':');
  auto second_colon =
      first_colon == std::string::npos ? std::string::npos : payload.find(':', first_colon + 1);
  if (first_colon == std::string::npos || second_colon == std::string::npos) {
    return json{{"valid_format", false}, {"decoded", payload}, {"token_length", token.size()}};
  }

  std::string token_func = payload.substr(0, first_colon);
  std::string token_call_id = payload.substr(first_colon + 1, second_colon - first_colon - 1);
  std::string token_expiry_str = payload.substr(second_colon + 1);

  int64_t current_time = current_timestamp();
  json status = json::object();
  status["current_time"] = current_time;
  json components = json::object();
  components["function"] = token_func;
  components["call_id"] = truncate8(token_call_id);
  components["expiry"] = token_expiry_str;
  components["signature"] = truncate8(signature);
  try {
    int64_t expiry = std::stoll(token_expiry_str);
    bool is_expired = expiry < current_time;
    status["is_expired"] = is_expired;
    status["expires_in_seconds"] = is_expired ? int64_t{0} : (expiry - current_time);
    components["expiry_date"] = iso8601_utc(expiry);
  } catch (...) {
    status["is_expired"] = nullptr;
    status["expires_in_seconds"] = nullptr;
    components["expiry_date"] = nullptr;
  }

  return json{{"valid_format", true}, {"components", components}, {"status", status}};
}

}  // namespace security
}  // namespace signalwire
