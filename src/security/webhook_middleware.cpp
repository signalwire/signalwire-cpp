// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
//
// cpp-httplib middleware adapter for SignalWire webhook signature
// validation. See include/signalwire/security/webhook_middleware.hpp for
// the contract and porting-sdk/webhooks.md for the spec.

#include "signalwire/security/webhook_middleware.hpp"
#include "signalwire/common.hpp"
#include "httplib.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>

namespace signalwire {
namespace security {

namespace {

/// Lower-case ASCII without modifying multi-byte chars. Headers in
/// cpp-httplib are case-sensitive in the multimap, so we need to look up
/// in a case-insensitive way.
std::string ascii_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}

/// Find a header by case-insensitive key. Returns empty string if absent.
std::string header_ci(const httplib::Request& req, const char* name) {
    std::string want = ascii_lower(name);
    for (const auto& kv : req.headers) {
        if (ascii_lower(kv.first) == want) return kv.second;
    }
    return {};
}

/// Reconstruct the URL SignalWire actually POSTed to. Resolution order:
///   1. ``opts.proxy_url_base`` (or ``SWML_PROXY_URL_BASE`` env)
///      joined with the request path + query.
///   2. ``X-Forwarded-Proto`` / ``X-Forwarded-Host`` if ``trust_proxy``
///      is true and both headers are present.
///   3. ``Host`` header + assumed scheme. cpp-httplib does not expose
///      whether SSL was negotiated at the TLS layer; we default to
///      ``https`` because production webhooks are always TLS-terminated.
///      Tests can override via ``proxy_url_base``.
std::string reconstruct_url(const httplib::Request& req,
                            const WebhookValidatorOptions& opts) {
    std::string path_and_query = req.path;
    if (!req.params.empty()) {
        path_and_query += "?";
        bool first = true;
        for (const auto& kv : req.params) {
            if (!first) path_and_query += "&";
            first = false;
            path_and_query += url_encode(kv.first);
            path_and_query += "=";
            path_and_query += url_encode(kv.second);
        }
    }

    std::string proxy_base = opts.proxy_url_base;
    if (proxy_base.empty()) {
        proxy_base = get_env("SWML_PROXY_URL_BASE", "");
    }
    if (!proxy_base.empty()) {
        // Strip trailing slash to avoid ``host//path``.
        while (!proxy_base.empty() && proxy_base.back() == '/') {
            proxy_base.pop_back();
        }
        return proxy_base + path_and_query;
    }

    if (opts.trust_proxy) {
        std::string fwd_host = header_ci(req, "X-Forwarded-Host");
        std::string fwd_proto = header_ci(req, "X-Forwarded-Proto");
        if (fwd_proto.empty()) fwd_proto = "https";
        if (!fwd_host.empty()) {
            return fwd_proto + "://" + fwd_host + path_and_query;
        }
    }

    std::string host = header_ci(req, "Host");
    // cpp-httplib doesn't expose the TLS-vs-plaintext distinction at the
    // request level. Default to https — that's what production webhook
    // traffic uses. Plaintext-only tests should pass an explicit
    // proxy_url_base or run behind a proxy.
    std::string scheme = "https";
    if (host.empty()) {
        host = "localhost";
        scheme = "http";
    }
    return scheme + "://" + host + path_and_query;
}

void send_403(httplib::Response& res) {
    // No body detail — per spec, validators MUST NOT leak which branch
    // failed (Scheme A vs Scheme B vs URL mismatch vs bodySHA256).
    res.status = 403;
    res.set_content("Forbidden", "text/plain");
}

} // namespace

HttpHandler WrapWithSignatureValidation(std::string_view signing_key,
                                        HttpHandler downstream,
                                        WebhookValidatorOptions opts) {
    if (signing_key.empty()) {
        throw std::invalid_argument("signing_key is required");
    }
    if (!downstream) {
        throw std::invalid_argument("downstream handler is required");
    }
    // Capture by value: the wrapper owns its own copy of the key for
    // the lifetime of the returned closure. Never logged anywhere.
    std::string key_copy(signing_key);
    auto opts_copy = opts;
    auto handler = std::move(downstream);

    return [key_copy = std::move(key_copy), opts_copy = std::move(opts_copy),
            handler = std::move(handler)]
           (const httplib::Request& req, httplib::Response& res) {
        std::string sig = header_ci(req, "X-SignalWire-Signature");
        if (sig.empty()) {
            sig = header_ci(req, "X-Twilio-Signature");
        }
        if (sig.empty()) {
            send_403(res);
            return;
        }

        std::string url = reconstruct_url(req, opts_copy);

        bool ok = false;
        try {
            ok = ValidateWebhookSignature(key_copy, sig, url, req.body);
        } catch (...) {
            // Programming errors (e.g. empty key — but we checked at
            // wrap time) or any other surprise: treat as 403 without
            // leaking the cause.
            send_403(res);
            return;
        }

        if (!ok) {
            send_403(res);
            return;
        }

        handler(req, res);
    };
}

} // namespace security
} // namespace signalwire
