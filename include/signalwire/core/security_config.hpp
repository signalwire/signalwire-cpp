// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
//
// Unified security configuration for SignalWire services.
//
// C++ port of the Python reference
// ``signalwire.core.security_config.SecurityConfig`` (cross-checked against the
// Java ``com.signalwire.sdk.core.SecurityConfig``). Provides centralized
// security settings (SSL, allowed hosts, CORS, security headers, basic auth)
// consumed by the web/agent services so behavior stays consistent. Defaults are
// applied first, then environment variables (backward compatibility), then a
// config file if available (highest priority).
#pragma once

#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace signalwire {
namespace core {

using json = nlohmann::json;

/// Result of ``SecurityConfig::validate_ssl_config``: a validity flag plus an
/// optional error message (Python returns ``(bool, str | None)``).
struct SslValidationResult {
  bool valid = false;
  std::optional<std::string> error;
};

/// Centralized security settings for a SignalWire service — SSL, allowed
/// hosts, CORS, response security headers, request limits, and basic-auth
/// credentials.
///
/// The web/agent services read their security posture from one of these so the
/// behaviour is consistent across them. Settings are resolved in three layers,
/// each overriding the last: **built-in defaults**, then the ``SWML_*``
/// **environment variables** named by the class constants below, then a
/// **config file**'s ``security`` section (highest priority) — located either
/// from an explicit path or from the service name.
///
/// Security-relevant behaviours worth knowing before you deploy:
///   * ``get_basic_auth`` never returns an empty password. When none is
///     configured it GENERATES a random one and warns once — that password
///     lives only in this process, so external callers who do not know it get
///     HTTP 401. Configure ``SWML_BASIC_AUTH_USER``/``_PASSWORD`` for anything
///     a client must reach.
///   * ``validate_ssl_config`` is always valid when SSL is disabled; with SSL
///     enabled it requires cert and key paths that are set AND exist on disk,
///     and ``get_ssl_context_kwargs`` returns an EMPTY object when SSL is off
///     OR that validation fails (logging the reason) — so a caller that binds
///     TLS only on a non-empty result will not silently serve plaintext.
///   * ``should_allow_host`` treats ``*`` in the allowed list as allow-all.
///   * ``get_security_headers`` adds ``Strict-Transport-Security`` only when
///     the caller says the connection is HTTPS and HSTS is enabled — sending
///     HSTS over plaintext is meaningless and can lock out a host.
///   * ``log_config`` never logs a secret.
///
/// Defaults: SSL off, verify mode ``CERT_REQUIRED``, 10 MiB max request, 60
/// requests/min rate limit, 30 s request timeout, HSTS on with a one-year
/// max-age.
class SecurityConfig {
 public:
  // Security environment variable names (mirror the Python class constants).
  static constexpr const char* SSL_ENABLED = "SWML_SSL_ENABLED";
  static constexpr const char* SSL_CERT_PATH = "SWML_SSL_CERT_PATH";
  static constexpr const char* SSL_KEY_PATH = "SWML_SSL_KEY_PATH";
  static constexpr const char* SSL_DOMAIN = "SWML_DOMAIN";
  static constexpr const char* SSL_VERIFY_MODE = "SWML_SSL_VERIFY_MODE";
  static constexpr const char* ALLOWED_HOSTS = "SWML_ALLOWED_HOSTS";
  static constexpr const char* CORS_ORIGINS = "SWML_CORS_ORIGINS";
  static constexpr const char* MAX_REQUEST_SIZE = "SWML_MAX_REQUEST_SIZE";
  static constexpr const char* RATE_LIMIT = "SWML_RATE_LIMIT";
  static constexpr const char* REQUEST_TIMEOUT = "SWML_REQUEST_TIMEOUT";
  static constexpr const char* USE_HSTS = "SWML_USE_HSTS";
  static constexpr const char* HSTS_MAX_AGE = "SWML_HSTS_MAX_AGE";
  static constexpr const char* BASIC_AUTH_USER = "SWML_BASIC_AUTH_USER";
  static constexpr const char* BASIC_AUTH_PASSWORD = "SWML_BASIC_AUTH_PASSWORD";

  /// Initialize security configuration.
  /// @param config_file  Optional explicit config file path.
  /// @param service_name Optional service name used to locate a config file.
  explicit SecurityConfig(const std::optional<std::string>& config_file = std::nullopt,
                          const std::optional<std::string>& service_name = std::nullopt);

  /// Load configuration from environment variables (public; part of the
  /// Python surface — called by the ctor and re-callable).
  void load_from_env();

  /// Validate SSL configuration. When SSL is disabled the result is always
  /// valid. Otherwise cert/key paths must be set and exist on disk.
  [[nodiscard]] SslValidationResult validate_ssl_config() const;

  /// SSL options for binding an HTTPS server. Empty when SSL is disabled or
  /// validation fails; otherwise EXACTLY the reference's two keys and nothing
  /// else — ``ssl_certfile`` (the cert path) and ``ssl_keyfile`` (the key
  /// path). This is byte-identical to Python's ``get_ssl_context_kwargs``;
  /// C++ does NOT substitute a "neutral" ``{ssl_enabled, cert_path,
  /// key_path}`` map. Pinned by
  /// ``security_config_ssl_context_kwargs_matches_python_when_enabled``.
  [[nodiscard]] json get_ssl_context_kwargs() const;

  /// Get basic auth credentials, generating a random URL-safe password when
  /// none is configured. Returns ``{username, password}``.
  [[nodiscard]] std::pair<std::string, std::string> get_basic_auth();

  /// Security headers to add to responses. When ``is_https`` is true and HSTS
  /// is enabled a ``Strict-Transport-Security`` header is included.
  [[nodiscard]] json get_security_headers(bool is_https = false) const;

  /// Check if a host is allowed (``*`` in the allowed list allows all).
  [[nodiscard]] bool should_allow_host(const std::string& host) const;

  /// CORS configuration.
  [[nodiscard]] json get_cors_config() const;

  /// URL scheme based on SSL configuration ("https" or "http").
  [[nodiscard]] std::string get_url_scheme() const;

  /// Log the current security configuration (never logs secrets).
  void log_config(const std::string& service_name) const;

  // Accessors (matches the Python public attributes).
  [[nodiscard]] bool ssl_enabled() const { return ssl_enabled_; }
  [[nodiscard]] const std::optional<std::string>& ssl_cert_path() const { return ssl_cert_path_; }
  [[nodiscard]] const std::optional<std::string>& ssl_key_path() const { return ssl_key_path_; }
  [[nodiscard]] const std::optional<std::string>& domain() const { return domain_; }
  [[nodiscard]] const std::string& ssl_verify_mode() const { return ssl_verify_mode_; }
  [[nodiscard]] const std::vector<std::string>& allowed_hosts() const { return allowed_hosts_; }
  [[nodiscard]] const std::vector<std::string>& cors_origins() const { return cors_origins_; }
  [[nodiscard]] long max_request_size() const { return max_request_size_; }
  [[nodiscard]] int rate_limit() const { return rate_limit_; }
  [[nodiscard]] int request_timeout() const { return request_timeout_; }
  [[nodiscard]] bool use_hsts() const { return use_hsts_; }
  [[nodiscard]] long hsts_max_age() const { return hsts_max_age_; }
  [[nodiscard]] const std::optional<std::string>& basic_auth_user() const {
    return basic_auth_user_;
  }
  [[nodiscard]] const std::optional<std::string>& basic_auth_password() const {
    return basic_auth_password_;
  }

 private:
  void set_defaults();
  void load_config_file(const std::optional<std::string>& config_file,
                        const std::optional<std::string>& service_name);
  void apply_security_section(const json& section);
  static std::vector<std::string> parse_list(const std::string& value);
  static std::vector<std::string> parse_list_json(const json& value);

  bool ssl_enabled_ = false;
  std::optional<std::string> ssl_cert_path_;
  std::optional<std::string> ssl_key_path_;
  std::optional<std::string> domain_;
  std::string ssl_verify_mode_ = "CERT_REQUIRED";
  std::vector<std::string> allowed_hosts_;
  std::vector<std::string> cors_origins_;
  long max_request_size_ = 10L * 1024 * 1024;
  int rate_limit_ = 60;
  int request_timeout_ = 30;
  bool use_hsts_ = true;
  long hsts_max_age_ = 31536000L;
  std::optional<std::string> basic_auth_user_;
  std::optional<std::string> basic_auth_password_;
  mutable bool basic_auth_autogen_warned_ = false;
};

}  // namespace core
}  // namespace signalwire
