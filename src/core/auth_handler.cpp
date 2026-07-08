// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT

#include "signalwire/core/auth_handler.hpp"

#include <cctype>

#include "signalwire/common.hpp"
#include "signalwire/logging.hpp"

namespace signalwire {
namespace core {

namespace {

std::string to_lower(const std::string& s) {
  std::string out = s;
  for (char& c : out) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return out;
}

bool starts_with(const std::string& s, const std::string& prefix) {
  return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

}  // namespace

AuthHandler::AuthHandler(SecurityConfig& security_config) : security_config_(security_config) {
  setup_auth_methods();
}

void AuthHandler::setup_auth_methods() {
  // Basic auth is always available (backward compatibility). get_basic_auth
  // may generate a random password if none was configured.
  auto [username, password] = security_config_.get_basic_auth();
  basic_enabled_ = true;
  basic_username_ = username;
  basic_password_ = password;

  // Bearer / API key: SecurityConfig has no such fields in the reference, so
  // (as in Python's getattr(..., None)) they stay disabled unless a subclass
  // provides them. They remain in the code path so verify_bearer_token /
  // verify_api_key are real and correct when a token/key is set.
  bearer_enabled_ = false;
  api_key_enabled_ = false;
  api_key_header_ = "X-API-Key";
}

bool AuthHandler::verify_basic_auth(const BasicCredentials& credentials) const {
  if (!basic_enabled_) {
    return false;
  }
  bool username_correct = timing_safe_compare(credentials.username, basic_username_);
  bool password_correct = timing_safe_compare(credentials.password, basic_password_);
  return username_correct && password_correct;
}

bool AuthHandler::verify_bearer_token(const BearerCredentials& credentials) const {
  if (!bearer_enabled_) {
    return false;
  }
  return timing_safe_compare(credentials.credentials, bearer_token_);
}

bool AuthHandler::verify_api_key(const std::string& api_key) const {
  if (!api_key_enabled_) {
    return false;
  }
  return timing_safe_compare(api_key, api_key_value_);
}

bool AuthHandler::enabled(const std::string& method) const {
  if (method == "basic") {
    return basic_enabled_;
  }
  if (method == "bearer") {
    return bearer_enabled_;
  }
  if (method == "api_key") {
    return api_key_enabled_;
  }
  return false;
}

std::optional<std::string> AuthHandler::authenticate(const Headers& headers) const {
  if (bearer_ok(headers)) {
    return std::string("bearer");
  }
  if (api_key_ok(headers)) {
    return std::string("api_key");
  }
  if (basic_ok(headers)) {
    return std::string("basic");
  }
  return std::nullopt;
}

bool AuthHandler::bearer_ok(const Headers& headers) const {
  if (!enabled("bearer")) {
    return false;
  }
  std::string auth = header(headers, "Authorization").value_or("");
  if (!starts_with(auth, "Bearer ")) {
    return false;
  }
  return verify_bearer_token(BearerCredentials{auth.substr(7)});
}

bool AuthHandler::api_key_ok(const Headers& headers) const {
  if (!enabled("api_key")) {
    return false;
  }
  auto key = header(headers, api_key_header_);
  return key.has_value() && verify_api_key(*key);
}

bool AuthHandler::basic_ok(const Headers& headers) const {
  if (!enabled("basic")) {
    return false;
  }
  auto creds = parse_basic_auth(header(headers, "Authorization").value_or(""));
  return creds.has_value() && verify_basic_auth(*creds);
}

std::optional<BasicCredentials> AuthHandler::parse_basic_auth(const std::string& header) {
  if (!starts_with(header, "Basic ")) {
    return std::nullopt;
  }
  std::string encoded = header.substr(6);
  // Trim surrounding whitespace.
  auto start = encoded.find_first_not_of(" \t\r\n");
  auto end = encoded.find_last_not_of(" \t\r\n");
  if (start == std::string::npos) {
    return std::nullopt;
  }
  encoded = encoded.substr(start, end - start + 1);
  std::string pair = base64_decode(encoded);
  auto idx = pair.find(':');
  std::string user = idx != std::string::npos ? pair.substr(0, idx) : pair;
  std::string pass = idx != std::string::npos ? pair.substr(idx + 1) : "";
  return BasicCredentials{user, pass};
}

std::optional<std::string> AuthHandler::header(const Headers& headers, const std::string& name) {
  auto it = headers.find(name);
  if (it != headers.end()) {
    return it->second;
  }
  std::string lname = to_lower(name);
  for (const auto& kv : headers) {
    if (to_lower(kv.first) == lname) {
      return kv.second;
    }
  }
  return std::nullopt;
}

AuthResponse AuthHandler::unauthorized_response() {
  AuthResponse resp;
  resp.status = 401;
  resp.headers = json::object();
  resp.headers["content-type"] = "text/plain";
  resp.headers["www-authenticate"] = "Basic realm=\"SignalWire Service\"";
  resp.body = "Authentication required";
  return resp;
}

void AuthHandler::log_auth_failure(const Headers& headers) const {
  Logger::instance().warn("auth_failed ip=" + header(headers, "X-Forwarded-For").value_or("") +
                          " method=" + header(headers, "X-Request-Method").value_or("") +
                          " path=" + header(headers, "X-Request-Path").value_or(""));
}

AuthHandler::FastapiDependency AuthHandler::get_fastapi_dependency(bool optional) const {
  const AuthHandler* self = this;
  return [self, optional](const Headers& headers) -> AuthResult {
    auto method = self->authenticate(headers);
    bool authenticated = method.has_value();
    if (!authenticated && !optional) {
      throw AuthException(unauthorized_response());
    }
    AuthResult result;
    result.authenticated = authenticated;
    result.method = method;
    return result;
  };
}

AuthHandler::RequestHandler AuthHandler::flask_decorator(RequestHandler app) const {
  const AuthHandler* self = this;
  return [self, app = std::move(app)](const Headers& headers) -> AuthResponse {
    if (self->authenticate(headers).has_value()) {
      return app(headers);
    }
    self->log_auth_failure(headers);
    return unauthorized_response();
  };
}

json AuthHandler::get_auth_info() const {
  json info = json::object();
  if (basic_enabled_) {
    json basic = json::object();
    basic["enabled"] = true;
    basic["username"] = basic_username_;
    info["basic"] = basic;
  }
  if (bearer_enabled_) {
    json bearer = json::object();
    bearer["enabled"] = true;
    bearer["hint"] = "Use Authorization: Bearer <token>";
    info["bearer"] = bearer;
  }
  if (api_key_enabled_) {
    json api_key = json::object();
    api_key["enabled"] = true;
    api_key["header"] = api_key_header_;
    api_key["hint"] = "Use " + api_key_header_ + ": <key>";
    info["api_key"] = api_key;
  }
  return info;
}

}  // namespace core
}  // namespace signalwire
