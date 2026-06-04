// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
//
// Implementation of the TLS capability-test helpers. See tls_mocktest.hpp.
#include "tls_mocktest.hpp"

#include <cstdlib>
#include <stdexcept>
#include <sys/stat.h>

#include "httplib.h"

namespace signalwire {
namespace tlstest {

namespace {

int env_int(const char* name, int fallback) {
    if (const char* v = std::getenv(name)) {
        if (v && *v) {
            try { return std::stoi(v); } catch (...) {}
        }
    }
    return fallback;
}

// Walk up from PROJECT_SOURCE_DIR (injected by CMake) looking for the adjacent
// porting-sdk/test_harness/tls/certs directory.
std::string discover_certs_dir() {
#ifdef PROJECT_SOURCE_DIR
    std::string dir = PROJECT_SOURCE_DIR;
#else
    std::string dir = ".";
#endif
    while (true) {
        while (dir.size() > 1 && dir.back() == '/') dir.pop_back();
        auto slash = dir.find_last_of('/');
        if (slash == std::string::npos || slash == 0) return std::string();
        std::string parent = dir.substr(0, slash);
        std::string candidate = parent + "/porting-sdk/test_harness/tls/certs";
        struct stat st;
        if (::stat((candidate + "/ca.crt").c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
            return candidate;
        }
        if (parent == dir) return std::string();
        dir = parent;
    }
}

// An https:// httplib client that trusts the test CA and verifies the server
// certificate (NEVER disabled). Used to drive the REST mock's HTTPS control
// plane in TLS mode.
httplib::Client tls_control_client(const std::string& base) {
    httplib::Client cli(base);
    cli.set_connection_timeout(5, 0);
    cli.set_read_timeout(10, 0);
    std::string ca = ca_cert_path();
    if (!ca.empty()) {
        cli.set_ca_cert_path(ca.c_str());
        cli.enable_server_certificate_verification(true);
    }
    return cli;
}

httplib::Client plain_control_client(const std::string& host, int port) {
    httplib::Client cli(host, port);
    cli.set_connection_timeout(5, 0);
    cli.set_read_timeout(10, 0);
    return cli;
}

} // namespace

std::string ca_cert_path() {
    static const std::string path = []() {
        std::string dir = discover_certs_dir();
        return dir.empty() ? std::string() : dir + "/ca.crt";
    }();
    return path;
}

std::string trust_test_ca() {
    std::string ca = ca_cert_path();
    if (!ca.empty()) ::setenv("SSL_CERT_FILE", ca.c_str(), 1);
    return ca;
}

// ---- REST -----------------------------------------------------------------

int rest_tls_port() { return env_int("MOCK_SIGNALWIRE_TLS_PORT", 8773); }

std::string rest_tls_base_url() {
    return "https://127.0.0.1:" + std::to_string(rest_tls_port());
}

std::string rest_tls_control_base() { return rest_tls_base_url(); }

bool rest_tls_available() {
    auto cli = tls_control_client(rest_tls_control_base());
    auto res = cli.Get("/__mock__/health");
    return res && res->status == 200;
}

void rest_journal_reset() {
    auto cli = tls_control_client(rest_tls_control_base());
    cli.Post("/__mock__/journal/reset", "", "application/json");
    cli.Post("/__mock__/scenarios/reset", "", "application/json");
}

json rest_journal_last() {
    auto cli = tls_control_client(rest_tls_control_base());
    auto res = cli.Get("/__mock__/journal");
    if (!res || res->status != 200) {
        throw std::runtime_error("tls_mocktest: REST journal GET failed");
    }
    json arr = json::parse(res->body);
    if (!arr.is_array() || arr.empty()) {
        throw std::runtime_error("tls_mocktest: REST journal empty");
    }
    return arr.back();
}

// ---- RELAY ----------------------------------------------------------------

int relay_tls_ws_port() { return env_int("MOCK_RELAY_TLS_PORT", 8783); }

int relay_tls_http_port() {
    return env_int("MOCK_RELAY_TLS_HTTP_PORT", relay_tls_ws_port() + 1000);
}

std::string relay_tls_http_url() {
    return "http://127.0.0.1:" + std::to_string(relay_tls_http_port());
}

bool relay_tls_available() {
    auto cli = plain_control_client("127.0.0.1", relay_tls_http_port());
    auto res = cli.Get("/__mock__/health");
    return res && res->status == 200;
}

void relay_journal_reset() {
    auto cli = plain_control_client("127.0.0.1", relay_tls_http_port());
    cli.Post("/__mock__/journal/reset", "", "application/json");
    cli.Post("/__mock__/scenarios/reset", "", "application/json");
}

std::vector<json> relay_journal_recv(const std::string& method) {
    auto cli = plain_control_client("127.0.0.1", relay_tls_http_port());
    auto res = cli.Get("/__mock__/journal");
    std::vector<json> out;
    if (!res || res->status != 200) return out;
    json arr = json::parse(res->body);
    if (!arr.is_array()) return out;
    for (const auto& e : arr) {
        if (e.value("direction", "") != "recv") continue;
        if (!method.empty() && e.value("method", "") != method) continue;
        out.push_back(e);
    }
    return out;
}

} // namespace tlstest
} // namespace signalwire
