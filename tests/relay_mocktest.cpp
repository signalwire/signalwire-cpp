// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
//
// relay_mocktest.cpp -- shared mock-relay harness implementation. Spawns
// `python -m mock_relay --ws-port <ws> --http-port <http>` on the first
// ensure_server() call when nothing is already listening. Mirrors the
// Python conftest fixtures and the REST mocktest.cpp implementation.
#include "relay_mocktest.hpp"

#include <cctype>
#include <chrono>
#include <cstdlib>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>

#include "httplib.h"

namespace signalwire {
namespace relay {
namespace mocktest {

namespace {

constexpr int kDefaultWsPort = 8782;
constexpr int kStartupTimeoutSeconds = 30;

std::mutex& server_mutex() {
    static std::mutex m;
    return m;
}

bool& server_started() {
    static bool started = false;
    return started;
}

std::string& http_url_cache() {
    static std::string url;
    return url;
}

// Thread-local active session id. make_client() sets it; the parallel runner
// gives each test its own thread, so harness calls scope to the test's client.
std::string& active_session_ref() {
    thread_local std::string sid;
    return sid;
}

// URL-encode a query value (session ids are hex, but be correct anyway).
std::string url_encode(const std::string& s) {
    static const char* hexd = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(hexd[c >> 4]);
            out.push_back(hexd[c & 0xF]);
        }
    }
    return out;
}

// "?session_id=<active>" when scoped, else "".
std::string session_query() {
    const std::string& sid = active_session_ref();
    return sid.empty() ? std::string() : ("?session_id=" + url_encode(sid));
}

std::pair<std::string, int> split_url(const std::string& base) {
    std::string s = base;
    auto pos = s.find("://");
    if (pos != std::string::npos) s = s.substr(pos + 3);
    auto cpos = s.find(':');
    std::string host = (cpos == std::string::npos) ? s : s.substr(0, cpos);
    int port = (cpos == std::string::npos) ? 80 : std::stoi(s.substr(cpos + 1));
    return {host, port};
}

bool probe_health(const std::string& base_url) {
    auto [host, port] = split_url(base_url);
    httplib::Client cli(host, port);
    cli.set_connection_timeout(2, 0);
    cli.set_read_timeout(2, 0);
    auto res = cli.Get("/__mock__/health");
    if (!res || res->status != 200) return false;
    try {
        auto j = json::parse(res->body);
        return j.contains("schemas_loaded");
    } catch (...) {
        return false;
    }
}

void post_no_body(const std::string& base_url, const std::string& path) {
    auto [host, port] = split_url(base_url);
    httplib::Client cli(host, port);
    cli.set_connection_timeout(5, 0);
    cli.set_read_timeout(5, 0);
    auto res = cli.Post(path, "", "application/json");
    if (!res) {
        throw std::runtime_error("relay_mocktest: POST " + path + " failed");
    }
    if (res->status != 200) {
        throw std::runtime_error("relay_mocktest: POST " + path + " returned status "
                                 + std::to_string(res->status));
    }
}

json post_json(const std::string& base_url, const std::string& path,
               const json& body) {
    auto [host, port] = split_url(base_url);
    httplib::Client cli(host, port);
    cli.set_connection_timeout(15, 0);
    cli.set_read_timeout(30, 0);
    auto res = cli.Post(path, body.dump(), "application/json");
    if (!res) {
        throw std::runtime_error("relay_mocktest: POST " + path + " failed");
    }
    if (res->status != 200) {
        throw std::runtime_error(
            "relay_mocktest: POST " + path + " returned status "
            + std::to_string(res->status) + " body=" + res->body);
    }
    if (res->body.empty()) return json::object();
    try {
        return json::parse(res->body);
    } catch (...) {
        return json::object();
    }
}

json get_json(const std::string& base_url, const std::string& path) {
    auto [host, port] = split_url(base_url);
    httplib::Client cli(host, port);
    cli.set_connection_timeout(5, 0);
    cli.set_read_timeout(5, 0);
    auto res = cli.Get(path);
    if (!res) {
        throw std::runtime_error("relay_mocktest: GET " + path + " failed");
    }
    if (res->status != 200) {
        throw std::runtime_error("relay_mocktest: GET " + path + " returned "
                                 + std::to_string(res->status));
    }
    return json::parse(res->body);
}

// Walk this source file's directory upward looking for an adjacent
// porting-sdk/test_harness/<name>/<name>/__init__.py. Returns the
// absolute path to the directory containing the Python package or "" if
// not found. We anchor at PROJECT_SOURCE_DIR (injected from CMakeLists.txt)
// so the walk is independent of CWD at test-run time.
std::string discover_porting_sdk_package(const std::string& name) {
#ifndef PROJECT_SOURCE_DIR
    std::string anchor = __FILE__;
    auto last = anchor.find_last_of('/');
    if (last != std::string::npos) anchor = anchor.substr(0, last);
#else
    std::string anchor = PROJECT_SOURCE_DIR;
#endif
    std::string dir = anchor;
    while (true) {
        while (dir.size() > 1 && dir.back() == '/') dir.pop_back();
        auto last = dir.find_last_of('/');
        if (last == std::string::npos || last == 0) {
            return std::string();
        }
        std::string parent = dir.substr(0, last);
        std::string candidate = parent + "/porting-sdk/test_harness/" + name;
        std::string init = candidate + "/" + name + "/__init__.py";
        struct stat st;
        if (::stat(init.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
            return candidate;
        }
        if (parent == dir) return std::string();
        dir = parent;
    }
}

void spawn_mock_server(int ws_port, int http_port) {
    std::string pkg_dir = discover_porting_sdk_package("mock_relay");

    pid_t pid = fork();
    if (pid < 0) {
        throw std::runtime_error("relay_mocktest: fork() failed");
    }
    if (pid == 0) {
        int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) {
            dup2(devnull, STDIN_FILENO);
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }
        setsid();
        if (!pkg_dir.empty()) {
            const char* existing = std::getenv("PYTHONPATH");
            std::string new_pp = pkg_dir;
            if (existing && *existing) {
                new_pp.push_back(':');
                new_pp.append(existing);
            }
            ::setenv("PYTHONPATH", new_pp.c_str(), 1);
        }
        std::string ws_str = std::to_string(ws_port);
        std::string http_str = std::to_string(http_port);
        execlp("python", "python",
               "-m", "mock_relay",
               "--host", "127.0.0.1",
               "--ws-port", ws_str.c_str(),
               "--http-port", http_str.c_str(),
               "--log-level", "error",
               (char*)nullptr);
        _exit(127);
    }
    // Parent: don't wait. ensure_server() polls health until ready.
}

JournalEntry parse_entry(const json& e) {
    JournalEntry je;
    if (e.contains("timestamp") && e["timestamp"].is_number()) {
        je.timestamp = e["timestamp"].get<double>();
    }
    if (e.contains("direction") && e["direction"].is_string()) {
        je.direction = e["direction"].get<std::string>();
    }
    if (e.contains("method") && e["method"].is_string()) {
        je.method = e["method"].get<std::string>();
    }
    if (e.contains("request_id") && e["request_id"].is_string()) {
        je.request_id = e["request_id"].get<std::string>();
    }
    if (e.contains("connection_id") && e["connection_id"].is_string()) {
        je.connection_id = e["connection_id"].get<std::string>();
    }
    if (e.contains("session_id") && e["session_id"].is_string()) {
        je.session_id = e["session_id"].get<std::string>();
    }
    if (e.contains("frame")) je.frame = e["frame"];
    return je;
}

} // namespace

int resolve_ws_port() {
    if (const char* env = std::getenv("MOCK_RELAY_PORT")) {
        if (env && *env) {
            try { return std::stoi(env); } catch (...) {}
        }
    }
    return kDefaultWsPort;
}

int resolve_http_port() {
    if (const char* env = std::getenv("MOCK_RELAY_HTTP_PORT")) {
        if (env && *env) {
            try { return std::stoi(env); } catch (...) {}
        }
    }
    return resolve_ws_port() + 1000;
}

std::string ensure_server() {
    std::lock_guard<std::mutex> lock(server_mutex());
    if (server_started()) return http_url_cache();

    int ws_port = resolve_ws_port();
    int http_port = resolve_http_port();
    std::string url = "http://127.0.0.1:" + std::to_string(http_port);

    if (probe_health(url)) {
        server_started() = true;
        http_url_cache() = url;
        return url;
    }

    spawn_mock_server(ws_port, http_port);

    auto deadline = std::chrono::steady_clock::now()
                  + std::chrono::seconds(kStartupTimeoutSeconds);
    while (std::chrono::steady_clock::now() < deadline) {
        if (probe_health(url)) {
            server_started() = true;
            http_url_cache() = url;
            return url;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
    }

    throw std::runtime_error(
        "relay_mocktest: `python -m mock_relay` did not become ready within "
        + std::to_string(kStartupTimeoutSeconds) + "s on ws=" + std::to_string(ws_port)
        + " http=" + std::to_string(http_port)
        + " (clone porting-sdk next to signalwire-cpp so tests find "
        + "porting-sdk/test_harness/mock_relay/, or set MOCK_RELAY_PORT / "
        + "MOCK_RELAY_HTTP_PORT to a pre-running instance)");
}

void force_ws_scheme() {
    static std::once_flag scheme_once;
    std::call_once(scheme_once, [] { ::setenv("SIGNALWIRE_RELAY_SCHEME", "ws", 1); });
}

void set_active_session(const std::string& session_id) {
    active_session_ref() = session_id;
}

std::string active_session() {
    return active_session_ref();
}

void clear_active_session() {
    active_session_ref().clear();
}

void reset() {
    std::string url = ensure_server();
    std::string q = session_query();
    post_no_body(url, "/__mock__/journal/reset" + q);
    post_no_body(url, "/__mock__/scenarios/reset" + q);
}

std::vector<JournalEntry> journal() {
    std::string url = ensure_server();
    json arr = get_json(url, "/__mock__/journal" + session_query());
    std::vector<JournalEntry> out;
    if (arr.is_array()) {
        out.reserve(arr.size());
        for (const auto& e : arr) {
            out.push_back(parse_entry(e));
        }
    }
    return out;
}

std::vector<JournalEntry> journal_recv(const std::string& method) {
    auto all = journal();
    std::vector<JournalEntry> out;
    for (auto& e : all) {
        if (e.direction != "recv") continue;
        if (!method.empty() && e.method != method) continue;
        out.push_back(e);
    }
    return out;
}

std::vector<JournalEntry> journal_send(const std::string& event_type) {
    auto all = journal();
    std::vector<JournalEntry> out;
    for (auto& e : all) {
        if (e.direction != "send") continue;
        if (event_type.empty()) {
            out.push_back(e);
            continue;
        }
        if (!e.frame.contains("method")) continue;
        if (e.frame.value("method", "") != "signalwire.event") continue;
        json p = e.frame.value("params", json::object());
        if (!p.is_object()) continue;
        if (p.value("event_type", "") != event_type) continue;
        out.push_back(e);
    }
    return out;
}

JournalEntry journal_last() {
    auto entries = journal();
    if (entries.empty()) {
        throw std::runtime_error(
            "relay_mocktest: journal is empty - SDK call did not reach the mock");
    }
    return entries.back();
}

JournalEntry journal_last_recv(const std::string& method) {
    auto entries = journal_recv(method);
    if (entries.empty()) {
        throw std::runtime_error(
            "relay_mocktest: no recv entry with method=" + method);
    }
    return entries.back();
}

void arm_method(const std::string& method, const json& events) {
    std::string url = ensure_server();
    // Scope the scenario to this session so a parallel test can't consume it.
    post_json(url, "/__mock__/scenarios/" + method + session_query(), events);
}

void arm_dial(const json& body) {
    std::string url = ensure_server();
    post_json(url, "/__mock__/scenarios/dial" + session_query(), body);
}

json push(const json& frame, const std::string& session_id) {
    std::string url = ensure_server();
    // Explicit arg wins; otherwise target this thread's active session so the
    // frame reaches only this test's client (empty => broadcast, legacy).
    std::string target = session_id.empty() ? active_session_ref() : session_id;
    std::string path = "/__mock__/push";
    if (!target.empty()) path += "?session_id=" + url_encode(target);
    return post_json(url, path, {{"frame", frame}});
}

// Stamp each push/expect_recv op of a scenario_play timeline with the active
// session id (unless it already carries one), so the timeline targets only
// this test's client and expect_recv matches only this session's frames.
static json scope_ops(const json& ops) {
    const std::string& sid = active_session_ref();
    if (sid.empty() || !ops.is_array()) return ops;
    json out = json::array();
    for (const auto& op : ops) {
        json o = op;
        if (o.is_object()) {
            for (const char* key : {"push", "expect_recv"}) {
                if (o.contains(key) && o[key].is_object()
                    && !o[key].contains("session_id")) {
                    o[key]["session_id"] = sid;
                }
            }
        }
        out.push_back(std::move(o));
    }
    return out;
}

json scenario_play(const json& ops) {
    std::string url = ensure_server();
    return post_json(url, "/__mock__/scenario_play", scope_ops(ops));
}

json inbound_call(const InboundCallOpts& opts) {
    std::string url = ensure_server();
    json body;
    body["from_number"] = opts.from_number;
    body["to_number"] = opts.to_number;
    body["context"] = opts.context;
    body["delay_ms"] = opts.delay_ms;
    if (!opts.auto_states.empty()) {
        body["auto_states"] = opts.auto_states;
    } else {
        body["auto_states"] = json::array({"created"});
    }
    if (!opts.call_id.empty()) body["call_id"] = opts.call_id;
    // Explicit opts.session_id wins; otherwise target this thread's active
    // session so the inbound-call sequence reaches only this test's client.
    std::string target = opts.session_id.empty() ? active_session_ref()
                                                  : opts.session_id;
    if (!target.empty()) body["session_id"] = target;
    return post_json(url, "/__mock__/inbound_call", body);
}

std::vector<json> sessions() {
    std::string url = ensure_server();
    json j = get_json(url, "/__mock__/sessions");
    std::vector<json> out;
    if (j.contains("sessions") && j["sessions"].is_array()) {
        for (auto& s : j["sessions"]) out.push_back(s);
    }
    return out;
}

RelayConfig make_config(const std::string& project, const std::string& token) {
    // ensure server is running so the host string is valid.
    ensure_server();
    RelayConfig cfg;
    cfg.project = project;
    cfg.token = token;
    cfg.host = "127.0.0.1";
    cfg.port = resolve_ws_port();
    cfg.contexts = {"default"};
    return cfg;
}

std::unique_ptr<RelayClient> make_client(const std::string& project,
                                          const std::string& token,
                                          const std::vector<std::string>& contexts) {
    ensure_server();
    // Drop any prior thread-local scope before connecting so we don't, e.g.,
    // accidentally inherit a previous test's session on a reused worker thread.
    clear_active_session();
    // Force plain WS scheme in the global env so RelayClient::connect()
    // takes the connect_plain() path. This applies to every test in the
    // process; the production code path is still exercised by the regular
    // `connect()` overload. Set exactly once (process-global env mutated from
    // multiple worker threads under the parallel runner).
    force_ws_scheme();
    RelayConfig cfg = make_config(project, token);
    cfg.contexts = contexts;
    auto client = std::make_unique<RelayClient>(cfg);
    if (!client->connect()) {
        throw std::runtime_error(
            "relay_mocktest: client connect() failed (mock URL "
            + http_url_cache() + ")");
    }
    // Scope this thread's subsequent harness calls to THIS client's session.
    // No reset is needed: a brand-new session starts with an empty (scoped)
    // journal/scenario view, so the test sees a clean slate and never disturbs
    // a concurrent test's session.
    set_active_session(client->session_id());
    return client;
}

bool wait_for_session(int timeout_ms) {
    auto deadline = std::chrono::steady_clock::now()
                  + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        try {
            if (!sessions().empty()) return true;
        } catch (...) {}
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return false;
}

Call* drive_inbound_call(RelayClient& client,
                         const std::string& call_id,
                         const std::vector<std::string>& auto_states,
                         int timeout_ms) {
    InboundCallOpts opts;
    opts.call_id = call_id;
    opts.auto_states = auto_states;
    opts.delay_ms = 5;
    inbound_call(opts);

    auto deadline = std::chrono::steady_clock::now()
                  + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        Call* c = client.find_call(call_id);
        if (c) return c;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return nullptr;
}

} // namespace mocktest
} // namespace relay
} // namespace signalwire
