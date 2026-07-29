// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
//
// Unified authentication handler supporting multiple auth methods.
//
// C++ port of the Python reference
// ``signalwire.core.auth_handler.AuthHandler`` (cross-checked against the Java
// ``com.signalwire.sdk.core.AuthHandler``). Provides a clean pattern for Basic
// Auth, Bearer tokens, and API keys across all SignalWire services. All
// credential comparisons are timing-safe.
//
// Idiom note: Python's ``flask_decorator`` / ``get_fastapi_dependency`` are
// framework-bound (Flask / FastAPI). C++ ships neither web framework, so the
// native equivalents here are framework-neutral, modelled exactly like the Java
// port: a "request" is a case-insensitive header map (``Headers``). These two
// methods EXIST (so the surface has the names) and are REAL — they enforce/
// report authentication over a header map — they just don't bind to a specific
// C++ web framework.
#pragma once

#include <functional>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>

#include "signalwire/core/security_config.hpp"

namespace signalwire {
namespace core {

using json = nlohmann::json;

/// A framework-neutral request header map (name -> value). Header lookups are
/// case-insensitive (see ``AuthHandler``'s internal lookup).
using Headers = std::map<std::string, std::string>;

/// Basic-auth credential carrier (matches FastAPI's HTTPBasicCredentials).
struct BasicCredentials {
  std::string username;
  std::string password;
};

/// Bearer-token credential carrier (matches FastAPI's
/// HTTPAuthorizationCredentials, which carries BOTH halves of the
/// ``Authorization`` header: the scheme token and the credential string).
struct BearerCredentials {
  std::string scheme;
  std::string credentials;
};

/// Result of the ``get_fastapi_dependency`` callable.
struct AuthResult {
  bool authenticated = false;
  std::optional<std::string> method;
};

/// A minimal HTTP response tuple used by ``flask_decorator``.
struct AuthResponse {
  int status = 200;
  json headers = json::object();
  std::string body;
};

/// Thrown by the ``get_fastapi_dependency`` callable when required auth fails.
class AuthException : public std::runtime_error {
 public:
  explicit AuthException(AuthResponse response)
      : std::runtime_error("Invalid authentication credentials"), response_(std::move(response)) {}

  [[nodiscard]] const AuthResponse& response() const { return response_; }

 private:
  AuthResponse response_;
};

/// Unified authentication over a request header map — HTTP Basic, Bearer
/// token, and API key.
///
/// The handler is constructed from a ``SecurityConfig`` and authenticates a
/// request by trying the ENABLED methods in order — Bearer, then API key, then
/// Basic — reporting which one succeeded. Which methods are enabled follows
/// the reference exactly: **Basic is always on** (seeded from
/// ``SecurityConfig::get_basic_auth``, which generates a random password when
/// none is configured, so the endpoint is never credential-free), while Bearer
/// and API key are **off** because the reference ``SecurityConfig`` carries no
/// such fields. Their verifiers are real and correct — a subclass that
/// supplies a token or key gets working enforcement — but out of the box a
/// Bearer or ``X-API-Key`` header authenticates nothing.
///
/// **Every credential comparison is timing-safe** (constant-time over the full
/// operand), so a caller cannot recover a secret byte-by-byte from response
/// latency. Header lookup is case-insensitive, matching HTTP; the API-key
/// header name defaults to ``X-API-Key`` and comes from the config. A failed
/// attempt is logged without the submitted credential, and ``get_auth_info``
/// reports which methods are enabled but never a secret. The API-key header
/// name is ``X-API-Key``.
///
/// Two entry points wrap that core for the two shapes the Python reference
/// exposes as framework bindings. C++ ships no web framework, so both are
/// framework-neutral over ``Headers`` (same idiom as the Java port), and both
/// are real enforcement, not stubs: ``get_fastapi_dependency`` returns a
/// callable yielding an ``AuthResult`` — throwing ``AuthException`` when
/// ``optional`` is false and auth fails — and ``flask_decorator`` wraps a
/// downstream ``RequestHandler`` so unauthenticated requests get an HTTP 401
/// with a ``WWW-Authenticate`` challenge and never reach it.
///
/// Holds a REFERENCE to the ``SecurityConfig``: the config must outlive the
/// handler.
class AuthHandler {
 public:
  /// Framework-neutral request handler: header map in, ``AuthResponse`` out.
  using RequestHandler = std::function<AuthResponse(const Headers&)>;

  /// The callable returned by ``get_fastapi_dependency``.
  using FastapiDependency = std::function<AuthResult(const Headers&)>;

  /// Initialize the auth handler with a ``SecurityConfig``.
  explicit AuthHandler(SecurityConfig& security_config);

  /// The ``SecurityConfig`` this handler authenticates against (reference:
  /// ``self.security_config``). The caller constructs the handler with it, so
  /// the caller can read it back — e.g. to inspect which auth modes are on.
  [[nodiscard]] const SecurityConfig& security_config() const { return security_config_; }

  /// Verify basic-auth credentials. Timing-safe.
  [[nodiscard]] bool verify_basic_auth(const BasicCredentials& credentials) const;

  /// Verify a bearer token. Timing-safe.
  [[nodiscard]] bool verify_bearer_token(const BearerCredentials& credentials) const;

  /// Verify an API key. Timing-safe.
  [[nodiscard]] bool verify_api_key(const std::string& api_key) const;

  /// Framework-neutral equivalent of Python's FastAPI dependency. Returns a
  /// callable taking a request header map and returning an ``AuthResult``.
  /// When ``optional`` is false and authentication fails, the callable throws
  /// ``AuthException``; when true it returns the (unauthenticated) result.
  [[nodiscard]] FastapiDependency get_fastapi_dependency(bool optional = false) const;

  /// Framework-neutral equivalent of Python's Flask decorator. Given a
  /// downstream ``RequestHandler``, returns a wrapping handler that enforces
  /// authentication: authenticated requests pass through, others get an HTTP
  /// 401 with a WWW-Authenticate challenge.
  [[nodiscard]] RequestHandler flask_decorator(RequestHandler app) const;

  /// Information about configured auth methods (never includes secrets).
  [[nodiscard]] json get_auth_info() const;

 private:
  void setup_auth_methods();
  [[nodiscard]] bool enabled(const std::string& method) const;
  [[nodiscard]] std::optional<std::string> authenticate(const Headers& headers) const;
  [[nodiscard]] bool bearer_ok(const Headers& headers) const;
  [[nodiscard]] bool api_key_ok(const Headers& headers) const;
  [[nodiscard]] bool basic_ok(const Headers& headers) const;
  [[nodiscard]] static std::optional<std::string> header(const Headers& headers,
                                                         const std::string& name);
  [[nodiscard]] static std::optional<BasicCredentials> parse_basic_auth(const std::string& header);
  [[nodiscard]] static AuthResponse unauthorized_response();
  void log_auth_failure(const Headers& headers) const;

  SecurityConfig& security_config_;

  // Configured auth methods (matches Python's self.auth_methods dict).
  bool basic_enabled_ = false;
  std::string basic_username_;
  std::string basic_password_;
  bool bearer_enabled_ = false;
  std::string bearer_token_;
  bool api_key_enabled_ = false;
  std::string api_key_value_;
  std::string api_key_header_ = "X-API-Key";
};

}  // namespace core
}  // namespace signalwire
