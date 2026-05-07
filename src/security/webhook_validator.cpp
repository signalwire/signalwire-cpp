// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
//
// Webhook signature validation — see include/signalwire/security/webhook_validator.hpp
// for the contract and porting-sdk/webhooks.md for the cross-language spec.

#include "signalwire/security/webhook_validator.hpp"

#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <openssl/crypto.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <map>
#include <unordered_map>

namespace signalwire {
namespace security {

namespace {

// ---------------------------------------------------------------------------
// Crypto helpers
// ---------------------------------------------------------------------------

/// Compute HMAC-SHA1 over ``message`` keyed by ``key``. Returns the raw
/// 20-byte digest as a string. The key/message bytes are passed through
/// to OpenSSL's HMAC() — no transformation, no logging.
std::string hmac_sha1_raw(std::string_view key, std::string_view message) {
    unsigned char out[EVP_MAX_MD_SIZE];
    unsigned int out_len = 0;
    HMAC(EVP_sha1(),
         reinterpret_cast<const unsigned char*>(key.data()),
         static_cast<int>(key.size()),
         reinterpret_cast<const unsigned char*>(message.data()),
         message.size(),
         out, &out_len);
    return std::string(reinterpret_cast<const char*>(out), out_len);
}

/// Lowercase-hex of the HMAC-SHA1 digest. Scheme A output.
std::string hex_hmac_sha1(std::string_view key, std::string_view message) {
    auto raw = hmac_sha1_raw(key, message);
    std::ostringstream ss;
    ss << std::hex << std::setfill('0');
    for (unsigned char b : raw) {
        ss << std::setw(2) << static_cast<int>(b);
    }
    return ss.str();
}

/// Standard base64 (NOT url-safe) — Scheme B output. We hand-roll rather
/// than reach for OpenSSL EVP_EncodeBlock to keep the output free of
/// trailing newlines / padding quirks the EVP wrapper sometimes inserts.
std::string base64_encode_bytes(const std::string& data) {
    static const char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);
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
    while (out.size() % 4) out.push_back('=');
    return out;
}

std::string b64_hmac_sha1(std::string_view key, std::string_view message) {
    return base64_encode_bytes(hmac_sha1_raw(key, message));
}

/// Constant-time string compare. Uses ``CRYPTO_memcmp`` per the spec.
/// Returns ``false`` on length mismatch (without leaking which one is
/// longer beyond the guarantees of ``size()``).
bool safe_eq(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    return CRYPTO_memcmp(a.data(), b.data(), a.size()) == 0;
}

/// SHA-256 hex of ``data`` — for the ``bodySHA256`` query-param check.
std::string sha256_hex(std::string_view data) {
    unsigned char digest[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(data.data()), data.size(), digest);
    std::ostringstream ss;
    ss << std::hex << std::setfill('0');
    for (size_t i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        ss << std::setw(2) << static_cast<int>(digest[i]);
    }
    return ss.str();
}

// ---------------------------------------------------------------------------
// URL parsing — hand-rolled, no external dep. Just enough to:
//   - extract scheme, host, port, path, query, fragment
//   - normalize (insert :443 / :80 or strip)
//   - extract a query-param value (bodySHA256)
// IPv6 hosts are supported in the bracketed form ``[::1]:8080``.
// ---------------------------------------------------------------------------

struct ParsedUrl {
    std::string scheme;
    std::string host;        // no brackets, no port
    bool host_is_ipv6 = false;
    std::string port;        // empty when absent
    std::string path;
    std::string query;
    std::string fragment;
};

ParsedUrl parse_url(std::string_view url) {
    ParsedUrl p;

    // scheme
    auto scheme_end = url.find("://");
    if (scheme_end == std::string_view::npos) {
        // No scheme — treat the whole thing as path. Host stays empty.
        p.path = std::string(url);
        return p;
    }
    p.scheme.assign(url.data(), scheme_end);

    // Lowercase scheme — match Python's parsed.scheme
    std::transform(p.scheme.begin(), p.scheme.end(), p.scheme.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    auto rest = url.substr(scheme_end + 3);

    // Find end of authority section
    size_t auth_end = rest.size();
    auto path_start = rest.find('/');
    auto query_start = rest.find('?');
    auto frag_start = rest.find('#');
    auth_end = std::min({auth_end,
                         path_start == std::string_view::npos ? rest.size() : path_start,
                         query_start == std::string_view::npos ? rest.size() : query_start,
                         frag_start == std::string_view::npos ? rest.size() : frag_start});
    auto authority = rest.substr(0, auth_end);

    // userinfo not relevant for signing — skip past any '@'
    auto at = authority.rfind('@');
    if (at != std::string_view::npos) {
        authority = authority.substr(at + 1);
    }

    // host[:port], with IPv6 in brackets
    if (!authority.empty() && authority.front() == '[') {
        auto close = authority.find(']');
        if (close != std::string_view::npos) {
            p.host.assign(authority.data() + 1, close - 1);
            p.host_is_ipv6 = true;
            if (close + 1 < authority.size() && authority[close + 1] == ':') {
                p.port.assign(authority.data() + close + 2,
                              authority.size() - (close + 2));
            }
        } else {
            p.host = std::string(authority);
        }
    } else {
        auto colon = authority.rfind(':');
        if (colon != std::string_view::npos) {
            p.host.assign(authority.data(), colon);
            p.port.assign(authority.data() + colon + 1, authority.size() - colon - 1);
        } else {
            p.host = std::string(authority);
        }
    }

    // path / query / fragment
    auto remainder = rest.substr(auth_end);
    if (!remainder.empty() && remainder.front() != '?' && remainder.front() != '#') {
        // path
        auto q = remainder.find('?');
        auto f = remainder.find('#');
        auto end = std::min(q == std::string_view::npos ? remainder.size() : q,
                            f == std::string_view::npos ? remainder.size() : f);
        p.path.assign(remainder.data(), end);
        remainder = remainder.substr(end);
    }
    if (!remainder.empty() && remainder.front() == '?') {
        auto f = remainder.find('#');
        auto end = f == std::string_view::npos ? remainder.size() : f;
        p.query.assign(remainder.data() + 1, end - 1);
        remainder = remainder.substr(end);
    }
    if (!remainder.empty() && remainder.front() == '#') {
        p.fragment.assign(remainder.data() + 1, remainder.size() - 1);
    }
    return p;
}

std::string build_url(const ParsedUrl& p, const std::string& port_override) {
    std::string out;
    out.reserve(p.scheme.size() + 3 + p.host.size() + 6 + p.path.size() + p.query.size());
    if (!p.scheme.empty()) {
        out += p.scheme;
        out += "://";
    }
    if (!p.host.empty()) {
        if (p.host_is_ipv6) {
            out += '[';
            out += p.host;
            out += ']';
        } else {
            out += p.host;
        }
    }
    if (!port_override.empty()) {
        out += ':';
        out += port_override;
    }
    out += p.path;
    if (!p.query.empty()) {
        out += '?';
        out += p.query;
    }
    if (!p.fragment.empty()) {
        out += '#';
        out += p.fragment;
    }
    return out;
}

/// Return the URL variants to try for Scheme B port normalization.
///
/// Mirrors ``_candidate_urls`` in the Python reference:
///   - non-standard explicit port  -> just the input URL
///   - https + no port             -> input + url with :443
///   - http  + no port             -> input + url with :80
///   - https + :443 (or http + :80) -> input + url with port stripped
std::vector<std::string> candidate_urls(std::string_view url) {
    std::vector<std::string> out;
    out.emplace_back(url);

    auto p = parse_url(url);
    if (p.host.empty()) return out;

    std::string standard;
    if (p.scheme == "http") standard = "80";
    else if (p.scheme == "https") standard = "443";
    if (standard.empty()) return out;

    if (p.port.empty()) {
        std::string with_port = build_url(p, standard);
        if (with_port != out[0]) out.push_back(std::move(with_port));
    } else if (p.port == standard) {
        std::string without_port = build_url(p, "");
        if (without_port != out[0]) out.push_back(std::move(without_port));
    }
    // else: explicit non-standard port — only as-is
    return out;
}

// ---------------------------------------------------------------------------
// Form-encoding helpers
// ---------------------------------------------------------------------------

/// Decode a single x-www-form-urlencoded token. ``+`` becomes space and
/// ``%xx`` becomes the byte. Invalid escapes pass through verbatim.
std::string urldecode_form(std::string_view in) {
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        char c = in[i];
        if (c == '+') {
            out.push_back(' ');
        } else if (c == '%' && i + 2 < in.size() &&
                   std::isxdigit(static_cast<unsigned char>(in[i + 1])) &&
                   std::isxdigit(static_cast<unsigned char>(in[i + 2]))) {
            auto hex_to_int = [](char h) {
                if (h >= '0' && h <= '9') return h - '0';
                if (h >= 'a' && h <= 'f') return h - 'a' + 10;
                if (h >= 'A' && h <= 'F') return h - 'A' + 10;
                return 0;
            };
            int hi = hex_to_int(in[i + 1]);
            int lo = hex_to_int(in[i + 2]);
            out.push_back(static_cast<char>((hi << 4) | lo));
            i += 2;
        } else {
            out.push_back(c);
        }
    }
    return out;
}

/// Best-effort parse of an x-www-form-urlencoded body. Returns an empty
/// list if the body is empty or contains nothing that decodes as form data.
///
/// Preserves submission order — required for repeated-key handling per
/// porting-sdk/webhooks.md.
std::vector<std::pair<std::string, std::string>>
parse_form_body(std::string_view body) {
    std::vector<std::pair<std::string, std::string>> items;
    if (body.empty()) return items;
    size_t i = 0;
    while (i < body.size()) {
        auto amp = body.find('&', i);
        auto end = amp == std::string_view::npos ? body.size() : amp;
        auto pair = body.substr(i, end - i);
        if (!pair.empty()) {
            auto eq = pair.find('=');
            std::string k, v;
            if (eq == std::string_view::npos) {
                k = urldecode_form(pair);
            } else {
                k = urldecode_form(pair.substr(0, eq));
                v = urldecode_form(pair.substr(eq + 1));
            }
            items.emplace_back(std::move(k), std::move(v));
        }
        if (amp == std::string_view::npos) break;
        i = amp + 1;
    }
    return items;
}

/// Concatenate form params per Scheme B rules:
///   - sort by key, ASCII ascending
///   - within repeated keys, preserve original submission order
///   - emit ``key + value`` for each pair
std::string sorted_concat_pairs(
    const std::vector<std::pair<std::string, std::string>>& items) {
    if (items.empty()) return {};
    // Stable sort by key — preserves order within repeated keys.
    auto sorted = items;
    std::stable_sort(sorted.begin(), sorted.end(),
                     [](const auto& a, const auto& b) { return a.first < b.first; });
    std::string out;
    for (const auto& kv : sorted) {
        out += kv.first;
        out += kv.second;
    }
    return out;
}

/// Concatenate FormParams (key -> [v1, v2, ...]) per Scheme B rules:
///   - sort by key, ASCII ascending
///   - within a key, preserve order of values exactly as supplied
///   - emit ``key + v1 + key + v2 + ...``
std::string sorted_concat_form_params(const FormParams& params) {
    if (params.empty()) return {};
    auto sorted = params;
    std::stable_sort(sorted.begin(), sorted.end(),
                     [](const auto& a, const auto& b) { return a.first < b.first; });
    std::string out;
    for (const auto& kv : sorted) {
        for (const auto& v : kv.second) {
            out += kv.first;
            out += v;
        }
    }
    return out;
}

/// Extract a query-param value by name from a raw query string. Returns
/// an empty optional if absent. Decodes the value as urlencoded.
std::string find_query_param(std::string_view query, std::string_view name) {
    size_t i = 0;
    while (i < query.size()) {
        auto amp = query.find('&', i);
        auto end = amp == std::string_view::npos ? query.size() : amp;
        auto pair = query.substr(i, end - i);
        auto eq = pair.find('=');
        std::string_view k = eq == std::string_view::npos ? pair : pair.substr(0, eq);
        if (k == name) {
            std::string_view raw_v = eq == std::string_view::npos
                                     ? std::string_view{}
                                     : pair.substr(eq + 1);
            return urldecode_form(raw_v);
        }
        if (amp == std::string_view::npos) break;
        i = amp + 1;
    }
    return {};
}

/// If URL has ``?bodySHA256=<hex>``, verify ``sha256_hex(raw_body)`` matches.
/// Returns ``true`` if absent (no constraint), or present and matches.
/// Returns ``false`` only when present and mismatches.
bool check_body_sha256(std::string_view url, std::string_view raw_body) {
    auto p = parse_url(url);
    if (p.query.empty()) return true;
    std::string expected = find_query_param(p.query, "bodySHA256");
    if (expected.empty()) return true;
    return safe_eq(sha256_hex(raw_body), expected);
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool ValidateWebhookSignature(std::string_view signing_key,
                              std::string_view signature,
                              std::string_view url,
                              std::string_view raw_body) {
    if (signing_key.empty()) {
        throw std::invalid_argument("signing_key is required");
    }
    if (signature.empty()) {
        return false;
    }

    // ------------------------------------------------------------------
    // Scheme A — RELAY/SWML/JSON: hex(HMAC-SHA1(key, url + raw_body))
    // ------------------------------------------------------------------
    {
        std::string msg;
        msg.reserve(url.size() + raw_body.size());
        msg.append(url.data(), url.size());
        msg.append(raw_body.data(), raw_body.size());
        std::string expected_a = hex_hmac_sha1(signing_key, msg);
        if (safe_eq(expected_a, signature)) {
            return true;
        }
    }

    // ------------------------------------------------------------------
    // Scheme B — Compat/cXML form: base64(HMAC-SHA1(key, url + sorted_concat))
    // Try with parsed form params; fall back to empty params for JSON-on-compat.
    // Try both candidate URLs (port-normalized variants).
    // ------------------------------------------------------------------
    auto parsed_params = parse_form_body(raw_body);
    std::vector<std::vector<std::pair<std::string, std::string>>> shapes;
    shapes.push_back(parsed_params);
    shapes.push_back({});  // empty params — JSON-on-compat fallback

    for (const auto& candidate : candidate_urls(url)) {
        for (const auto& shape : shapes) {
            std::string concat = sorted_concat_pairs(shape);
            std::string msg = candidate + concat;
            std::string expected_b = b64_hmac_sha1(signing_key, msg);
            if (safe_eq(expected_b, signature)) {
                if (check_body_sha256(candidate, raw_body)) {
                    return true;
                }
                // bodySHA256 mismatched — keep trying other shapes/urls.
            }
        }
    }
    return false;
}

bool ValidateRequest(std::string_view signing_key,
                     std::string_view signature,
                     std::string_view url,
                     const ParamsOrBody& params_or_raw_body) {
    if (signing_key.empty()) {
        throw std::invalid_argument("signing_key is required");
    }
    if (signature.empty()) {
        return false;
    }

    if (std::holds_alternative<std::string>(params_or_raw_body)) {
        const auto& body = std::get<std::string>(params_or_raw_body);
        return ValidateWebhookSignature(signing_key, signature, url, body);
    }

    const auto& params = std::get<FormParams>(params_or_raw_body);
    std::string concat = sorted_concat_form_params(params);
    for (const auto& candidate : candidate_urls(url)) {
        std::string msg = candidate + concat;
        std::string expected_b = b64_hmac_sha1(signing_key, msg);
        if (safe_eq(expected_b, signature)) {
            // bodySHA256 has no raw body to verify here — skip that check.
            return true;
        }
    }
    return false;
}

} // namespace security
} // namespace signalwire
