// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#pragma once

#include <optional>
#include <string>

namespace signalwire {
namespace relay {

// ===========================================================================
// Typed RELAY lifecycle-state enums (Tier-3 idiom pass).
//
// The Python reference + this port model call / dial / message lifecycle as
// bare-string constants (`CALL_STATE_*`, `dial_state`, `MESSAGE_STATE_*` in
// `relay/constants.py` / `relay/constants.hpp`) with stringly predicates
// (`is_answered`, `is_ended`, `is_delivered`, …). These are KNOWABLE closed
// sets, so a statically-typed port should expose them as `enum class`
// ALONGSIDE the existing string accessors (parity), per the floor-not-ceiling
// rule.
//
// ★ THREE SEPARATE VOCABULARIES that must NEVER be conflated — each mirrors a
// distinct field the server emits:
//   * CallState    — `call_state` on calling.call.state events
//                    {created, ringing, answered, ending, ended}
//   * DialState     — `dial_state` on the calling.call.dial event
//                    {dialing, answered, failed}  (RELAY_IMPLEMENTATION_GUIDE.md
//                    line 193: "dialing (progress), answered (success),
//                    failed (all legs failed)"). NOTE the dial OUTCOME set is
//                    distinct from CallState — `failed` is dial-only and there
//                    is no dial `created`/`ringing`/`ending`.
//   * MessageState  — `message_state` on messaging.state events
//                    {queued, initiated, sent, delivered, undelivered, failed,
//                     received}  (relay/constants.py MESSAGE_STATE_*).
//
// These mirror SERVER-emitted values that can GROW, so `*_from_string` returns
// `std::optional<E>` (empty on an unknown value — never throws); the typed
// accessors below stay usable even when the server introduces a new state.
// `*_value()` / `to_string` are the single normalization point: the enum and
// the bare string round-trip to the IDENTICAL wire string.
//
// `is_terminal()` follows the reference's own definition of "terminal"
// (ended/failed — the states after which no further transitions occur):
//   * CallState::Ended
//   * DialState::Answered | DialState::Failed (the dial RPC resolves on either)
//   * MessageState::{Delivered, Undelivered, Failed}  (== MESSAGE_TERMINAL_STATES)
//
// `[[nodiscard]]` on the value-returning free functions (consistent with the
// Tier-2 pass): a stray `call_state_value(x);` that drops the result is a bug.
// ===========================================================================

// ---------------------------------------------------------------------------
// CallState — calling.call.state `call_state`
// ---------------------------------------------------------------------------

/// Call lifecycle state. Mirrors `CALL_STATE_*` / `CALL_STATES` in
/// `relay/constants.py`. Server-emitted and may grow → `call_state_from_string`
/// returns an optional on an unknown value.
enum class CallState {
  Created,
  Ringing,
  Answered,
  Ending,
  Ended,
};

[[nodiscard]] inline std::string call_state_value(CallState v) {
  switch (v) {
    case CallState::Created:
      return "created";
    case CallState::Ringing:
      return "ringing";
    case CallState::Answered:
      return "answered";
    case CallState::Ending:
      return "ending";
    case CallState::Ended:
      return "ended";
  }
  return "";  // unreachable for a valid enumerator
}

/// `to_string` ADL overload — same wire string as `call_state_value`.
[[nodiscard]] inline std::string to_string(CallState v) { return call_state_value(v); }

/// Parse a wire string into a `CallState`. Returns `std::nullopt` for any
/// value not in the known set (server may introduce new states) — NEVER throws.
[[nodiscard]] inline std::optional<CallState> call_state_from_string(const std::string& s) {
  if (s == "created") { return CallState::Created;
}
  if (s == "ringing") { return CallState::Ringing;
}
  if (s == "answered") { return CallState::Answered;
}
  if (s == "ending") { return CallState::Ending;
}
  if (s == "ended") { return CallState::Ended;
}
  return std::nullopt;
}

/// Terminal == no further transitions. For a call that is `ended`.
[[nodiscard]] inline bool is_terminal(CallState v) { return v == CallState::Ended; }

// ---------------------------------------------------------------------------
// DialState — calling.call.dial `dial_state` (the dial OUTCOME, not call state)
// ---------------------------------------------------------------------------

/// Outbound-dial outcome state on the `calling.call.dial` event's `dial_state`
/// field. Grounded in RELAY_IMPLEMENTATION_GUIDE.md line 193:
/// `dialing` (progress), `answered` (success), `failed` (all legs failed).
/// DISTINCT from `CallState` — `failed` is dial-only; the dial RPC resolves on
/// `answered`/`failed`. Server-emitted → `dial_state_from_string` is optional.
enum class DialState {
  Dialing,
  Answered,
  Failed,
};

[[nodiscard]] inline std::string dial_state_value(DialState v) {
  switch (v) {
    case DialState::Dialing:
      return "dialing";
    case DialState::Answered:
      return "answered";
    case DialState::Failed:
      return "failed";
  }
  return "";
}

[[nodiscard]] inline std::string to_string(DialState v) { return dial_state_value(v); }

[[nodiscard]] inline std::optional<DialState> dial_state_from_string(const std::string& s) {
  if (s == "dialing") { return DialState::Dialing;
}
  if (s == "answered") { return DialState::Answered;
}
  if (s == "failed") { return DialState::Failed;
}
  return std::nullopt;
}

/// Terminal == the dial RPC resolves: success (`answered`) or failure (`failed`).
/// `dialing` is the in-progress (non-terminal) state.
[[nodiscard]] inline bool is_terminal(DialState v) {
  return v == DialState::Answered || v == DialState::Failed;
}

// ---------------------------------------------------------------------------
// MessageState — messaging.state `message_state`
// ---------------------------------------------------------------------------

/// SMS/MMS delivery state. Mirrors `MESSAGE_STATE_*` in `relay/constants.py`.
/// Terminal set == `MESSAGE_TERMINAL_STATES` {delivered, undelivered, failed}.
/// NOTE `failed` here is the MESSAGE failure state — NOT `DialState::Failed`;
/// the two vocabularies are separate and must not be unified. Server-emitted →
/// `message_state_from_string` is optional.
enum class MessageState {
  Queued,
  Initiated,
  Sent,
  Delivered,
  Undelivered,
  Failed,
  Received,
};

[[nodiscard]] inline std::string message_state_value(MessageState v) {
  switch (v) {
    case MessageState::Queued:
      return "queued";
    case MessageState::Initiated:
      return "initiated";
    case MessageState::Sent:
      return "sent";
    case MessageState::Delivered:
      return "delivered";
    case MessageState::Undelivered:
      return "undelivered";
    case MessageState::Failed:
      return "failed";
    case MessageState::Received:
      return "received";
  }
  return "";
}

[[nodiscard]] inline std::string to_string(MessageState v) { return message_state_value(v); }

[[nodiscard]] inline std::optional<MessageState> message_state_from_string(const std::string& s) {
  if (s == "queued") { return MessageState::Queued;
}
  if (s == "initiated") { return MessageState::Initiated;
}
  if (s == "sent") { return MessageState::Sent;
}
  if (s == "delivered") { return MessageState::Delivered;
}
  if (s == "undelivered") { return MessageState::Undelivered;
}
  if (s == "failed") { return MessageState::Failed;
}
  if (s == "received") { return MessageState::Received;
}
  return std::nullopt;
}

/// Terminal == `MESSAGE_TERMINAL_STATES`: delivered, undelivered, or failed.
[[nodiscard]] inline bool is_terminal(MessageState v) {
  return v == MessageState::Delivered || v == MessageState::Undelivered ||
         v == MessageState::Failed;
}

}  // namespace relay
}  // namespace signalwire
