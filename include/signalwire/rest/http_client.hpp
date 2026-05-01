// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#pragma once

#include <string>
#include <map>
#include <stdexcept>
#include <vector>
#include <nlohmann/json.hpp>

namespace signalwire {
namespace rest {

using json = nlohmann::json;

/// Error thrown on non-2xx REST API responses
class SignalWireRestError : public std::runtime_error {
public:
    SignalWireRestError(int status, const std::string& message, const std::string& body = "")
        : std::runtime_error(message), status_(status), body_(body) {}
    int status() const { return status_; }
    const std::string& body() const { return body_; }
private:
    int status_;
    std::string body_;
};

/// HTTP client with Basic Auth support using cpp-httplib
class HttpClient {
public:
    HttpClient(const std::string& base_url,
               const std::string& username,
               const std::string& password);

    /// GET request
    json get(const std::string& path,
             const std::map<std::string, std::string>& params = {}) const;

    /// POST request
    json post(const std::string& path,
              const json& body = json::object()) const;

    /// PUT request
    json put(const std::string& path,
             const json& body = json::object()) const;

    /// PATCH request
    json patch(const std::string& path,
               const json& body = json::object()) const;

    /// DELETE request
    json del(const std::string& path) const;

    /// Set additional default headers
    void set_header(const std::string& key, const std::string& value);

    /// Set request timeout in seconds
    void set_timeout(int seconds);

    const std::string& base_url() const { return base_url_; }

private:
    json handle_response(int status, const std::string& body) const;
    std::string build_query_string(const std::map<std::string, std::string>& params) const;

    std::string base_url_;
    std::string auth_header_;
    std::map<std::string, std::string> headers_;
    int timeout_ = 30;
};

/// Generic CRUD resource for REST API namespaces
class CrudResource {
public:
    CrudResource(const HttpClient& client, const std::string& base_path);

    json list(const std::map<std::string, std::string>& params = {}) const;
    json create(const json& data) const;
    json get(const std::string& id) const;
    json update(const std::string& id, const json& data) const;
    json del(const std::string& id) const;

protected:
    const HttpClient& client_;
    std::string base_path_;
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
    PaginatedIterator(const HttpClient& http,
                      const std::string& path,
                      const std::map<std::string, std::string>& params = {},
                      const std::string& data_key = "data");

    /// Returns true if another item can be fetched. Performs HTTP if
    /// the in-memory buffer is exhausted but more pages remain.
    bool has_next();

    /// Returns the next item; throws std::out_of_range when the iterator
    /// is exhausted (mirrors Python's StopIteration).
    json next();

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

} // namespace rest
} // namespace signalwire
