// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#pragma once

#include <map>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <vector>

namespace httplib {
class Client;
}

namespace signalwire {
namespace rest {

using json = nlohmann::json;

/// Error thrown on non-2xx REST API responses.
///
/// Carries the full request/response envelope — HTTP ``status`` code, response
/// ``body``, the request ``url`` and ``method`` — mirroring Python's
/// ``SignalWireRestError(status_code, body, url, method)``. Every field is
/// exposed so a caller catching the error can inspect exactly which request
/// failed and how.
class SignalWireRestError : public std::runtime_error {
 public:
  SignalWireRestError(int status, const std::string& message, const std::string& body = "",
                      const std::string& url = "", const std::string& method = "GET")
      : std::runtime_error(message), status_(status), body_(body), url_(url), method_(method) {}
  int status() const { return status_; }
  const std::string& body() const { return body_; }
  const std::string& url() const { return url_; }
  const std::string& method() const { return method_; }

 private:
  int status_;
  std::string body_;
  std::string url_;
  std::string method_;
};

/// Error thrown when a REST request never reached a response — a
/// transport-level failure (connection refused, DNS failure, connection
/// reset, TLS error), as opposed to a well-formed non-2xx HTTP response.
///
/// A member of the ``SignalWireRestError`` family: ``status()`` is ``0``
/// (the sentinel this port uses for "no HTTP status" — there is no response
/// to carry one), and the underlying transport-library error text is
/// preserved as the exception message. Because it extends
/// ``SignalWireRestError``, a caller catching that one type handles both an
/// HTTP-error response and a transport failure with a single ``catch``,
/// instead of a bare cpp-httplib/curl error leaking through. Mirrors the
/// Python reference's ``SignalWireRestTransportError(SignalWireRestError)``
/// (plan 1.3b).
class SignalWireRestTransportError : public SignalWireRestError {
 public:
  SignalWireRestTransportError(const std::string& message, const std::string& url = "",
                               const std::string& method = "GET")
      : SignalWireRestError(0, message, "", url, method) {}
};

/// HTTP client with Basic Auth support using cpp-httplib
class HttpClient {
 public:
  HttpClient(const std::string& base_url, const std::string& username, const std::string& password);

  // [[nodiscard]] on every verb: the returned JSON IS the API response.
  // Dropping it discards the call's result (and the only place errors that
  // aren't thrown would surface), which is always a bug.

  /// GET request
  [[nodiscard]] json get(const std::string& path,
                         const std::map<std::string, std::string>& params = {}) const;

  /// POST request
  [[nodiscard]] json post(const std::string& path, const json& body = json::object()) const;

  /// PUT request
  [[nodiscard]] json put(const std::string& path, const json& body = json::object()) const;

  /// PATCH request
  [[nodiscard]] json patch(const std::string& path, const json& body = json::object()) const;

  /// DELETE request
  [[nodiscard]] json del(const std::string& path) const;

  /// Set additional default headers
  void set_header(const std::string& key, const std::string& value);

  /// Set request timeout in seconds
  void set_timeout(int seconds);

  /// Trust a private/self-signed CA bundle (PEM) for https:// requests.
  /// Sets the underlying SSLClient's CA path and keeps server-certificate
  /// verification ON. Production (public CAs) needs no call — the system
  /// trust store is used, and SSL_CERT_FILE is also honored automatically.
  /// C++-only ergonomic hook (Python's requests-based client trusts a custom
  /// CA via the SSL_CERT_FILE / REQUESTS_CA_BUNDLE env vars instead).
  void set_ca_cert_path(const std::string& path);

  const std::string& base_url() const { return base_url_; }

 private:
  json handle_response(int status, const std::string& body, const std::string& url,
                       const std::string& method) const;
  std::string build_query_string(const std::map<std::string, std::string>& params) const;
  void configure_client(httplib::Client& cli) const;

  std::string base_url_;
  std::string auth_header_;
  std::map<std::string, std::string> headers_;
  int timeout_ = 30;
  std::string ca_cert_path_;
};

/// Iterates items across paginated API responses.
///
/// Mirrors signalwire-python's ``signalwire.rest._pagination.PaginatedIterator``:
/// fetches the configured path with the configured params, walks the
/// ``data_key`` array, then follows ``links.next`` (parsing its query string
/// for the next page's params) until the response carries no ``links.next``.
///
/// Iteration is lazy -- the constructor records inputs but performs no
/// HTTP. The first ``has_next()`` / ``next()`` call performs the first
/// fetch. Cursor query params are extracted by parsing ``links.next`` like
/// Python's ``urllib.parse.urlparse + parse_qs``.
class PaginatedIterator {
 public:
  PaginatedIterator(const HttpClient& http, const std::string& path,
                    const std::map<std::string, std::string>& params = {},
                    const std::string& data_key = "data");

  /// Returns true if another item can be fetched. Performs HTTP if
  /// the in-memory buffer is exhausted but more pages remain.
  /// [[nodiscard]]: this is the loop condition — discarding it loses the
  /// "more items?" answer (and the side-effecting page fetch is not why
  /// you'd call it).
  [[nodiscard]] bool has_next();

  /// Returns the next item; throws std::out_of_range when the iterator
  /// is exhausted (mirrors Python's StopIteration).
  /// [[nodiscard]]: dropping the returned item silently consumes it.
  [[nodiscard]] json next();

  // -- Read-only state accessors used by tests ------------------------
  const HttpClient& http() const { return http_; }
  const std::string& path() const { return path_; }
  const std::map<std::string, std::string>& params() const { return params_; }
  const std::string& data_key() const { return data_key_; }
  size_t index() const { return index_; }
  const std::vector<json>& items() const { return items_; }
  bool done() const { return done_; }

 private:
  void fetch_next();

  const HttpClient& http_;
  std::string path_;
  std::map<std::string, std::string> params_;
  std::string data_key_;
  std::vector<json> items_;
  size_t index_ = 0;
  bool done_ = false;
};

}  // namespace rest
}  // namespace signalwire
