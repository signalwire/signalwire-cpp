// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT

#include "signalwire/core/security_config.hpp"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "signalwire/common.hpp"
#include "signalwire/core/config_loader.hpp"
#include "signalwire/logging.hpp"

namespace signalwire {
namespace core {

namespace {

bool file_exists(const std::string& path) {
  std::ifstream f(path);
  return f.good();
}

std::string to_lower(const std::string& s) {
  std::string out = s;
  for (char& c : out) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return out;
}

std::string trim(const std::string& s) {
  auto start = s.find_first_not_of(" \t\r\n");
  if (start == std::string::npos) {
    return "";
  }
  auto end = s.find_last_not_of(" \t\r\n");
  return s.substr(start, end - start + 1);
}

// Environment variable read that distinguishes "set" from "unset" (Python's
// os.environ.get returns None when unset).
std::optional<std::string> env_opt(const char* key) {
  const char* v = std::getenv(key);
  if (v == nullptr) {
    return std::nullopt;
  }
  return std::string(v);
}

long parse_long(const std::optional<std::string>& v, long fallback) {
  if (!v.has_value() || v->empty()) {
    return fallback;
  }
  try {
    return std::stol(trim(*v));
  } catch (const std::exception&) {
    return fallback;
  }
}

bool truthy(const json& v) {
  if (v.is_boolean()) {
    return v.get<bool>();
  }
  if (v.is_number_integer()) {
    return v.get<long long>() != 0;
  }
  if (v.is_string()) {
    std::string s = to_lower(v.get<std::string>());
    return s == "true" || s == "1" || s == "yes";
  }
  return false;
}

std::string json_to_str(const json& v) {
  if (v.is_string()) {
    return v.get<std::string>();
  }
  return v.dump();
}

}  // namespace

SecurityConfig::SecurityConfig(const std::optional<std::string>& config_file,
                               const std::optional<std::string>& service_name) {
  set_defaults();
  load_from_env();
  load_config_file(config_file, service_name);
}

void SecurityConfig::set_defaults() {
  ssl_enabled_ = false;
  ssl_cert_path_ = std::nullopt;
  ssl_key_path_ = std::nullopt;
  domain_ = std::nullopt;
  ssl_verify_mode_ = "CERT_REQUIRED";
  allowed_hosts_ = parse_list("*");
  cors_origins_ = parse_list("*");
  max_request_size_ = 10L * 1024 * 1024;
  rate_limit_ = 60;
  request_timeout_ = 30;
  use_hsts_ = true;
  hsts_max_age_ = 31536000L;
  basic_auth_user_ = std::nullopt;
  basic_auth_password_ = std::nullopt;
}

void SecurityConfig::load_from_env() {
  std::string enabled = to_lower(get_env(SSL_ENABLED));
  ssl_enabled_ = (enabled == "true" || enabled == "1" || enabled == "yes");
  ssl_cert_path_ = env_opt(SSL_CERT_PATH);
  ssl_key_path_ = env_opt(SSL_KEY_PATH);
  domain_ = env_opt(SSL_DOMAIN);
  ssl_verify_mode_ = get_env(SSL_VERIFY_MODE, "CERT_REQUIRED");

  allowed_hosts_ = parse_list(get_env(ALLOWED_HOSTS, "*"));
  cors_origins_ = parse_list(get_env(CORS_ORIGINS, "*"));
  max_request_size_ = parse_long(env_opt(MAX_REQUEST_SIZE), 10L * 1024 * 1024);
  rate_limit_ = static_cast<int>(parse_long(env_opt(RATE_LIMIT), 60));
  request_timeout_ = static_cast<int>(parse_long(env_opt(REQUEST_TIMEOUT), 30));

  std::string use_hsts_env = to_lower(get_env(USE_HSTS));
  use_hsts_ = use_hsts_env.empty() ? true : (use_hsts_env != "false");
  hsts_max_age_ = parse_long(env_opt(HSTS_MAX_AGE), 31536000L);

  basic_auth_user_ = env_opt(BASIC_AUTH_USER);
  basic_auth_password_ = env_opt(BASIC_AUTH_PASSWORD);
}

void SecurityConfig::load_config_file(const std::optional<std::string>& config_file,
                                      const std::optional<std::string>& service_name) {
  std::optional<std::string> file = config_file;
  if (!file.has_value()) {
    file = ConfigLoader::find_config_file(service_name);
  }
  if (!file.has_value()) {
    return;
  }
  ConfigLoader loader(std::vector<std::string>{*file});
  if (!loader.has_config()) {
    return;
  }
  Logger::instance().info("loading_config_from_file file=" + *file);
  json section = loader.get_section("security");
  if (!section.is_object() || section.empty()) {
    return;
  }
  apply_security_section(section);
}

void SecurityConfig::apply_security_section(const json& section) {
  if (section.contains("ssl_enabled")) {
    ssl_enabled_ = truthy(section["ssl_enabled"]);
  }
  if (section.contains("ssl_cert_path")) {
    ssl_cert_path_ = json_to_str(section["ssl_cert_path"]);
  }
  if (section.contains("ssl_key_path")) {
    ssl_key_path_ = json_to_str(section["ssl_key_path"]);
  }
  if (section.contains("domain")) {
    domain_ = json_to_str(section["domain"]);
  }
  if (section.contains("ssl_verify_mode")) {
    ssl_verify_mode_ = json_to_str(section["ssl_verify_mode"]);
  }
  if (section.contains("allowed_hosts")) {
    allowed_hosts_ = parse_list_json(section["allowed_hosts"]);
  }
  if (section.contains("cors_origins")) {
    cors_origins_ = parse_list_json(section["cors_origins"]);
  }
  if (section.contains("max_request_size")) {
    max_request_size_ = parse_long(json_to_str(section["max_request_size"]), max_request_size_);
  }
  if (section.contains("rate_limit")) {
    rate_limit_ = static_cast<int>(parse_long(json_to_str(section["rate_limit"]), rate_limit_));
  }
  if (section.contains("request_timeout")) {
    request_timeout_ =
        static_cast<int>(parse_long(json_to_str(section["request_timeout"]), request_timeout_));
  }
  if (section.contains("use_hsts")) {
    use_hsts_ = truthy(section["use_hsts"]);
  }
  if (section.contains("hsts_max_age")) {
    hsts_max_age_ = parse_long(json_to_str(section["hsts_max_age"]), hsts_max_age_);
  }
  // Authentication from config: security.auth.basic.{user,password}.
  if (section.contains("auth") && section["auth"].is_object()) {
    const json& auth = section["auth"];
    if (auth.contains("basic") && auth["basic"].is_object()) {
      const json& basic = auth["basic"];
      if (basic.contains("user")) {
        basic_auth_user_ = json_to_str(basic["user"]);
      }
      if (basic.contains("password")) {
        basic_auth_password_ = json_to_str(basic["password"]);
      }
    }
  }
}

std::vector<std::string> SecurityConfig::parse_list(const std::string& value) {
  if (value == "*") {
    return {"*"};
  }
  std::vector<std::string> out;
  std::stringstream ss(value);
  std::string item;
  while (std::getline(ss, item, ',')) {
    std::string trimmed = trim(item);
    if (!trimmed.empty()) {
      out.push_back(trimmed);
    }
  }
  return out;
}

std::vector<std::string> SecurityConfig::parse_list_json(const json& value) {
  if (value.is_array()) {
    std::vector<std::string> out;
    for (const auto& item : value) {
      out.push_back(json_to_str(item));
    }
    return out;
  }
  return parse_list(json_to_str(value));
}

SslValidationResult SecurityConfig::validate_ssl_config() const {
  SslValidationResult result;
  if (!ssl_enabled_) {
    result.valid = true;
    return result;
  }
  if (!ssl_cert_path_.has_value()) {
    result.valid = false;
    result.error = "SSL enabled but SWML_SSL_CERT_PATH not set";
    return result;
  }
  if (!ssl_key_path_.has_value()) {
    result.valid = false;
    result.error = "SSL enabled but SWML_SSL_KEY_PATH not set";
    return result;
  }
  if (!file_exists(*ssl_cert_path_)) {
    result.valid = false;
    result.error = "SSL certificate file not found: " + *ssl_cert_path_;
    return result;
  }
  if (!file_exists(*ssl_key_path_)) {
    result.valid = false;
    result.error = "SSL key file not found: " + *ssl_key_path_;
    return result;
  }
  result.valid = true;
  return result;
}

json SecurityConfig::get_ssl_context_kwargs() const {
  json out = json::object();
  if (!ssl_enabled_) {
    return out;
  }
  SslValidationResult result = validate_ssl_config();
  if (!result.valid) {
    Logger::instance().error("ssl_validation_failed error=" + result.error.value_or(""));
    return out;
  }
  out["ssl_certfile"] = *ssl_cert_path_;
  out["ssl_keyfile"] = *ssl_key_path_;
  return out;
}

std::pair<std::string, std::string> SecurityConfig::get_basic_auth() {
  std::string username = basic_auth_user_.value_or("");
  if (username.empty()) {
    username = "signalwire";
  }
  if (!basic_auth_password_.has_value() || basic_auth_password_->empty()) {
    basic_auth_password_ = generate_random_password(32);
    if (!basic_auth_autogen_warned_) {
      Logger::instance().warn(
          "basic_auth_password_autogenerated username=" + username +
          ": no SWML_BASIC_AUTH_PASSWORD in environment and no password passed; generated a "
          "random password that exists only in this process. External callers will get HTTP 401 "
          "unless they read it from this process's env. Set SWML_BASIC_AUTH_USER / "
          "SWML_BASIC_AUTH_PASSWORD to suppress.");
      basic_auth_autogen_warned_ = true;
    }
  }
  return {username, *basic_auth_password_};
}

json SecurityConfig::get_security_headers(bool is_https) const {
  json headers = json::object();
  headers["X-Content-Type-Options"] = "nosniff";
  headers["X-Frame-Options"] = "DENY";
  headers["X-XSS-Protection"] = "1; mode=block";
  headers["Referrer-Policy"] = "strict-origin-when-cross-origin";
  if (is_https && use_hsts_) {
    headers["Strict-Transport-Security"] =
        "max-age=" + std::to_string(hsts_max_age_) + "; includeSubDomains";
  }
  return headers;
}

bool SecurityConfig::should_allow_host(const std::string& host) const {
  for (const auto& h : allowed_hosts_) {
    if (h == "*") {
      return true;
    }
  }
  for (const auto& h : allowed_hosts_) {
    if (h == host) {
      return true;
    }
  }
  return false;
}

json SecurityConfig::get_cors_config() const {
  json cors = json::object();
  cors["allow_origins"] = cors_origins_;
  cors["allow_credentials"] = true;
  cors["allow_methods"] = json::array({"*"});
  cors["allow_headers"] = json::array({"*"});
  return cors;
}

std::string SecurityConfig::get_url_scheme() const { return ssl_enabled_ ? "https" : "http"; }

void SecurityConfig::log_config(const std::string& service_name) const {
  bool has_basic_auth = basic_auth_user_.has_value() && basic_auth_password_.has_value();
  std::ostringstream oss;
  oss << "security_config_loaded service=" << service_name << " ssl_enabled=" << ssl_enabled_
      << " domain=" << domain_.value_or("(none)")
      << " allowed_hosts=" << json(allowed_hosts_).dump()
      << " cors_origins=" << json(cors_origins_).dump() << " max_request_size=" << max_request_size_
      << " rate_limit=" << rate_limit_ << " use_hsts=" << use_hsts_
      << " has_basic_auth=" << has_basic_auth;
  Logger::instance().info(oss.str());
}

}  // namespace core
}  // namespace signalwire
