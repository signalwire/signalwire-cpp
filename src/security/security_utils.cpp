// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
//
// Standalone security-hygiene utilities — see
// include/signalwire/security/security_utils.hpp for the contract and
// signalwire-python .../core/security/security_utils.py for the reference.

#include "signalwire/security/security_utils.hpp"

#include <array>
#include <cctype>
#include <regex>
#include <string>
#include <string_view>

namespace signalwire {
namespace security {
namespace security_utils {

namespace {

// Header names whose values are credentials/secrets and must never be handed
// to user callbacks or written to logs. Compared case-insensitively.
constexpr std::array<std::string_view, 5> kSensitiveHeaders = {
    "authorization", "cookie", "x-api-key", "proxy-authorization", "set-cookie",
};

// Lower-case an ASCII string (header keys are ASCII). Used only for the
// case-insensitive sensitivity comparison; the stored key keeps its casing.
std::string AsciiLower(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (unsigned char c : s) {
    out.push_back(static_cast<char>(std::tolower(c)));
  }
  return out;
}

bool IsSensitive(const std::string& key) {
  const std::string lowered = AsciiLower(key);
  for (const std::string_view sensitive : kSensitiveHeaders) {
    if (lowered == sensitive) {
      return true;
    }
  }
  return false;
}

// URL credentials: ``://user:secret@host`` -> ``://user:****@host``.
// Mirrors the Python pattern ``://([^:@/]+):([^@/]+)@``.
const std::regex& UrlCredentialsRe() {
  static const std::regex re(R"(://([^:@/]+):([^@/]+)@)");
  return re;
}

}  // namespace

std::map<std::string, std::string> FilterSensitiveHeaders(
    const std::map<std::string, std::string>& headers) {
  std::map<std::string, std::string> filtered;
  for (const auto& [key, value] : headers) {
    if (!IsSensitive(key)) {
      filtered.emplace(key, value);
    }
  }
  return filtered;
}

std::string RedactUrl(const std::string& url) {
  return std::regex_replace(url, UrlCredentialsRe(), "://$1:****@");
}

bool IsValidHostname(const std::string& host) {
  if (host.empty()) {
    return false;
  }
  for (const unsigned char c : host) {
    // Reject whitespace, slashes, backslashes, and control chars
    // (matches the Python char class ``[\s/\\\x00-\x1f\x7f]``).
    if (std::isspace(c) != 0 || c == '/' || c == '\\' || c <= 0x1f || c == 0x7f) {
      return false;
    }
  }
  return true;
}

}  // namespace security_utils
}  // namespace security
}  // namespace signalwire
