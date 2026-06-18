// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
//
// mocktest.hpp -- C++ test helper for the porting-sdk mock_signalwire HTTP
// server. Mirrors the Python conftest fixtures (signalwire_client + mock) and
// the Go pilot pkg/rest/internal/mocktest so unit tests can exercise the real
// SDK code path against a real HTTP server backed by SignalWire's 13 OpenAPI
// specs.
//
// The mock server is per-process: the first call to ensure_server() probes
// http://127.0.0.1:<port>/__mock__/health and either confirms a running
// instance or spawns one as a subprocess. Each test gets a freshly reset
// journal/scenario state via reset() before it runs.
//
// The default port is 8772 (matching the C++ rollout slot). Override via the
// MOCK_SIGNALWIRE_PORT environment variable to share a pre-running instance.
#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

#include "signalwire/rest/rest_client.hpp"

namespace signalwire {
namespace rest {
namespace mocktest {

using nlohmann::json;

// JournalEntry mirrors mock_signalwire.journal.JournalEntry over the wire.
struct JournalEntry {
    double timestamp = 0.0;
    std::string method;
    std::string path;
    std::map<std::string, std::vector<std::string>> query_params;
    std::map<std::string, std::string> headers;
    json body;                   // Either a JSON object/array or a string
    std::optional<std::string> matched_route;
    std::optional<int> response_status;

    // Convenience: returns true iff the request body parses as a JSON object.
    bool body_is_object() const { return body.is_object(); }
};

// Probe-or-spawn the mock server. Returns the configured base URL once the
// server answers /__mock__/health. Throws std::runtime_error if startup
// fails. Idempotent across calls within the test process.
std::string ensure_server();

// Resolve the port the helper will target. Honours MOCK_SIGNALWIRE_PORT;
// otherwise falls back to 8772 (the C++ rollout slot).
int resolve_port();

// ── Session isolation ────────────────────────────────────────────────────
//
// REST is pure request/response with NO handshake, so the mock needs no
// session change for the SDK: each request is self-identifying via its
// `Authorization: Basic base64(project:token)` header. make_client() gives
// each test's client a UNIQUE RANDOM project (`test_proj_<hex>`) => a unique
// auth header, recorded into a THREAD-LOCAL "active scope". The parallel
// runner gives each test its own thread, so the harness's free functions
// implicitly scope to the client this thread built:
//   - journal()/journal_last() filter the shared global journal CLIENT-SIDE
//     to this auth header, so a test only ever sees its own requests;
//   - journal_reset() is a NO-OP when scoped (a scoped view starts empty and a
//     global wipe would race a concurrent test);
//   - scenario_set() scopes the override SERVER-SIDE via
//     `?session_id=<auth header>` (the mock keys REST scenarios by auth header).
// An empty active scope => legacy global/unscoped behavior.

// Set the thread-local active scope (project + its Basic-auth header).
// make_client() calls this; tests building a RestClient by hand can too.
void set_active_scope(const std::string& project, const std::string& auth_header);

// The thread-local active project ("" if unscoped). Tests that assert on the
// AccountSid embedded in a LAML path read THIS instead of hard-coding
// "test_proj", because each test now authenticates with a random project.
std::string active_project();

// The thread-local active Authorization header ("" if unscoped).
std::string active_auth_header();

// Clear the thread-local active scope.
void clear_active_scope();

// Reset the mock server's journal + scenarios. NO-OP when the active scope is
// set (the scoped journal view starts empty; a global wipe would race a
// concurrent test). Unscoped callers do the legacy global reset.
void journal_reset();

// Read this client's recorded journal entries in arrival order. Filtered to
// the active scope's auth header when set (so a parallel test never sees
// another test's requests); unscoped callers see the whole journal.
std::vector<JournalEntry> journal();

// Return the most recent journal entry for THIS client. Throws
// std::runtime_error when this client made no request yet.
JournalEntry journal_last();

// Stage a one-shot response override for the named operation id. Scoped to the
// active auth header (server-side) when set, so a concurrent test can't consume
// it. Subsequent hits fall back to spec synthesis. The endpoint id is the
// OpenAPI operationId; the active list is exposed at /__mock__/scenarios.
void scenario_set(const std::string& endpoint_id,
                  int status,
                  const json& body);

// Build a RestClient pointed at the mock server with a UNIQUE RANDOM project
// (`test_proj_<hex>`) and token="test_tok", and set this thread's active scope
// to it. The random project makes the auth header unique per test so the
// shared mock is safe under parallel execution. No global reset is performed:
// the scoped (auth-filtered) journal view starts empty.
RestClient make_client();

} // namespace mocktest
} // namespace rest
} // namespace signalwire
