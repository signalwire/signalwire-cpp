// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#pragma once

#include <string>

namespace signalwire {
namespace relay {

// ========================================================================
// Connection
// ========================================================================

constexpr const char* DEFAULT_HOST = "relay.signalwire.com";
constexpr int DEFAULT_PORT = 443;
constexpr const char* PROTOCOL_PREFIX = "signalwire";

// ========================================================================
// Call states
// ========================================================================

constexpr const char* CALL_STATE_CREATED   = "created";
constexpr const char* CALL_STATE_RINGING   = "ringing";
constexpr const char* CALL_STATE_ANSWERED  = "answered";
constexpr const char* CALL_STATE_ENDING    = "ending";
constexpr const char* CALL_STATE_ENDED     = "ended";

// ========================================================================
// Call connect states
// ========================================================================

constexpr const char* CONNECT_STATE_CONNECTING = "connecting";
constexpr const char* CONNECT_STATE_CONNECTED  = "connected";
constexpr const char* CONNECT_STATE_FAILED     = "failed";
constexpr const char* CONNECT_STATE_DISCONNECTED = "disconnected";

// ========================================================================
// Play / Record / Collect states
// ========================================================================

constexpr const char* COMPONENT_STATE_PLAYING   = "playing";
constexpr const char* COMPONENT_STATE_FINISHED  = "finished";
constexpr const char* COMPONENT_STATE_ERROR     = "error";
constexpr const char* COMPONENT_STATE_RECORDING = "recording";
constexpr const char* COMPONENT_STATE_NO_INPUT  = "no_input";

// ========================================================================
// Event types
// ========================================================================

constexpr const char* EVENT_CALL_RECEIVED = "calling.call.received";
constexpr const char* EVENT_CALL_STATE    = "calling.call.state";
constexpr const char* EVENT_CALL_PLAY     = "calling.call.play";
constexpr const char* EVENT_CALL_RECORD   = "calling.call.record";
constexpr const char* EVENT_CALL_COLLECT  = "calling.call.collect";
constexpr const char* EVENT_CALL_TAP      = "calling.call.tap";
constexpr const char* EVENT_CALL_DETECT   = "calling.call.detect";
constexpr const char* EVENT_CALL_CONNECT  = "calling.call.connect";
constexpr const char* EVENT_CALL_FAX      = "calling.call.fax";
constexpr const char* EVENT_CALL_SEND_DIGITS = "calling.call.send_digits";
constexpr const char* EVENT_MESSAGING_RECEIVE = "messaging.receive";
constexpr const char* EVENT_MESSAGING_STATE   = "messaging.state";

// ========================================================================
// Reconnection
// ========================================================================

constexpr int RECONNECT_BASE_DELAY_MS = 1000;
constexpr int RECONNECT_MAX_DELAY_MS  = 30000;
constexpr double RECONNECT_BACKOFF_FACTOR = 2.0;
constexpr int MAX_RECONNECT_ATTEMPTS = 50;

// ========================================================================
// Concurrency
// ========================================================================

constexpr int DEFAULT_MAX_ACTIVE_CALLS = 1000;
constexpr int DEFAULT_MAX_CONNECTIONS  = 1;

} // namespace relay
} // namespace signalwire
