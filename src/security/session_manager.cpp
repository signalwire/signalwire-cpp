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

std::string SessionManager::base64url_encode(const std::string& data) {
  std::string out = base64_encode(data);
  for (char& c : out) {
    if (c == '+') {
      c = '-';
    } else if (c == '/') {
      c = '_';
    }
  }
  while (!out.empty() && out.back() == '=') {
    out.pop_back();
  }
  return out;
}

std::string SessionManager::base64url_decode(const std::string& encoded) {
  std::string std_b64 = encoded;
  for (char& c : std_b64) {
    if (c == '-') {
      c = '+';
    } else if (c == '_') {
      c = '/';
    }
  }
  while (std_b64.size() % 4) {
    std_b64.push_back('=');
  }
  return base64_decode(std_b64);
}

std::string SessionManager::hex_encode(const std::vector<uint8_t>& data) {
  std::ostringstream ss;
  ss << std::hex << std::setfill('0');
  for (auto b : data) {
    ss << std::setw(2) << static_cast<int>(b);
  }
  return ss.str();
}

std::string SessionManager::token_hex(int bytes) {
  std::vector<uint8_t> buf(static_cast<size_t>(bytes));
  if (RAND_bytes(buf.data(), bytes) != 1) {
    throw std::runtime_error("Failed to generate random nonce");
  }
  return hex_encode(buf);
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
  std::string nonce = token_hex(8);  // 16 hex chars, matches secrets.token_hex(8)

  // Sign the call_id-first colon-joined message.
  std::string message = call_id + ":" + function_name + ":" + std::to_string(expiry) + ":" + nonce;
  std::string sig_raw = hmac_sha256(message);
  std::vector<uint8_t> sig_bytes(sig_raw.begin(), sig_raw.end());
  std::string hex_sig = hex_encode(sig_bytes);

  // Combine the 5 dot-joined fields, then base64url-wrap the whole token.
  std::string token =
      call_id + "." + function_name + "." + std::to_string(expiry) + "." + nonce + "." + hex_sig;
  return base64url_encode(token);
}

bool SessionManager::validate_token(std::string_view token, std::string_view function_name,
                                    std::string_view call_id) const {
  // Reject validation when no call_id is provided.
  if (call_id.empty()) {
    return false;
  }

  // base64url-decode the whole token, then split into the 5 dot-fields
  // {call_id}.{function_name}.{expiry}.{nonce}.{signature}.
  std::string decoded = base64url_decode(std::string(token));
  std::vector<std::string> parts;
  {
    size_t start = 0;
    while (true) {
      size_t dot = decoded.find('.', start);
      if (dot == std::string::npos) {
        parts.push_back(decoded.substr(start));
        break;
      }
      parts.push_back(decoded.substr(start, dot - start));
      start = dot + 1;
    }
  }
  if (parts.size() != 5) {
    return false;
  }

  const std::string& token_call_id = parts[0];
  const std::string& token_func = parts[1];
  const std::string& token_expiry_str = parts[2];
  const std::string& token_nonce = parts[3];
  const std::string& token_signature = parts[4];

  // Verify the function matches.
  if (token_func != std::string(function_name)) {
    return false;
  }

  // Check expiry.
  try {
    int64_t expiry = std::stoll(token_expiry_str);
    if (expiry < current_timestamp()) {
      return false;
    }
  } catch (...) {
    return false;
  }

  // Recompute the signature over the signed message and compare in constant
  // time (timing_safe_compare short-circuits ONLY on a length mismatch).
  std::string message =
      token_call_id + ":" + token_func + ":" + token_expiry_str + ":" + token_nonce;
  std::string expected_sig_raw = hmac_sha256(message);
  std::vector<uint8_t> sig_bytes(expected_sig_raw.begin(), expected_sig_raw.end());
  std::string expected_sig = hex_encode(sig_bytes);
  if (!timing_safe_compare(token_signature, expected_sig)) {
    return false;
  }

  // Finally verify the call_id matches (checked last, per the reference).
  return timing_safe_compare(token_call_id, std::string(call_id));
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
  // base64url-decode the whole token, then split into the 5 dot-fields.
  std::string decoded = base64url_decode(token);
  std::vector<std::string> parts;
  {
    size_t start = 0;
    while (true) {
      size_t dot = decoded.find('.', start);
      if (dot == std::string::npos) {
        parts.push_back(decoded.substr(start));
        break;
      }
      parts.push_back(decoded.substr(start, dot - start));
      start = dot + 1;
    }
  }
  if (parts.size() != 5) {
    return json{
        {"valid_format", false}, {"parts_count", parts.size()}, {"token_length", token.size()}};
  }

  std::string token_call_id = parts[0];
  std::string token_func = parts[1];
  std::string token_expiry_str = parts[2];
  std::string token_nonce = parts[3];
  std::string signature = parts[4];

  int64_t current_time = current_timestamp();
  json status = json::object();
  status["current_time"] = current_time;
  json components = json::object();
  components["function"] = token_func;
  components["call_id"] = truncate8(token_call_id);
  components["expiry"] = token_expiry_str;
  components["nonce"] = token_nonce;
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
