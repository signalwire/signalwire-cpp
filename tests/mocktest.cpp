// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
//
// mocktest.cpp -- shared mock-server harness implementation. Spawns
// `python -m mock_signalwire` on the first ensure_server() call when nothing
// is already listening. Mirrors the Python conftest fixtures and the Go
// pilot pkg/rest/internal/mocktest.
#include "mocktest.hpp"

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
namespace rest {
namespace mocktest {

namespace {

constexpr int kDefaultPort = 8772;
constexpr int kStartupTimeoutSeconds = 30;

// Serialise port resolution + spawn so concurrent tests don't fight over
// the singleton.
std::mutex& server_mutex() {
    static std::mutex m;
    return m;
}

bool& server_started() {
    static bool started = false;
    return started;
}

std::string& server_url_cache() {
    static std::string url;
    return url;
}

// Parse a base URL like http://127.0.0.1:8772 into (host, port).
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
        return j.contains("specs_loaded");
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
        throw std::runtime_error("mocktest: POST " + path + " failed");
    }
    if (res->status != 200) {
        throw std::runtime_error("mocktest: POST " + path + " returned status "
                                 + std::to_string(res->status));
    }
}

// Walk this source file's directory upward looking for an adjacent
// porting-sdk/test_harness/<name>/<name>/__init__.py. Returns the
// absolute path to the directory containing the Python package (the
// value to put on PYTHONPATH so that `python -m <name>` resolves), or
// the empty string when no adjacent porting-sdk is reachable.
//
// We anchor at PROJECT_SOURCE_DIR (injected via target_compile_definitions
// from CMakeLists.txt) so the walk is independent of the build directory
// or the working directory at test-run time. Fresh adjacent clones —
// porting-sdk next to signalwire-cpp — are all that's required.
std::string discover_porting_sdk_package(const std::string& name) {
#ifndef PROJECT_SOURCE_DIR
    // Fall back to compile-time __FILE__ if the cmake macro wasn't injected.
    std::string anchor = __FILE__;
    auto last = anchor.find_last_of('/');
    if (last != std::string::npos) anchor = anchor.substr(0, last);
#else
    std::string anchor = PROJECT_SOURCE_DIR;
#endif
    std::string dir = anchor;
    while (true) {
        // Strip trailing slash for consistent parent computation.
        while (dir.size() > 1 && dir.back() == '/') dir.pop_back();
        auto last = dir.find_last_of('/');
        if (last == std::string::npos || last == 0) {
            // No more parents to walk to.
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

// Spawn `python -m mock_signalwire --port <port>` and detach it so the test
// process isn't blocked waiting on its pipes when the test binary exits.
void spawn_mock_server(int port) {
    // Resolve adjacency once in the parent so we don't fork pointlessly
    // and so the child can simply read the value rather than re-walking.
    // When the walk fails (e.g. porting-sdk is not adjacent), we still
    // spawn — the child falls back to whatever is on the system Python's
    // sys.path, and the readiness probe surfaces a clear timeout error
    // if neither mode is available.
    std::string pkg_dir = discover_porting_sdk_package("mock_signalwire");

    pid_t pid = fork();
    if (pid < 0) {
        throw std::runtime_error("mocktest: fork() failed");
    }
    if (pid == 0) {
        // Child: detach stdio (pipe-drain on test exit must not block) and
        // start a new session so a Ctrl-C against the test binary doesn't
        // also kill the mock.
        int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) {
            dup2(devnull, STDIN_FILENO);
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }
        setsid();
        // Inject porting-sdk/test_harness/mock_signalwire/ into PYTHONPATH
        // so `python -m mock_signalwire` resolves without a prior
        // `pip install -e ...`. When pkg_dir is empty (no adjacent
        // porting-sdk) the child relies on the system Python's sys.path.
        if (!pkg_dir.empty()) {
            const char* existing = std::getenv("PYTHONPATH");
            std::string new_pp = pkg_dir;
            if (existing && *existing) {
                new_pp.push_back(':');
                new_pp.append(existing);
            }
            ::setenv("PYTHONPATH", new_pp.c_str(), 1);
        }
        std::string port_str = std::to_string(port);
        execlp("python", "python",
               "-m", "mock_signalwire",
               "--host", "127.0.0.1",
               "--port", port_str.c_str(),
               "--log-level", "error",
               (char*)nullptr);
        _exit(127);
    }
    // Parent: don't wait. The OS reaps the orphan when the test process
    // exits. We just fire-and-forget; ensure_server polls health until
    // ready or times out.
}

} // namespace

int resolve_port() {
    if (const char* env = std::getenv("MOCK_SIGNALWIRE_PORT")) {
        if (env && *env) {
            try { return std::stoi(env); } catch (...) {}
        }
    }
    return kDefaultPort;
}

std::string ensure_server() {
    std::lock_guard<std::mutex> lock(server_mutex());
    if (server_started()) return server_url_cache();

    int port = resolve_port();
    std::string url = "http://127.0.0.1:" + std::to_string(port);

    if (probe_health(url)) {
        server_started() = true;
        server_url_cache() = url;
        return url;
    }

    spawn_mock_server(port);

    auto deadline = std::chrono::steady_clock::now()
                  + std::chrono::seconds(kStartupTimeoutSeconds);
    while (std::chrono::steady_clock::now() < deadline) {
        if (probe_health(url)) {
            server_started() = true;
            server_url_cache() = url;
            return url;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
    }

    throw std::runtime_error(
        "mocktest: `python -m mock_signalwire` did not become ready within "
        + std::to_string(kStartupTimeoutSeconds) + "s on port "
        + std::to_string(port)
        + " (clone porting-sdk next to signalwire-cpp so tests can find "
        + "porting-sdk/test_harness/mock_signalwire/, pip install the "
        + "mock_signalwire package, or set MOCK_SIGNALWIRE_PORT to a pre-running instance)");
}

void journal_reset() {
    std::string url = ensure_server();
    post_no_body(url, "/__mock__/journal/reset");
    post_no_body(url, "/__mock__/scenarios/reset");
}

std::vector<JournalEntry> journal() {
    std::string url = ensure_server();
    auto [host, port] = split_url(url);
    httplib::Client cli(host, port);
    cli.set_connection_timeout(5, 0);
    cli.set_read_timeout(5, 0);
    auto res = cli.Get("/__mock__/journal");
    if (!res) {
        throw std::runtime_error("mocktest: GET /__mock__/journal failed");
    }
    if (res->status != 200) {
        throw std::runtime_error("mocktest: GET /__mock__/journal returned "
                                 + std::to_string(res->status));
    }
    json arr = json::parse(res->body);
    std::vector<JournalEntry> out;
    out.reserve(arr.size());
    for (const auto& e : arr) {
        JournalEntry je;
        if (e.contains("timestamp") && e["timestamp"].is_number()) {
            je.timestamp = e["timestamp"].get<double>();
        }
        if (e.contains("method") && e["method"].is_string()) {
            je.method = e["method"].get<std::string>();
        }
        if (e.contains("path") && e["path"].is_string()) {
            je.path = e["path"].get<std::string>();
        }
        if (e.contains("query_params") && e["query_params"].is_object()) {
            for (auto it = e["query_params"].begin();
                 it != e["query_params"].end(); ++it) {
                std::vector<std::string> values;
                if (it.value().is_array()) {
                    for (const auto& v : it.value()) {
                        if (v.is_string()) values.push_back(v.get<std::string>());
                    }
                }
                je.query_params[it.key()] = std::move(values);
            }
        }
        if (e.contains("headers") && e["headers"].is_object()) {
            for (auto it = e["headers"].begin(); it != e["headers"].end(); ++it) {
                if (it.value().is_string()) {
                    je.headers[it.key()] = it.value().get<std::string>();
                }
            }
        }
        if (e.contains("body")) je.body = e["body"];
        if (e.contains("matched_route") && e["matched_route"].is_string()) {
            je.matched_route = e["matched_route"].get<std::string>();
        }
        if (e.contains("response_status") && e["response_status"].is_number()) {
            je.response_status = e["response_status"].get<int>();
        }
        out.push_back(std::move(je));
    }
    return out;
}

JournalEntry journal_last() {
    auto entries = journal();
    if (entries.empty()) {
        throw std::runtime_error(
            "mocktest: journal is empty - SDK call did not reach the mock server");
    }
    return entries.back();
}

void scenario_set(const std::string& endpoint_id, int status, const json& body) {
    std::string url = ensure_server();
    auto [host, port] = split_url(url);
    httplib::Client cli(host, port);
    cli.set_connection_timeout(5, 0);
    cli.set_read_timeout(5, 0);
    json payload = {{"status", status}, {"response", body}};
    auto res = cli.Post("/__mock__/scenarios/" + endpoint_id,
                        payload.dump(), "application/json");
    if (!res) {
        throw std::runtime_error("mocktest: POST /__mock__/scenarios failed");
    }
    if (res->status != 200) {
        throw std::runtime_error(
            "mocktest: POST /__mock__/scenarios/" + endpoint_id
            + " returned status " + std::to_string(res->status));
    }
}

RestClient make_client() {
    std::string url = ensure_server();
    journal_reset();
    return RestClient::with_base_url(url, "test_proj", "test_tok");
}

} // namespace mocktest
} // namespace rest
} // namespace signalwire
