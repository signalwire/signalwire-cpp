// Copyright (c) 2026 SignalWire
// SPDX-License-Identifier: MIT
//
// Moving one conversation between voice and text.
//
// ChatGateway lets a browser hold a text conversation. HandoffRouter is the other
// half a browser client needs: the three routes it calls to move that
// conversation to a phone call and back (``/handoff``, ``/escalate``) and to type
// into a live call (``/say``). The SignalWire address widget calls them against
// the same URL that points at a ChatGateway, so mount both at the same prefix.
//
// This class owns the wire contract only -- the routes, the nonce, the ordering
// guarantee and the spend guards. What a conversation IS (where a leg's
// transcript is written, what a resumed greeting says) is the application's,
// injected as callbacks.
//
// THE NONCE. A browser cannot be trusted to name a call, so it proves which call
// it is on: the application puts a random ``handoff_nonce`` in one dial's user
// variables, registers it here against that call's ids, and the browser
// presents it later. The first registration stands; redemption is single use; an
// unknown nonce is answered exactly like an expired or redeemed one. Typing is
// repeatable, bounded by max_messages_per_call, until the nonce is redeemed or
// nonce_ttl passes.
//
// THE ORDERING GUARANTEE. A medium never starts until the one it replaces has
// finished and its record is durable: ``/handoff`` ends the call and waits for
// capture_leg before minting a handle; ``/escalate`` ends the chat leg and waits
// before returning.
//
// The nonce table lives in this process; a redemption must reach the replica
// that served the dial. Registration, redemption and the typing count are
// atomic within one router.
#pragma once

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "signalwire/ai_chat/gateway.hpp"
#include "signalwire/server/router.hpp"

namespace signalwire {
namespace ai_chat {

/// Seconds a nonce stays usable after its first registration.
constexpr int DEFAULT_NONCE_TTL = 3600;
/// Ceiling on typed messages for one call.
constexpr int DEFAULT_MAX_MESSAGES_PER_CALL = 200;
/// Seconds to wait for capture_leg.
constexpr double DEFAULT_CAPTURE_TIMEOUT = 8.0;

/// ``capture_leg(conversation_id, medium)``: end a leg and write its record;
/// return true once that record is durable.
using CaptureLeg =
    std::function<bool(const std::string& conversation_id, const std::string& medium)>;
/// ``end_call(call_id)``: hang the call up server-side.
using EndCall = std::function<void(const std::string& call_id)>;
/// ``send_message(call_id, text)``: inject typed text into the live call.
using SendMessage = std::function<bool(const std::string& call_id, const std::string& text)>;
/// ``next_conversation_id(conversation_id)``: the id for the NEW leg.
using NextConversationId = std::function<std::string(const std::string& conversation_id)>;

/// What a nonce is a capability for. ``redeemed`` marks a nonce ``/handoff`` has
/// exchanged for a handle; the entry is kept until its TTL passes, so the nonce
/// can be neither redeemed nor registered again.
struct NonceEntry {
  std::string conversation_id;
  std::optional<std::string> call_id;
  /// Monotonic seconds at registration.
  double issued_at = 0.0;
  /// Typed messages delivered so far.
  int messages = 0;
  bool redeemed = false;
};

/// Construction options for HandoffRouter.
struct HandoffRouterOptions {
  /// ``capture_leg(conversation_id, medium)``: end a leg and write its record;
  /// return true once that record is durable. Unset: no wait happens, and the
  /// ordering guarantee is not provided.
  CaptureLeg capture_leg;
  /// ``end_call(call_id)``: hang the call up server-side.
  EndCall end_call;
  /// ``send_message(call_id, text)``: inject typed text into the live call.
  /// Unset: typing is disabled (``/say`` answers 404).
  SendMessage send_message;
  /// Produces the id for the NEW leg; defaults to appending ``.N``.
  NextConversationId next_conversation_id;
  int nonce_ttl = DEFAULT_NONCE_TTL;
  int max_messages_per_call = DEFAULT_MAX_MESSAGES_PER_CALL;
  /// Seconds to wait for capture_leg -- a ceiling, not a budget.
  double capture_timeout = DEFAULT_CAPTURE_TIMEOUT;
  /// Optional nonce table shared between routers (one process). Null: the
  /// router keeps its own. Each router serializes its own steps only.
  std::shared_ptr<std::map<std::string, NonceEntry>> registry;
};

/// The three routes a browser client needs beside a ChatGateway.
class HandoffRouter {
 public:
  /// ``gateway`` owns the conversations (mints/reads handles, checks origins)
  /// and must outlive this router.
  explicit HandoffRouter(ChatGateway& gateway, const HandoffRouterOptions& options = {});

  /// The gateway that owns the conversations.
  [[nodiscard]] ChatGateway& gateway() { return gateway_; }
  /// The application's capture callback (may be empty).
  [[nodiscard]] const CaptureLeg& capture_leg() const { return options_.capture_leg; }
  /// The application's hang-up callback (may be empty).
  [[nodiscard]] const EndCall& end_call() const { return options_.end_call; }
  /// The application's typed-text delivery callback (may be empty).
  [[nodiscard]] const SendMessage& send_message() const { return options_.send_message; }
  /// Seconds a nonce stays usable after its first registration.
  [[nodiscard]] int nonce_ttl() const { return options_.nonce_ttl; }
  /// Ceiling on typed messages for one call.
  [[nodiscard]] int max_messages_per_call() const { return options_.max_messages_per_call; }
  /// Seconds to wait for capture_leg.
  [[nodiscard]] double capture_timeout() const { return options_.capture_timeout; }
  /// Produces the id for a NEW leg: the configured next_conversation_id, else
  /// the default ``root`` -> ``root.1``; ``root.2`` -> ``root.3``.
  [[nodiscard]] NextConversationId next_conversation_id() const;

  /// Record what a nonce is a capability for. Call from the per-call config
  /// callback of the dial that carried the nonce, reading ``call_id`` from the
  /// request the platform sent. The first registration stands until its TTL
  /// passes.
  /// (``register`` is a C++ keyword, hence the trailing underscore.)
  void register_(const std::string& nonce, const std::string& conversation_id,
                 const std::optional<std::string>& call_id = std::nullopt);

  /// Exchange a nonce for a chat handle (single use): ends the call, waits for
  /// its record, then mints a handle for a new leg. nullopt for an unknown,
  /// expired or already-redeemed nonce, deliberately indistinguishable.
  [[nodiscard]] std::optional<std::string> redeem(const std::string& nonce);

  /// End a chat leg and wait for its record, before a call is placed. False
  /// for a handle the gateway does not accept.
  [[nodiscard]] bool escalate(const std::string& handle);

  /// Deliver typed text into the live call the nonce names (does NOT consume
  /// the nonce). False when typing is disabled, the text is empty or over
  /// MAX_MESSAGE_BYTES, the nonce is not live, or the cap is reached.
  [[nodiscard]] bool say(const std::string& nonce, const std::string& text);

  /// The routes ``POST {prefix}/handoff``, ``{prefix}/escalate`` and
  /// ``{prefix}/say``. Mount at the SAME prefix as the gateway's. The router
  /// must outlive the server the routes are mounted on.
  [[nodiscard]] server::Router router();

 private:
  void prune_locked();
  NonceEntry* lookup_locked(const std::string& nonce);
  bool capture(const std::string& conversation_id, const std::string& medium);

  ChatGateway& gateway_;
  HandoffRouterOptions options_;
  std::mutex mutex_;
  std::shared_ptr<std::map<std::string, NonceEntry>> nonces_;
};

}  // namespace ai_chat
}  // namespace signalwire
