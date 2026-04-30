// Copyright (c) 2025 SignalWire
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#pragma once

#include <array>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace signalwire {
namespace utils {
namespace url_validator {

/**
 * Cross-port SSRF block list.  Order matches the Python reference for
 * ease of cross-language review.
 */
extern const std::array<const char*, 9> BLOCKED_NETWORKS;

/**
 * Pluggable resolver. Tests inject a callable to keep the suite
 * hermetic; production resolves via getaddrinfo.  Returns nullopt on
 * resolution failure.
 *
 * The signature: function(hostname) -> optional<vector<string>> of
 * IP-string addresses.
 */
using ResolverFn =
    std::function<std::optional<std::vector<std::string>>(const std::string&)>;

/** Install a custom resolver (for tests).  Pass nullptr to clear. */
void _set_resolver(ResolverFn resolver);

/**
 * Validate that a URL is safe to fetch.
 *
 * Mirrors Python's
 * ``signalwire.utils.url_validator.validate_url(url, allow_private=False) -> bool``.
 *
 * @param url            URL to validate.
 * @param allow_private  When true, bypass the IP-blocklist check.
 * @return  True iff the URL is safe to fetch.
 */
bool validate_url(const std::string& url, bool allow_private = false);

}  // namespace url_validator
}  // namespace utils
}  // namespace signalwire
