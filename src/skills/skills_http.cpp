// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#include "signalwire/skills/skills_http.hpp"

#include "httplib.h"

#include <chrono>
#include <exception>
#include <utility>

namespace signalwire {
namespace skills {

namespace {

// Split a URL of the form `scheme://host[:port]/path?query` into its
// (host_with_scheme_and_port, path_with_query) components. We hand the
// host portion to httplib::Client and keep the path+query for the call.
struct ParsedUrl {
    std::string host;     // e.g. "http://127.0.0.1:1234"
    std::string path;     // e.g. "/api?q=hi" — never empty (defaults to "/")
    bool ok = false;
    std::string error;
};

ParsedUrl parse_url(const std::string& url) {
    ParsedUrl out;
    auto pos = url.find("://");
    if (pos == std::string::npos) {
        out.error = "URL is missing scheme: " + url;
        return out;
    }
    auto rest_start = pos + 3;
    auto path_start = url.find('/', rest_start);
    if (path_start == std::string::npos) {
        out.host = url;
        out.path = "/";
    } else {
        out.host = url.substr(0, path_start);
        out.path = url.substr(path_start);
    }
    if (out.path.empty()) out.path = "/";
    out.ok = true;
    return out;
}

httplib::Headers make_headers(const std::map<std::string, std::string>& m) {
    httplib::Headers hdrs;
    for (const auto& [k, v] : m) hdrs.emplace(k, v);
    return hdrs;
}

}  // namespace

SkillHttpResponse http_get(const std::string& url,
                           const std::map<std::string, std::string>& headers,
                           int timeout_seconds) {
    SkillHttpResponse r;
    auto p = parse_url(url);
    if (!p.ok) {
        r.error = p.error;
        return r;
    }
    // httplib::Client throws std::invalid_argument for an unsupported scheme
    // (e.g. https:// when OpenSSL support is compiled out). Treat that — and
    // any other construction/transport exception — as a transport error
    // (status 0) rather than letting it propagate and terminate the process.
    try {
        httplib::Client cli(p.host);
        cli.set_connection_timeout(timeout_seconds, 0);
        cli.set_read_timeout(timeout_seconds, 0);

        auto res = cli.Get(p.path, make_headers(headers));
        if (!res) {
            r.error = "HTTP GET failed for " + url;
            return r;
        }
        r.status = res->status;
        r.body = res->body;
    } catch (const std::exception& e) {
        r.status = 0;
        r.error = std::string("HTTP GET error for ") + url + ": " + e.what();
    }
    return r;
}

SkillHttpResponse http_get_ms(const std::string& url,
                              const std::map<std::string, std::string>& headers,
                              long timeout_ms) {
    SkillHttpResponse r;
    auto p = parse_url(url);
    if (!p.ok) {
        r.error = p.error;
        return r;
    }
    // A non-positive budget would be an unbounded fetch — clamp to a 10s
    // safety default so a misconfigured per_page_timeout can never hang.
    if (timeout_ms <= 0) timeout_ms = 10000;
    auto dur = std::chrono::milliseconds(timeout_ms);
    // See http_get: an unsupported-scheme URL (e.g. an https:// result link
    // when SSL is compiled out) throws from the Client ctor. web_search feeds
    // arbitrary CSE result links here, so swallow it into a transport error —
    // the page is simply treated as unscrapable and we fall back to snippets.
    try {
        httplib::Client cli(p.host);
        cli.set_connection_timeout(dur);
        cli.set_read_timeout(dur);

        auto res = cli.Get(p.path, make_headers(headers));
        if (!res) {
            r.error = "HTTP GET failed for " + url;
            return r;
        }
        r.status = res->status;
        r.body = res->body;
    } catch (const std::exception& e) {
        r.status = 0;
        r.error = std::string("HTTP GET error for ") + url + ": " + e.what();
    }
    return r;
}

SkillHttpResponse http_post(const std::string& url,
                            const std::string& body,
                            const std::string& content_type,
                            const std::map<std::string, std::string>& headers,
                            int timeout_seconds) {
    SkillHttpResponse r;
    auto p = parse_url(url);
    if (!p.ok) {
        r.error = p.error;
        return r;
    }
    // See http_get: convert an unsupported-scheme / transport exception into a
    // status-0 error rather than terminating the process.
    try {
        httplib::Client cli(p.host);
        cli.set_connection_timeout(timeout_seconds, 0);
        cli.set_read_timeout(timeout_seconds, 0);

        auto res = cli.Post(p.path, make_headers(headers), body, content_type);
        if (!res) {
            r.error = "HTTP POST failed for " + url;
            return r;
        }
        r.status = res->status;
        r.body = res->body;
    } catch (const std::exception& e) {
        r.status = 0;
        r.error = std::string("HTTP POST error for ") + url + ": " + e.what();
    }
    return r;
}

}  // namespace skills
}  // namespace signalwire
