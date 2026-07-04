// SecurityConfig tests (signalwire::core::SecurityConfig)

#include <cstdlib>

#include "signalwire/core/security_config.hpp"

using signalwire::core::SecurityConfig;

namespace {

// Clear every SWML_* env var this test suite touches so each construction
// starts from a known state regardless of the ambient environment.
void clear_security_env() {
  const char* keys[] = {"SWML_SSL_ENABLED",     "SWML_SSL_CERT_PATH",      "SWML_SSL_KEY_PATH",
                        "SWML_DOMAIN",          "SWML_SSL_VERIFY_MODE",    "SWML_ALLOWED_HOSTS",
                        "SWML_CORS_ORIGINS",    "SWML_MAX_REQUEST_SIZE",   "SWML_RATE_LIMIT",
                        "SWML_REQUEST_TIMEOUT", "SWML_USE_HSTS",           "SWML_HSTS_MAX_AGE",
                        "SWML_BASIC_AUTH_USER", "SWML_BASIC_AUTH_PASSWORD"};
  for (const char* k : keys) {
    unsetenv(k);
  }
}

}  // namespace

TEST(security_config_defaults) {
  clear_security_env();
  SecurityConfig cfg;
  ASSERT_FALSE(cfg.ssl_enabled());
  ASSERT_EQ(cfg.get_url_scheme(), std::string("http"));
  ASSERT_EQ(cfg.ssl_verify_mode(), std::string("CERT_REQUIRED"));
  ASSERT_EQ(cfg.max_request_size(), 10L * 1024 * 1024);
  ASSERT_EQ(cfg.rate_limit(), 60);
  ASSERT_EQ(cfg.request_timeout(), 30);
  ASSERT_TRUE(cfg.use_hsts());
  ASSERT_EQ(cfg.hsts_max_age(), 31536000L);
  // Default allowed hosts is "*", so every host is allowed.
  ASSERT_TRUE(cfg.should_allow_host("example.com"));
  ASSERT_EQ(cfg.allowed_hosts().size(), 1u);
  ASSERT_EQ(cfg.allowed_hosts()[0], std::string("*"));
  return true;
}

TEST(security_config_ssl_enabled_from_env) {
  clear_security_env();
  setenv("SWML_SSL_ENABLED", "true", 1);
  SecurityConfig cfg;
  ASSERT_TRUE(cfg.ssl_enabled());
  ASSERT_EQ(cfg.get_url_scheme(), std::string("https"));
  clear_security_env();
  return true;
}

TEST(security_config_ssl_enabled_variants) {
  clear_security_env();
  for (const char* v : {"1", "yes", "TRUE", "Yes"}) {
    setenv("SWML_SSL_ENABLED", v, 1);
    SecurityConfig cfg;
    ASSERT_TRUE(cfg.ssl_enabled());
  }
  setenv("SWML_SSL_ENABLED", "no", 1);
  SecurityConfig cfg_off;
  ASSERT_FALSE(cfg_off.ssl_enabled());
  clear_security_env();
  return true;
}

TEST(security_config_validate_ssl_missing_paths) {
  clear_security_env();
  setenv("SWML_SSL_ENABLED", "true", 1);
  SecurityConfig cfg;
  auto res = cfg.validate_ssl_config();
  ASSERT_FALSE(res.valid);
  ASSERT_TRUE(res.error.has_value());
  // Cert path is checked first.
  ASSERT_TRUE(res.error->find("CERT_PATH") != std::string::npos);
  clear_security_env();
  return true;
}

TEST(security_config_validate_ssl_disabled_is_valid) {
  clear_security_env();
  SecurityConfig cfg;  // SSL off
  auto res = cfg.validate_ssl_config();
  ASSERT_TRUE(res.valid);
  ASSERT_FALSE(res.error.has_value());
  return true;
}

TEST(security_config_ssl_context_kwargs_empty_when_disabled) {
  clear_security_env();
  SecurityConfig cfg;
  auto kwargs = cfg.get_ssl_context_kwargs();
  ASSERT_TRUE(kwargs.is_object());
  ASSERT_TRUE(kwargs.empty());
  return true;
}

TEST(security_config_allowed_hosts_from_env) {
  clear_security_env();
  setenv("SWML_ALLOWED_HOSTS", "a.com, b.com ,c.com", 1);
  SecurityConfig cfg;
  ASSERT_EQ(cfg.allowed_hosts().size(), 3u);
  ASSERT_TRUE(cfg.should_allow_host("a.com"));
  ASSERT_TRUE(cfg.should_allow_host("b.com"));
  ASSERT_TRUE(cfg.should_allow_host("c.com"));
  ASSERT_FALSE(cfg.should_allow_host("d.com"));
  clear_security_env();
  return true;
}

TEST(security_config_security_headers) {
  clear_security_env();
  SecurityConfig cfg;
  auto headers = cfg.get_security_headers(false);
  ASSERT_EQ(headers["X-Content-Type-Options"], std::string("nosniff"));
  ASSERT_EQ(headers["X-Frame-Options"], std::string("DENY"));
  ASSERT_EQ(headers["X-XSS-Protection"], std::string("1; mode=block"));
  ASSERT_EQ(headers["Referrer-Policy"], std::string("strict-origin-when-cross-origin"));
  // No HSTS header when not https.
  ASSERT_FALSE(headers.contains("Strict-Transport-Security"));
  return true;
}

TEST(security_config_hsts_header_when_https) {
  clear_security_env();
  SecurityConfig cfg;  // use_hsts defaults true
  auto headers = cfg.get_security_headers(true);
  ASSERT_TRUE(headers.contains("Strict-Transport-Security"));
  ASSERT_TRUE(headers["Strict-Transport-Security"].get<std::string>().find("max-age=31536000") !=
              std::string::npos);
  return true;
}

TEST(security_config_hsts_disabled) {
  clear_security_env();
  setenv("SWML_USE_HSTS", "false", 1);
  SecurityConfig cfg;
  ASSERT_FALSE(cfg.use_hsts());
  auto headers = cfg.get_security_headers(true);
  ASSERT_FALSE(headers.contains("Strict-Transport-Security"));
  clear_security_env();
  return true;
}

TEST(security_config_cors_config) {
  clear_security_env();
  setenv("SWML_CORS_ORIGINS", "https://x.com,https://y.com", 1);
  SecurityConfig cfg;
  auto cors = cfg.get_cors_config();
  ASSERT_TRUE(cors["allow_credentials"].get<bool>());
  ASSERT_EQ(cors["allow_methods"].size(), 1u);
  ASSERT_EQ(cors["allow_methods"][0], std::string("*"));
  ASSERT_EQ(cors["allow_origins"].size(), 2u);
  ASSERT_EQ(cors["allow_origins"][0], std::string("https://x.com"));
  clear_security_env();
  return true;
}

TEST(security_config_basic_auth_autogenerates) {
  clear_security_env();
  SecurityConfig cfg;
  auto [user, pass] = cfg.get_basic_auth();
  ASSERT_EQ(user, std::string("signalwire"));
  ASSERT_FALSE(pass.empty());
  // Same instance returns the same generated password on a second call.
  auto [user2, pass2] = cfg.get_basic_auth();
  ASSERT_EQ(pass, pass2);
  ASSERT_EQ(user2, std::string("signalwire"));
  return true;
}

TEST(security_config_basic_auth_from_env) {
  clear_security_env();
  setenv("SWML_BASIC_AUTH_USER", "admin", 1);
  setenv("SWML_BASIC_AUTH_PASSWORD", "s3cret", 1);
  SecurityConfig cfg;
  auto [user, pass] = cfg.get_basic_auth();
  ASSERT_EQ(user, std::string("admin"));
  ASSERT_EQ(pass, std::string("s3cret"));
  clear_security_env();
  return true;
}

TEST(security_config_numeric_env) {
  clear_security_env();
  setenv("SWML_MAX_REQUEST_SIZE", "2048", 1);
  setenv("SWML_RATE_LIMIT", "120", 1);
  setenv("SWML_REQUEST_TIMEOUT", "45", 1);
  setenv("SWML_HSTS_MAX_AGE", "600", 1);
  SecurityConfig cfg;
  ASSERT_EQ(cfg.max_request_size(), 2048L);
  ASSERT_EQ(cfg.rate_limit(), 120);
  ASSERT_EQ(cfg.request_timeout(), 45);
  ASSERT_EQ(cfg.hsts_max_age(), 600L);
  clear_security_env();
  return true;
}
