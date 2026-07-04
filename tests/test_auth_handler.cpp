// AuthHandler tests (signalwire::core::AuthHandler)

#include <cstdlib>

#include "signalwire/common.hpp"
#include "signalwire/core/auth_handler.hpp"
#include "signalwire/core/security_config.hpp"

using signalwire::core::AuthException;
using signalwire::core::AuthHandler;
using signalwire::core::BasicCredentials;
using signalwire::core::BearerCredentials;
using signalwire::core::Headers;
using signalwire::core::SecurityConfig;

namespace {

// Build a SecurityConfig with deterministic basic-auth credentials via env.
SecurityConfig make_config(const char* user, const char* pass) {
  setenv("SWML_BASIC_AUTH_USER", user, 1);
  setenv("SWML_BASIC_AUTH_PASSWORD", pass, 1);
  SecurityConfig cfg;
  unsetenv("SWML_BASIC_AUTH_USER");
  unsetenv("SWML_BASIC_AUTH_PASSWORD");
  return cfg;
}

// Base64 of "user:pass" for a Basic header, using the SDK's own encoder.
std::string basic_header(const std::string& user, const std::string& pass) {
  return "Basic " + signalwire::base64_encode(user + ":" + pass);
}

}  // namespace

TEST(auth_handler_verify_basic_auth) {
  SecurityConfig cfg = make_config("admin", "s3cret");
  AuthHandler handler(cfg);
  ASSERT_TRUE(handler.verify_basic_auth(BasicCredentials{"admin", "s3cret"}));
  ASSERT_FALSE(handler.verify_basic_auth(BasicCredentials{"admin", "wrong"}));
  ASSERT_FALSE(handler.verify_basic_auth(BasicCredentials{"wronguser", "s3cret"}));
  ASSERT_FALSE(handler.verify_basic_auth(BasicCredentials{"", ""}));
  return true;
}

TEST(auth_handler_verify_bearer_disabled_by_default) {
  // SecurityConfig has no bearer token, so bearer auth is disabled -> always
  // false (parity with Python getattr(..., None)).
  SecurityConfig cfg = make_config("admin", "s3cret");
  AuthHandler handler(cfg);
  ASSERT_FALSE(handler.verify_bearer_token(BearerCredentials{"anything"}));
  return true;
}

TEST(auth_handler_verify_api_key_disabled_by_default) {
  SecurityConfig cfg = make_config("admin", "s3cret");
  AuthHandler handler(cfg);
  ASSERT_FALSE(handler.verify_api_key("anything"));
  return true;
}

TEST(auth_handler_get_auth_info) {
  SecurityConfig cfg = make_config("admin", "s3cret");
  AuthHandler handler(cfg);
  auto info = handler.get_auth_info();
  ASSERT_TRUE(info.contains("basic"));
  ASSERT_EQ(info["basic"]["enabled"], true);
  ASSERT_EQ(info["basic"]["username"], std::string("admin"));
  // No secrets leaked: password is never present in the info map.
  ASSERT_FALSE(info["basic"].contains("password"));
  // Bearer/api_key disabled -> absent.
  ASSERT_FALSE(info.contains("bearer"));
  ASSERT_FALSE(info.contains("api_key"));
  return true;
}

TEST(auth_handler_fastapi_dependency_authenticated) {
  SecurityConfig cfg = make_config("admin", "s3cret");
  AuthHandler handler(cfg);
  auto dep = handler.get_fastapi_dependency(false);

  Headers good{{"Authorization", basic_header("admin", "s3cret")}};
  auto result = dep(good);
  ASSERT_TRUE(result.authenticated);
  ASSERT_TRUE(result.method.has_value());
  ASSERT_EQ(*result.method, std::string("basic"));
  return true;
}

TEST(auth_handler_fastapi_dependency_required_throws) {
  SecurityConfig cfg = make_config("admin", "s3cret");
  AuthHandler handler(cfg);
  auto dep = handler.get_fastapi_dependency(false);

  Headers bad{{"Authorization", basic_header("admin", "wrong")}};
  bool threw = false;
  try {
    dep(bad);
  } catch (const AuthException& e) {
    threw = true;
    ASSERT_EQ(e.response().status, 401);
  }
  ASSERT_TRUE(threw);
  return true;
}

TEST(auth_handler_fastapi_dependency_optional_no_throw) {
  SecurityConfig cfg = make_config("admin", "s3cret");
  AuthHandler handler(cfg);
  auto dep = handler.get_fastapi_dependency(true);

  Headers none{};
  auto result = dep(none);
  ASSERT_FALSE(result.authenticated);
  ASSERT_FALSE(result.method.has_value());
  return true;
}

TEST(auth_handler_flask_decorator_passes_through) {
  SecurityConfig cfg = make_config("admin", "s3cret");
  AuthHandler handler(cfg);

  bool inner_called = false;
  AuthHandler::RequestHandler app = [&inner_called](const Headers&) {
    inner_called = true;
    signalwire::core::AuthResponse ok;
    ok.status = 200;
    ok.body = "OK";
    return ok;
  };
  auto wrapped = handler.flask_decorator(app);

  Headers good{{"Authorization", basic_header("admin", "s3cret")}};
  auto resp = wrapped(good);
  ASSERT_TRUE(inner_called);
  ASSERT_EQ(resp.status, 200);
  ASSERT_EQ(resp.body, std::string("OK"));
  return true;
}

TEST(auth_handler_flask_decorator_blocks_unauthenticated) {
  SecurityConfig cfg = make_config("admin", "s3cret");
  AuthHandler handler(cfg);

  bool inner_called = false;
  AuthHandler::RequestHandler app = [&inner_called](const Headers&) {
    inner_called = true;
    signalwire::core::AuthResponse ok;
    ok.status = 200;
    return ok;
  };
  auto wrapped = handler.flask_decorator(app);

  Headers bad{{"Authorization", basic_header("admin", "nope")}};
  auto resp = wrapped(bad);
  ASSERT_FALSE(inner_called);
  ASSERT_EQ(resp.status, 401);
  ASSERT_TRUE(resp.headers.contains("www-authenticate"));
  return true;
}

TEST(auth_handler_header_lookup_case_insensitive) {
  SecurityConfig cfg = make_config("admin", "s3cret");
  AuthHandler handler(cfg);
  auto dep = handler.get_fastapi_dependency(true);
  // Lowercase header name still resolves.
  Headers good{{"authorization", basic_header("admin", "s3cret")}};
  auto result = dep(good);
  ASSERT_TRUE(result.authenticated);
  return true;
}
