// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
//
// relay_mocktest.hpp -- C++ test helper for the porting-sdk mock_relay
// WebSocket server. Mirrors the Python conftest fixtures
// (signalwire_relay_client + mock_relay) so unit tests can exercise the real
// SDK code path against a real WebSocket server backed by SignalWire's
// switchblade-derived JSON Schemas.
//
// The mock server is per-process: the first call to ensure_server() probes
// http://127.0.0.1:<HTTP_PORT>/__mock__/health and either confirms a running
// instance or spawns one as a subprocess. Each test gets a freshly reset
// journal/scenario state via reset() before it runs.
//
// The default WebSocket port is 8782 (the C++ rollout slot for relay) and
// the HTTP control plane defaults to 9782 (WS_PORT + 1000). Override via
// the MOCK_RELAY_PORT / MOCK_RELAY_HTTP_PORT environment variables.
#pragma once

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

#include "signalwire/relay/client.hpp"

namespace signalwire {
namespace relay {
namespace mocktest {

using nlohmann::json;

// JournalEntry mirrors mock_relay.journal.JournalEntry over the wire.
struct JournalEntry {
    double timestamp = 0.0;
    std::string direction;     // "recv" | "send"
    std::string method;        // JSON-RPC method or empty for responses
    std::string request_id;
    json frame;                // The full JSON-RPC frame
    std::string connection_id;
    std::string session_id;
};

// Probe-or-spawn the mock server. Returns the configured HTTP base URL once
// the server answers /__mock__/health. Throws std::runtime_error if startup
// fails. Idempotent across calls within the test process.
std::string ensure_server();

// Resolve the WebSocket port. Honours MOCK_RELAY_PORT; falls back to 8782.
int resolve_ws_port();

// Resolve the HTTP control-plane port. Honours MOCK_RELAY_HTTP_PORT; falls
// back to ws_port + 1000.
int resolve_http_port();

// ── Session isolation ────────────────────────────────────────────────────
//
// The mock's journal AND scenario store are session-scoped on the server by
// the RELAY handshake `sessionid` (the server returns it in the connect
// result; RelayClient::session_id() captures it). To make the shared mock
// safe under parallel test execution, every control-plane call below
// (journal read, journal/scenario reset, push, inbound_call, scenario_play,
// arm_method, arm_dial) threads `?session_id=<active>` so a test only ever
// sees/targets its own frames.
//
// The "active session" is a THREAD-LOCAL set automatically by make_client()
// to the new client's session_id(). Because the parallel test runner gives
// each test case its own thread, a test's harness calls are implicitly scoped
// to the client it built — no signature change at the ~130 call sites. Tests
// that build a RelayClient by hand call set_active_session(client.session_id())
// themselves. An empty active session means "global" (legacy single-threaded /
// broadcast), preserving the old behavior for any unscoped caller.

// Force the plain-WS scheme (SIGNALWIRE_RELAY_SCHEME=ws) exactly once. Tests
// that build a RelayClient by hand call this instead of setenv() directly, so
// the process-global env isn't mutated concurrently by parallel workers.
void force_ws_scheme();

// Set the thread-local active session id. Subsequent harness calls on this
// thread scope to it. Pass "" to clear (global/broadcast).
void set_active_session(const std::string& session_id);

// Read the thread-local active session id ("" if none).
std::string active_session();

// Clear the thread-local active session id (== set_active_session("")).
void clear_active_session();

// Reset the mock server's journal + scenario queues. Scoped to the active
// session when one is set (clears only this session's entries — safe under
// parallel load); otherwise clears everything (legacy global reset).
void reset();

// Read every journal entry in arrival order. Filtered helpers below are
// preferred for assertions.
std::vector<JournalEntry> journal();

// Inbound (SDK→server) journal entries, optionally filtered by method.
// "method" filter compares against the JSON-RPC method string (e.g.
// "calling.play", "signalwire.connect").
std::vector<JournalEntry> journal_recv(const std::string& method = "");

// Outbound (server→SDK) journal entries, optionally filtered by inner
// event_type (only meaningful for signalwire.event frames).
std::vector<JournalEntry> journal_send(const std::string& event_type = "");

// Most recent journal entry (any direction). Throws if journal is empty.
JournalEntry journal_last();

// Most recent inbound entry whose method matches. Throws if not found.
JournalEntry journal_last_recv(const std::string& method);

// Queue scripted post-RPC events for `method` (FIFO consume-once). Each
// event is a JSON object like
//   {"emit": {"state": "playing"}, "delay_ms": 5}
// The mock derives the default event_type from the method (calling.play →
// calling.call.play). Pass "event_type" inside the object to override.
void arm_method(const std::string& method, const json& events);

// Queue a dial-dance scenario.
void arm_dial(const json& body);

// Push a single frame to one or all sessions. "frame" is the full
// JSON-RPC envelope. "session_id" empty means broadcast.
json push(const json& frame, const std::string& session_id = "");

// Run a scripted timeline (sleep / push / expect_recv).
json scenario_play(const json& ops);

// Inject an inbound call announcement.
struct InboundCallOpts {
    std::string call_id;
    std::string from_number = "+15551234567";
    std::string to_number = "+15559876543";
    std::string context = "default";
    std::vector<std::string> auto_states;
    int delay_ms = 50;
    std::string session_id;
};
json inbound_call(const InboundCallOpts& opts);

// List active WS sessions reported by the mock.
std::vector<json> sessions();

// Build a RELAY config pointed at the mock with project="test_proj" /
// token="test_tok". Matches the Python signalwire_relay_client fixture.
RelayConfig make_config(const std::string& project = "test_proj",
                        const std::string& token = "test_tok");

// Connect a real RelayClient to the mock and return it. Resets the
// journal first. The caller owns the lifetime; call client.disconnect()
// before destroying. The returned pointer is non-null on success.
std::unique_ptr<RelayClient> make_client(
    const std::string& project = "test_proj",
    const std::string& token = "test_tok",
    const std::vector<std::string>& contexts = {"default"});

// Same as make_client(), but lets the caller mutate the RelayConfig (e.g. set a
// small max_active_calls for the MAP-BOUNDS cap test) before connect(). All the
// connect + session-scoping wiring is identical to make_client().
std::unique_ptr<RelayClient> make_client_with_config(
    const std::function<void(RelayConfig&)>& mutate,
    const std::vector<std::string>& contexts = {"default"});

// Wait until at least one session is established on the mock (or timeout).
// Used by tests that issue HTTP-side pushes immediately after connecting.
bool wait_for_session(int timeout_ms = 2000);

// Convenience: drive an inbound call sequence and synchronously wait for
// the SDK's on_call handler to register the Call into the registry. The
// handler may answer/play/etc. before this returns. Returns the Call*
// the SDK created (still owned by the client). Returns nullptr on timeout.
Call* drive_inbound_call(RelayClient& client,
                         const std::string& call_id,
                         const std::vector<std::string>& auto_states = {"created"},
                         int timeout_ms = 5000);

} // namespace mocktest
} // namespace relay
} // namespace signalwire
