// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <variant>
#include <utility>
#include <stdexcept>

/// SignalWire webhook signature validation.
///
/// Implements both schemes from porting-sdk/webhooks.md:
///
/// - Scheme A (RELAY/SWML/JSON): hex(HMAC-SHA1(key, url + raw_body))
/// - Scheme B (Compat/cXML form): base64(HMAC-SHA1(key, url + sortedFormParams))
///   with optional bodySHA256 query-param fallback for JSON-on-compat-surface.
///
/// All comparisons use ``CRYPTO_memcmp`` (constant-time) so the secret
/// is not leaked over repeated requests. The implementation MUST NOT log
/// keys, signatures, or which branch failed — that's a spec requirement.
namespace signalwire {
namespace security {

/// Pre-parsed form parameters for ``ValidateRequest``: ordered list of
/// (key, list-of-values) tuples. Repeated keys are represented as one
/// entry whose values vector holds each occurrence in submission order.
///
/// Used as one alternative in ``ParamsOrBody``; the other alternative is
/// the raw body string.
using FormParams = std::vector<std::pair<std::string, std::vector<std::string>>>;

/// Drop-in shape for ``ValidateRequest`` mirroring
/// ``@signalwire/compatibility-api``'s ``RestClient.validateRequest``:
/// either a raw body string (delegates to the combined validator) or a
/// pre-parsed form-params list (runs Scheme B directly).
using ParamsOrBody = std::variant<std::string, FormParams>;

/// Validate a SignalWire webhook signature against both schemes.
///
/// Tries Scheme A (hex JSON) first; on miss falls back to Scheme B
/// (base64 form) with URL port normalization, repeated-key handling,
/// and optional ``?bodySHA256=`` body-hash check for JSON bodies on
/// the compat surface.
///
/// @param signing_key The customer's Signing Key. UTF-8 string. MUST NOT
///                    be empty — empty throws ``std::invalid_argument``,
///                    that's a programming error not a validation
///                    failure.
/// @param signature   The ``X-SignalWire-Signature`` header value (or
///                    the legacy ``X-Twilio-Signature`` alias). Empty
///                    returns ``false`` without throwing.
/// @param url         Full URL SignalWire POSTed to (scheme, host,
///                    optional port, path, query) — must match what the
///                    platform saw, see the URL-reconstruction section
///                    of porting-sdk/webhooks.md.
/// @param raw_body    Raw request body bytes as a UTF-8 string, BEFORE
///                    any JSON / form parsing. Re-serialized JSON breaks
///                    Scheme A.
///
/// @return ``true`` if either scheme matches; ``false`` otherwise.
///
/// @throws std::invalid_argument when ``signing_key`` is empty.
bool ValidateWebhookSignature(std::string_view signing_key,
                              std::string_view signature,
                              std::string_view url,
                              std::string_view raw_body);

/// Legacy ``@signalwire/compatibility-api`` drop-in entry point.
///
/// If ``params_or_raw_body`` holds a ``std::string``, delegates to
/// ``ValidateWebhookSignature`` (Scheme A then Scheme B with parsed form).
///
/// If it holds a ``FormParams``, treats it as pre-parsed form params and
/// runs Scheme B directly (with URL port normalization).
///
/// @throws std::invalid_argument when ``signing_key`` is empty.
bool ValidateRequest(std::string_view signing_key,
                     std::string_view signature,
                     std::string_view url,
                     const ParamsOrBody& params_or_raw_body);

} // namespace security
} // namespace signalwire
