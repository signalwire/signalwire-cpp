// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#pragma once

#include <functional>
#include <string>
#include <string_view>

#include "signalwire/security/webhook_validator.hpp"

namespace httplib { class Request; class Response; }

namespace signalwire {
namespace security {

/// A cpp-httplib handler signature, lifted into a typedef so the adapter
/// stays framework-agnostic at the call site (the only deps needed by
/// this header are a forward declaration of httplib::Request/Response).
using HttpHandler = std::function<void(const httplib::Request&, httplib::Response&)>;

/// Optional knobs for ``WrapWithSignatureValidation``. ``trust_proxy``
/// controls whether ``X-Forwarded-Proto`` / ``X-Forwarded-Host`` headers
/// are honored when reconstructing the public URL — opt-in because proxy
/// headers are spoofable. ``proxy_url_base`` is consulted before the
/// proxy headers and is meant for ``SWML_PROXY_URL_BASE`` callers.
struct WebhookValidatorOptions {
    /// If true, honor ``X-Forwarded-Proto`` / ``X-Forwarded-Host`` when
    /// reconstructing the URL. Default false.
    bool trust_proxy = false;

    /// Optional base URL (e.g. ``https://abcd.ngrok.io``) joined with
    /// the request path + query when reconstructing the URL. Wins over
    /// proxy headers and ``request.host``.
    std::string proxy_url_base;
};

/// Wrap a downstream cpp-httplib POST handler with SignalWire webhook
/// signature validation.
///
/// Behavior:
///
/// 1. Read ``req.body`` (cpp-httplib has already buffered it).
/// 2. Pull ``X-SignalWire-Signature`` (or the ``X-Twilio-Signature``
///    legacy alias) from headers.
/// 3. Reconstruct the public URL the platform POSTed to (proxy-aware).
/// 4. Call ``ValidateWebhookSignature``.
/// 5. On invalid / missing signature: respond ``403 Forbidden`` with no
///    body detail. The downstream handler is NOT called. Per the
///    porting-sdk spec, the validator MUST NOT log which branch failed
///    or the expected signature.
/// 6. On valid: call ``downstream(req, res)``. The handler can read
///    ``req.body`` directly — cpp-httplib buffers it for repeat reads.
///
/// @param signing_key Customer's Signing Key (SignalWire Dashboard → API
///                    Credentials). MUST NOT be empty — empty throws
///                    ``std::invalid_argument`` at wrap time, that's a
///                    programming error not a runtime failure.
/// @param downstream  Handler to invoke on a valid signature.
/// @param opts        Optional URL reconstruction knobs.
///
/// @return A handler suitable for ``server.Post(path, handler)``.
///
/// @throws std::invalid_argument if ``signing_key`` is empty.
HttpHandler WrapWithSignatureValidation(std::string_view signing_key,
                                        HttpHandler downstream,
                                        WebhookValidatorOptions opts = {});

} // namespace security
} // namespace signalwire
