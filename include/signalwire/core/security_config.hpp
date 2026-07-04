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
  /// validation fails; otherwise a language-neutral option object with keys
  /// ``ssl_enabled``, ``cert_path``, ``key_path`` (Python returns uvicorn
  /// ``ssl_certfile``/``ssl_keyfile`` kwargs; the C++/Java idiom is a neutral
  /// map the web service consumes).
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

  // Accessors (parity with the Python public attributes).
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
