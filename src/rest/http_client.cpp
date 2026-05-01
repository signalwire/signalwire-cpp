// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#include "signalwire/rest/http_client.hpp"
#include "signalwire/common.hpp"
#include "httplib.h"

namespace signalwire {
namespace rest {

HttpClient::HttpClient(const std::string& base_url,
                       const std::string& username,
                       const std::string& password)
    : base_url_(base_url) {
    auth_header_ = "Basic " + signalwire::base64_encode(username + ":" + password);
    headers_["Content-Type"] = "application/json";
    headers_["Accept"] = "application/json";
}

void HttpClient::set_header(const std::string& key, const std::string& value) {
    headers_[key] = value;
}

void HttpClient::set_timeout(int seconds) {
    timeout_ = seconds;
}

std::string HttpClient::build_query_string(const std::map<std::string, std::string>& params) const {
    if (params.empty()) return "";
    std::string qs = "?";
    bool first = true;
    for (const auto& [k, v] : params) {
        if (!first) qs += "&";
        qs += signalwire::url_encode(k) + "=" + signalwire::url_encode(v);
        first = false;
    }
    return qs;
}

json HttpClient::handle_response(int status, const std::string& body) const {
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

    std::string msg = "HTTP " + std::to_string(status);
    try {
        json err = json::parse(body);
        if (err.contains("message")) msg += ": " + err["message"].get<std::string>();
        else if (err.contains("error")) msg += ": " + err["error"].get<std::string>();
    } catch (...) {
        if (!body.empty()) msg += ": " + body;
    }

    throw SignalWireRestError(status, msg, body);
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
    if (!host.empty() && host.back() == '/') host.pop_back();
    return {scheme, host};
}

static httplib::Headers make_headers(const std::string& auth,
                                      const std::map<std::string, std::string>& extra) {
    httplib::Headers hdrs;
    hdrs.emplace("Authorization", auth);
    for (const auto& [k, v] : extra) hdrs.emplace(k, v);
    return hdrs;
}

json HttpClient::get(const std::string& path,
                     const std::map<std::string, std::string>& params) const {
    auto [scheme, host] = parse_url(base_url_);
    std::string full_path = path + build_query_string(params);
    auto hdrs = make_headers(auth_header_, headers_);

    httplib::Client cli(scheme + "://" + host);
    cli.set_connection_timeout(timeout_, 0);
    cli.set_read_timeout(timeout_, 0);

    auto res = cli.Get(full_path, hdrs);
    if (!res) throw SignalWireRestError(0, "Connection failed to " + host);
    return handle_response(res->status, res->body);
}

json HttpClient::post(const std::string& path, const json& body) const {
    auto [scheme, host] = parse_url(base_url_);
    auto hdrs = make_headers(auth_header_, headers_);
    std::string body_str = body.dump();

    httplib::Client cli(scheme + "://" + host);
    cli.set_connection_timeout(timeout_, 0);
    cli.set_read_timeout(timeout_, 0);

    auto res = cli.Post(path, hdrs, body_str, "application/json");
    if (!res) throw SignalWireRestError(0, "Connection failed");
    return handle_response(res->status, res->body);
}

json HttpClient::put(const std::string& path, const json& body) const {
    auto [scheme, host] = parse_url(base_url_);
    auto hdrs = make_headers(auth_header_, headers_);
    std::string body_str = body.dump();

    httplib::Client cli(scheme + "://" + host);
    cli.set_connection_timeout(timeout_, 0);

    auto res = cli.Put(path, hdrs, body_str, "application/json");
    if (!res) throw SignalWireRestError(0, "Connection failed");
    return handle_response(res->status, res->body);
}

json HttpClient::patch(const std::string& path, const json& body) const {
    auto [scheme, host] = parse_url(base_url_);
    auto hdrs = make_headers(auth_header_, headers_);
    std::string body_str = body.dump();

    httplib::Client cli(scheme + "://" + host);
    cli.set_connection_timeout(timeout_, 0);

    auto res = cli.Patch(path, hdrs, body_str, "application/json");
    if (!res) throw SignalWireRestError(0, "Connection failed");
    return handle_response(res->status, res->body);
}

json HttpClient::del(const std::string& path) const {
    auto [scheme, host] = parse_url(base_url_);
    auto hdrs = make_headers(auth_header_, headers_);

    httplib::Client cli(scheme + "://" + host);
    cli.set_connection_timeout(timeout_, 0);

    auto res = cli.Delete(path, hdrs);
    if (!res) throw SignalWireRestError(0, "Connection failed");
    return handle_response(res->status, res->body);
}

// ============================================================================
// CrudResource
// ============================================================================

CrudResource::CrudResource(const HttpClient& client, const std::string& base_path)
    : client_(client), base_path_(base_path) {}

json CrudResource::list(const std::map<std::string, std::string>& params) const {
    return client_.get(base_path_, params);
}

json CrudResource::create(const json& data) const {
    return client_.post(base_path_, data);
}

json CrudResource::get(const std::string& id) const {
    return client_.get(base_path_ + "/" + id);
}

json CrudResource::update(const std::string& id, const json& data) const {
    return client_.put(base_path_ + "/" + id, data);
}

json CrudResource::del(const std::string& id) const {
    return client_.del(base_path_ + "/" + id);
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
std::map<std::string, std::string>
parse_query_string(const std::string& url) {
    std::map<std::string, std::string> out;
    auto qpos = url.find('?');
    if (qpos == std::string::npos) return out;
    std::string qs = url.substr(qpos + 1);
    // Strip a trailing fragment if any.
    auto frag = qs.find('#');
    if (frag != std::string::npos) qs = qs.substr(0, frag);

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
        if (end == std::string::npos) end = qs.size();
        std::string pair = qs.substr(start, end - start);
        if (!pair.empty()) {
            auto eq = pair.find('=');
            if (eq == std::string::npos) {
                out[decode_one(pair)] = std::string();
            } else {
                out[decode_one(pair.substr(0, eq))] =
                    decode_one(pair.substr(eq + 1));
            }
        }
        if (end == qs.size()) break;
        start = end + 1;
    }
    return out;
}

} // namespace

PaginatedIterator::PaginatedIterator(const HttpClient& http,
                                     const std::string& path,
                                     const std::map<std::string, std::string>& params,
                                     const std::string& data_key)
    : http_(http), path_(path), params_(params), data_key_(data_key) {}

bool PaginatedIterator::has_next() {
    while (index_ >= items_.size()) {
        if (done_) return false;
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
        for (const auto& it : resp[data_key_]) items_.push_back(it);
    }

    std::string next_url;
    bool had_data = !resp.is_object() ? false :
                    (resp.contains(data_key_)
                     && resp[data_key_].is_array()
                     && !resp[data_key_].empty());

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

} // namespace rest
} // namespace signalwire
