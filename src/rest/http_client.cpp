// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#include "signalwire/rest/http_client.hpp"

#include <cctype>
#include <chrono>
#include <cmath>
#include <set>
#include <thread>

#include "httplib.h"
#include "signalwire/common.hpp"

namespace signalwire {
namespace rest {

// §6.6 error-observability: the ctor + helpers live here (not inline in the
// header) so the error carries headers/request_id while the header stays a
// plain declaration surface.
SignalWireRestError::SignalWireRestError(int status, const std::string& message,
                                         const std::string& body, const std::string& url,
                                         const std::string& method,
                                         const std::map<std::string, std::string>& headers)
    : std::runtime_error(with_request_id(message, extract_request_id(headers))),
      status_(status),
      body_(body),
      url_(url),
      method_(method),
      headers_(headers),
      request_id_(extract_request_id(headers)) {}

std::string SignalWireRestError::extract_request_id(
    const std::map<std::string, std::string>& headers) {
  // Header names SignalWire (and common proxies) use for the platform request
  // id, in preference order — matched case-insensitively. Mirrors the Python
  // reference's _REQUEST_ID_HEADERS.
  static const char* kNames[] = {"x-request-id", "x-signalwire-request-id", "request-id",
                                 "x-amzn-requestid"};
  std::map<std::string, std::string> lowered;
  for (const auto& [k, v] : headers) {
    std::string lk = k;
    for (auto& c : lk) {
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    lowered.emplace(lk, v);
  }
  for (const char* name : kNames) {
    auto it = lowered.find(name);
    if (it != lowered.end()) {
      return it->second;
    }
  }
  return "";
}

std::string SignalWireRestError::with_request_id(const std::string& message,
                                                 const std::string& request_id) {
  if (request_id.empty()) {
    return message;
  }
  // Mirrors python: message += f" (request-id: {id})" when an id is present.
  return message + " (request-id: " + request_id + ")";
}

namespace {

// Built-in defaults (the contract floor), mirroring Python's
// signalwire.rest._request_options built-ins. Resolved per-request:
// per-request over client-default over built-in.
struct EffectiveOptions {
  double timeout;
  int retries;
  std::set<int> retry_on_status;
  double retry_backoff;
  std::atomic<bool>* abort_signal;
};

constexpr double kBuiltinTimeout = 30.0;
constexpr int kBuiltinRetries = 0;
constexpr double kBuiltinRetryBackoff = 0.5;

std::set<int> builtin_retry_on_status() { return {429, 500, 502, 503, 504}; }

// Resolve the effective options: merge per-request over the client default,
// then fill any unset field from the built-in floor.
EffectiveOptions resolve(const RequestOptions& client_default, const RequestOptions& per_request) {
  RequestOptions merged = client_default.merge(per_request);
  EffectiveOptions eff;
  eff.timeout = merged.timeout.value_or(kBuiltinTimeout);
  eff.retries = merged.retries.value_or(kBuiltinRetries);
  eff.retry_on_status = merged.retry_on_status.value_or(builtin_retry_on_status());
  eff.retry_backoff = merged.retry_backoff.value_or(kBuiltinRetryBackoff);
  eff.abort_signal = merged.abort_signal;
  return eff;
}

// Idempotency-aware retry policy, mirroring Python's status_is_retryable:
// idempotent methods retry the full retry_on_status set; non-idempotent
// methods (POST/PATCH) retry ONLY throttle statuses (429/503).
bool status_is_retryable(const std::string& method, int status, const EffectiveOptions& eff) {
  if (eff.retry_on_status.count(status) == 0) {
    return false;
  }
  static const std::set<std::string> kIdempotent = {"GET", "PUT", "DELETE", "HEAD", "OPTIONS"};
  if (kIdempotent.count(method) != 0) {
    return true;
  }
  return status == 429 || status == 503;
}

// Parse a Retry-After header (delta-seconds form) into a wait in seconds.
// Returns -1 when absent or an HTTP-date form (fall back to computed backoff).
double retry_after_seconds(const httplib::Response& res) {
  std::string val = res.get_header_value("Retry-After");
  if (val.empty()) {
    return -1;
  }
  try {
    size_t consumed = 0;
    double secs = std::stod(val, &consumed);
    // Reject a value with trailing non-numeric text (an HTTP-date form).
    if (consumed != val.size()) {
      return -1;
    }
    return secs;
  } catch (...) {
    return -1;
  }
}

void sleep_backoff(double seconds) {
  if (seconds > 0) {
    std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
  }
}

}  // namespace

HttpClient::HttpClient(const std::string& base_url, const std::string& username,
                       const std::string& password, const RequestOptions& request_options)
    : base_url_(base_url), request_options_(request_options) {
  auth_header_ = "Basic " + signalwire::base64_encode(username + ":" + password);
  headers_["Content-Type"] = "application/json";
  headers_["Accept"] = "application/json";
}

void HttpClient::set_header(const std::string& key, const std::string& value) {
  headers_[key] = value;
}

// Legacy setter — writes into the client-default RequestOptions so it still
// influences the effective per-attempt timeout resolved at request time.
void HttpClient::set_timeout(int seconds) {
  request_options_.timeout = static_cast<double>(seconds);
}

void HttpClient::set_ca_cert_path(const std::string& path) { ca_cert_path_ = path; }

// Apply per-request transport config: timeouts plus, for https:// targets, the
// CA bundle to trust. With CPPHTTPLIB_OPENSSL_SUPPORT a Client on an https://
// scheme is backed by an SSLClient that verifies the server cert against the
// system store by default. To trust a private/self-signed CA we point it at an
// explicit bundle: set_ca_cert_path() if the caller supplied one, else the
// SSL_CERT_FILE env var (the cross-port idiom). Verification stays ENABLED.
//
// The timeout is converted to httplib's std::chrono overload (microseconds) —
// NOT a raw (sec, nsec) pair — so a fractional-second timeout (e.g. 0.1s) is
// honored exactly instead of being truncated or misread.
void HttpClient::configure_client(httplib::Client& cli, double timeout_seconds) const {
  auto micros = std::chrono::microseconds(static_cast<long long>(timeout_seconds * 1e6));
  cli.set_connection_timeout(micros);
  cli.set_read_timeout(micros);
  cli.set_write_timeout(micros);

  std::string ca = ca_cert_path_;
  if (ca.empty()) {
    if (const char* env = std::getenv("SSL_CERT_FILE")) {
      if (env && *env) {
        ca = env;
      }
    }
  }
  if (!ca.empty()) {
    cli.set_ca_cert_path(ca);
    cli.enable_server_certificate_verification(true);
  }
}

std::string HttpClient::build_query_string(const std::map<std::string, std::string>& params) const {
  if (params.empty()) {
    return "";
  }
  std::string qs = "?";
  bool first = true;
  for (const auto& [k, v] : params) {
    if (!first) {
      qs += "&";
    }
    qs += signalwire::url_encode(k) + "=" + signalwire::url_encode(v);
    first = false;
  }
  return qs;
}

json HttpClient::handle_response(int status, const std::string& body, const std::string& url,
                                 const std::string& method,
                                 const std::map<std::string, std::string>& headers) const {
  if (status == 204 || body.empty()) {
    return json::object();
  }

  if (status >= 200 && status < 300) {
    try {
      return json::parse(body);
    } catch (...) {
      return json::object({{"raw", body}});
    }
  }

  // Mirror Python's ``f"{method} {url} returned {status}: {body}"`` message so
  // the thrown error carries the full (status, body, url, method) envelope.
  std::string msg = method + " " + url + " returned " + std::to_string(status);
  try {
    json err = json::parse(body);
    if (err.contains("message")) {
      msg += ": " + err["message"].get<std::string>();
    } else if (err.contains("error")) {
      msg += ": " + err["error"].get<std::string>();
    } else {
      msg += ": " + body;
    }
  } catch (...) {
    if (!body.empty()) {
      msg += ": " + body;
    }
  }

  throw SignalWireRestError(status, msg, body, url, method, headers);
}

// Helper to extract scheme and host from base_url
static std::pair<std::string, std::string> parse_url(const std::string& base_url) {
  std::string scheme = "http";
  std::string host = base_url;
  auto pos = host.find("://");
  if (pos != std::string::npos) {
    scheme = host.substr(0, pos);
    host = host.substr(pos + 3);
  }
  if (!host.empty() && host.back() == '/') {
    host.pop_back();
  }
  return {scheme, host};
}

static httplib::Headers make_headers(const std::string& auth,
                                     const std::map<std::string, std::string>& extra) {
  httplib::Headers hdrs;
  hdrs.emplace("Authorization", auth);
  for (const auto& [k, v] : extra) {
    hdrs.emplace(k, v);
  }
  return hdrs;
}

// The single funnel: resolve effective options, then run the retry/timeout/
// abort loop, dispatching by method. Mirrors Python's _base.py retry loop —
// total attempts = retries + 1; before each attempt check the abort signal;
// on a transport failure retry (backoff) while attempts remain else raise the
// transport error; on a non-2xx retry (Retry-After or backoff) only when the
// method+status is retryable and attempts remain, else surface the typed error.
json HttpClient::request(const std::string& method, const std::string& path, const json* body,
                         const std::map<std::string, std::string>* params,
                         const RequestOptions& per_request) const {
  EffectiveOptions eff = resolve(request_options_, per_request);

  auto [scheme, host] = parse_url(base_url_);

  // GET encodes params into the path and reports the full path in errors;
  // the body verbs report the bare path. Preserve that so error urls are
  // unchanged from the pre-envelope client.
  std::string full_path = path;
  if (method == "GET" && params != nullptr) {
    full_path = path + build_query_string(*params);
  }
  const std::string path_for_error = (method == "GET") ? full_path : path;

  std::string body_str;
  const bool has_body = (body != nullptr);
  if (has_body) {
    body_str = body->dump();
  }

  // scheme/host are loop-invariant — build the client URL once, reuse it to
  // construct a fresh Client per attempt.
  std::string client_url = scheme;
  client_url += "://";
  client_url += host;

  int attempt = 0;
  while (true) {
    ++attempt;

    // Cooperative cancellation: checked BEFORE each attempt (a synchronous
    // httplib read cannot be interrupted mid-flight without a thread).
    if (eff.abort_signal != nullptr && eff.abort_signal->load()) {
      throw SignalWireRestTransportError("request cancelled by abort_signal", path_for_error,
                                         method);
    }

    httplib::Client cli(client_url);
    configure_client(cli, eff.timeout);
    auto hdrs = make_headers(auth_header_, headers_);

    httplib::Result res;
    if (method == "GET") {
      res = cli.Get(full_path, hdrs);
    } else if (method == "POST") {
      res = cli.Post(path, hdrs, body_str, "application/json");
    } else if (method == "PUT") {
      res = cli.Put(path, hdrs, body_str, "application/json");
    } else if (method == "PATCH") {
      res = cli.Patch(path, hdrs, body_str, "application/json");
    } else {  // DELETE
      res = cli.Delete(path, hdrs);
    }

    if (!res) {
      // Transport failure — retry while attempts remain, else raise the typed
      // transport error. Preserve the pre-envelope messages so any test that
      // asserts them still passes (GET -> "...to <host>", others -> bare).
      if (attempt <= eff.retries) {
        sleep_backoff(eff.retry_backoff * std::pow(2.0, attempt - 1));
        continue;
      }
      if (method == "GET") {
        throw SignalWireRestTransportError("Connection failed to " + host, path_for_error, method);
      }
      throw SignalWireRestTransportError("Connection failed", path_for_error, method);
    }

    if (res->status < 200 || res->status >= 300) {
      if (attempt <= eff.retries && status_is_retryable(method, res->status, eff)) {
        double delay = retry_after_seconds(*res);
        if (delay < 0) {
          delay = eff.retry_backoff * std::pow(2.0, attempt - 1);
        }
        sleep_backoff(delay);
        continue;
      }
      // Not retryable / retries exhausted — fall through to handle_response,
      // which throws the typed SignalWireRestError.
    }

    // §6.6 error-observability: carry the response headers into the typed
    // error so a non-2xx surfaces the platform request id to the caller.
    std::map<std::string, std::string> response_headers(res->headers.begin(), res->headers.end());
    return handle_response(res->status, res->body, path_for_error, method, response_headers);
  }
}

json HttpClient::get(const std::string& path, const std::map<std::string, std::string>& params,
                     const RequestOptions& request_options) const {
  return request("GET", path, nullptr, &params, request_options);
}

json HttpClient::post(const std::string& path, const json& body,
                      const RequestOptions& request_options) const {
  return request("POST", path, &body, nullptr, request_options);
}

json HttpClient::put(const std::string& path, const json& body,
                     const RequestOptions& request_options) const {
  return request("PUT", path, &body, nullptr, request_options);
}

json HttpClient::patch(const std::string& path, const json& body,
                       const RequestOptions& request_options) const {
  return request("PATCH", path, &body, nullptr, request_options);
}

json HttpClient::del(const std::string& path, const RequestOptions& request_options) const {
  return request("DELETE", path, nullptr, nullptr, request_options);
}

// ============================================================================
// PaginatedIterator
//
// Mirrors signalwire-python's signalwire.rest._pagination.PaginatedIterator.
// fetch_next() walks links.next when present; ``data_key`` selects which
// array of items to consume from the response.
// ============================================================================

namespace {

// Naive query-string parser: splits "k=v&k2=v2" into a map of last-value-wins.
// Mirrors urllib.parse.parse_qs(...) flattened to single values, which is the
// behaviour Python's PaginatedIterator gets when ``len(v) == 1``.
std::map<std::string, std::string> parse_query_string(const std::string& url) {
  std::map<std::string, std::string> out;
  auto qpos = url.find('?');
  if (qpos == std::string::npos) {
    return out;
  }
  std::string qs = url.substr(qpos + 1);
  // Strip a trailing fragment if any.
  auto frag = qs.find('#');
  if (frag != std::string::npos) {
    qs = qs.substr(0, frag);
  }

  auto decode_one = [](std::string s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
      if (s[i] == '+') {
        out.push_back(' ');
      } else if (s[i] == '%' && i + 2 < s.size()) {
        auto hex = s.substr(i + 1, 2);
        try {
          out.push_back(static_cast<char>(std::stoi(hex, nullptr, 16)));
          i += 2;
        } catch (...) {
          out.push_back(s[i]);
        }
      } else {
        out.push_back(s[i]);
      }
    }
    return out;
  };

  size_t start = 0;
  while (start <= qs.size()) {
    size_t end = qs.find('&', start);
    if (end == std::string::npos) {
      end = qs.size();
    }
    std::string pair = qs.substr(start, end - start);
    if (!pair.empty()) {
      auto eq = pair.find('=');
      if (eq == std::string::npos) {
        out[decode_one(pair)] = std::string();
      } else {
        out[decode_one(pair.substr(0, eq))] = decode_one(pair.substr(eq + 1));
      }
    }
    if (end == qs.size()) {
      break;
    }
    start = end + 1;
  }
  return out;
}

}  // namespace

PaginatedIterator::PaginatedIterator(const HttpClient& http, const std::string& path,
                                     const std::map<std::string, std::string>& params,
                                     const std::string& data_key)
    : http_(http), path_(path), params_(params), data_key_(data_key) {}

bool PaginatedIterator::has_next() {
  while (index_ >= items_.size()) {
    if (done_) {
      return false;
    }
    fetch_next();
  }
  return true;
}

json PaginatedIterator::next() {
  if (!has_next()) {
    throw std::out_of_range("PaginatedIterator: exhausted");
  }
  return items_.at(index_++);
}

void PaginatedIterator::fetch_next() {
  auto resp = http_.get(path_, params_);
  if (resp.is_object() && resp.contains(data_key_) && resp[data_key_].is_array()) {
    for (const auto& it : resp[data_key_]) {
      items_.push_back(it);
    }
  }

  std::string next_url;
  bool had_data = !resp.is_object() ? false
                                    : (resp.contains(data_key_) && resp[data_key_].is_array() &&
                                       !resp[data_key_].empty());

  if (resp.is_object() && resp.contains("links") && resp["links"].is_object()) {
    const auto& links = resp["links"];
    if (links.contains("next") && links["next"].is_string()) {
      next_url = links["next"].get<std::string>();
    }
  }

  if (!next_url.empty() && had_data) {
    params_ = parse_query_string(next_url);
  } else {
    done_ = true;
  }
}

}  // namespace rest
}  // namespace signalwire
