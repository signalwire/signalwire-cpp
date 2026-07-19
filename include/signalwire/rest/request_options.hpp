// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#pragma once

#include <atomic>
#include <cstdint>
#include <optional>
#include <set>

namespace signalwire {
namespace rest {

/// RequestOptions — the REST request-options envelope (plan 4.2).
///
/// A single value object controlling per-request transport behavior: timeout,
/// retries (with an idempotency-aware retry policy + exponential backoff), and
/// cooperative cancellation. Mirrors Python's
/// ``signalwire.rest._request_options.RequestOptions``.
///
/// Supplied at two levels:
///  - **Client default**: ``RestClient(..., request_options)`` stored on the
///    ``HttpClient`` and applied to every request.
///  - **Per-request override**: each verb accepts an optional ``request_options``
///    that *shallow-overrides* the client default — an unset (``nullopt`` /
///    ``nullptr``) field falls back to the client default, then the built-in.
///
/// Every field is optional; an unset field means "inherit". The built-in
/// defaults (the contract floor) live on ``HttpClient`` and are resolved at
/// apply-time (per-request over client-default over built-in).
///
/// ``abort_signal`` fidelity is per-port idiom: in C++ (a synchronous httplib
/// client) an in-flight blocking socket read cannot be interrupted without a
/// thread, so cancellation is checked cooperatively *before* each attempt — the
/// honest, portable minimum. It is a non-owning pointer to a caller-owned
/// ``std::atomic<bool>``; a truthy value raises the transport error before the
/// send. ``nullptr`` == no cancellation (the default).
struct RequestOptions {
  /// Max wall-clock seconds per attempt; on exceed the request raises the
  /// transport-error type. Built-in default ``30.0``.
  std::optional<double> timeout;
  /// Number of RETRY attempts (total attempts = ``retries + 1``) on a retryable
  /// failure. Built-in default ``0`` (opt-in resilience).
  std::optional<int> retries;
  /// HTTP statuses that trigger a retry for an idempotent method. Built-in
  /// ``{429, 500, 502, 503, 504}``.
  std::optional<std::set<int>> retry_on_status;
  /// Base seconds for exponential backoff between retries
  /// (``backoff * 2 ** (attempt - 1)``), honoring ``Retry-After`` when present.
  /// Built-in ``0.5``.
  std::optional<double> retry_backoff;
  /// Cooperative-cancellation flag (non-owning); checked before each attempt.
  /// Built-in ``nullptr``.
  std::atomic<bool>* abort_signal = nullptr;

  /// Return ``*this`` with any set (non-empty) field of ``override_opts``
  /// applied. This is the per-request-over-client-default shallow merge: an
  /// unset field on ``override_opts`` leaves this value intact. Mirrors Python's
  /// ``RequestOptions.merge``.
  RequestOptions merge(const RequestOptions& override_opts) const {
    RequestOptions out = *this;
    if (override_opts.timeout.has_value()) out.timeout = override_opts.timeout;
    if (override_opts.retries.has_value()) out.retries = override_opts.retries;
    if (override_opts.retry_on_status.has_value())
      out.retry_on_status = override_opts.retry_on_status;
    if (override_opts.retry_backoff.has_value()) out.retry_backoff = override_opts.retry_backoff;
    if (override_opts.abort_signal != nullptr) out.abort_signal = override_opts.abort_signal;
    return out;
  }
};

}  // namespace rest
}  // namespace signalwire
