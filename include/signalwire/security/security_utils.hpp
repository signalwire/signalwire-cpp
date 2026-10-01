// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#pragma once

#include <map>
#include <string>

/// Standalone security-hygiene utilities: keep credentials out of user
/// callbacks and logs, plus a reusable character-level hostname check. Three
/// pure free functions, no state, no I/O.
///
/// These are PascalCase free functions in a dedicated ``security_utils``
/// namespace — the same convention used for ``ValidateWebhookSignature`` in
/// this module.
namespace signalwire {
namespace security {
namespace security_utils {

/// Return a copy of ``headers`` with sensitive (credential-bearing) headers
/// removed, so request headers can be safely passed to user callbacks or logs.
///
/// The sensitivity check is case-insensitive on the key; the sensitive set is
/// ``authorization``, ``cookie``, ``x-api-key``, ``proxy-authorization``,
/// ``set-cookie``. Non-sensitive keys are preserved exactly as given (original
/// casing). An empty input yields an empty map.
///
/// @param headers Map of header name -> value.
/// @return A new map containing only the non-sensitive headers.
[[nodiscard]] std::map<std::string, std::string> FilterSensitiveHeaders(
    const std::map<std::string, std::string>& headers);

/// Mask the password in a URL's userinfo before logging.
///
/// ``https://user:secret@host/path`` -> ``https://user:****@host/path``.
/// A URL with no embedded credentials is returned unchanged.
///
/// @param url The URL string.
/// @return The URL with any ``:password@`` replaced by ``:****@``.
[[nodiscard]] std::string RedactUrl(const std::string& url);

/// Standalone hostname sanity check: reject empty hosts and any host
/// containing whitespace, slashes, backslashes, or control characters.
///
/// This is the reusable character-level check, independent of the fuller
/// ``signalwire::utils::url_validator::validate_url`` (which also does scheme
/// checks, DNS resolution, and private-IP blocking).
///
/// @param host The hostname string.
/// @return ``true`` if non-empty and free of whitespace/slashes/control
///         characters; ``false`` otherwise.
[[nodiscard]] bool IsValidHostname(const std::string& host);

}  // namespace security_utils
}  // namespace security
}  // namespace signalwire
