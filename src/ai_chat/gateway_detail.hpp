// Copyright (c) 2026 SignalWire
// SPDX-License-Identifier: MIT
//
// Internal helpers shared by the ChatGateway and HandoffRouter routes. Not
// installed: lives beside the sources, not under include/.
#pragma once

#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>

namespace httplib {
struct Request;
struct Response;
}  // namespace httplib

namespace signalwire {
namespace ai_chat {
namespace detail {

using json = nlohmann::json;

/// Unpadded URL-safe base64.
std::string b64url_encode(const std::string& raw);
/// Decode unpadded URL-safe base64 (non-alphabet characters are skipped, as the
/// reference's decoder does); nullopt when no padding makes it whole bytes.
std::optional<std::string> b64url_decode(const std::string& text);
/// Raw HMAC-SHA256 digest.
std::string hmac_sha256_raw(const std::string& key, const std::string& message);
/// Constant-time string equality (a length mismatch is not secret).
bool constant_time_equal(const std::string& a, const std::string& b);
/// ``n`` cryptographically random bytes.
std::string random_bytes(size_t n);
/// Seconds on a monotonic clock.
double monotonic_seconds();
/// A request header's value, or nullopt when absent.
std::optional<std::string> header_value(const httplib::Request& req, const std::string& name);
/// Parse a request's JSON body, refusing one over ``limit`` bytes with
/// GatewayRejection(413). Throws std::invalid_argument for invalid JSON.
json read_json_body(const httplib::Request& req, size_t limit);
/// Answer with ``body`` as JSON, the given status and headers.
void send_json(httplib::Response& res, int status, const json& body,
               const std::map<std::string, std::string>& headers = {});
/// Python truthiness of a JSON value.
bool truthy(const json& v);

}  // namespace detail
}  // namespace ai_chat
}  // namespace signalwire
