// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#pragma once

#include <string>
#include <map>

namespace signalwire {
namespace skills {

/// Result of a skill HTTP request. `status` is the HTTP status code (0
/// indicates a transport-level error — connection refused, DNS, etc.).
/// `body` is the raw response body. `error` is non-empty when `status` is 0.
struct SkillHttpResponse {
    int status = 0;
    std::string body;
    std::string error;
};

/// Issue a real HTTP GET. Implementation uses cpp-httplib so it works
/// against any plain-HTTP host (including loopback fixtures used by the
/// `audit_skills_dispatch.py` audit). Skills that need TLS to reach
/// production upstreams set the appropriate base-URL env var (e.g.
/// `WEB_SEARCH_BASE_URL`) — production deployments point at TLS proxies
/// or a dev-side rewriter; the SDK keeps its transport stack OpenSSL-3
/// independent until the wider port adopts it.
[[nodiscard]] SkillHttpResponse http_get(const std::string& url,
                           const std::map<std::string, std::string>& headers = {},
                           int timeout_seconds = 10);

/// Millisecond-precision variant of `http_get`. The connection and read
/// timeouts are both bound to `timeout_ms`. Needed by web_search's
/// `per_page_timeout`, which is a sub-second float in the common case
/// (default 2.0s, but configurable below 1s) — the integer-second `http_get`
/// truncates a 0.3s budget to 0 and httplib treats a zero timeout as "no
/// wait", which would defeat the bound. A non-positive `timeout_ms` falls
/// back to a 10s default so a misconfiguration can never produce an
/// unbounded fetch.
[[nodiscard]] SkillHttpResponse http_get_ms(const std::string& url,
                              const std::map<std::string, std::string>& headers,
                              long timeout_ms);

/// Issue a real HTTP POST with `body` as the request body and `content_type`
/// for Content-Type. Same semantics as `http_get` for status / error / body.
[[nodiscard]] SkillHttpResponse http_post(const std::string& url,
                            const std::string& body,
                            const std::string& content_type = "application/json",
                            const std::map<std::string, std::string>& headers = {},
                            int timeout_seconds = 10);

}  // namespace skills
}  // namespace signalwire
