// Copyright (c) 2026 SignalWire
// SPDX-License-Identifier: MIT
#include "signalwire/ai_chat/handoff.hpp"

#include <cctype>
#include <chrono>
#include <future>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

#include "gateway_detail.hpp"
#include "httplib.h"
#include "signalwire/logging.hpp"

namespace signalwire {
namespace ai_chat {

namespace {

std::string strip(const std::string& s) {
  size_t b = 0;
  size_t e = s.size();
  while (b < e && std::isspace(static_cast<unsigned char>(s[b])) != 0) {
    ++b;
  }
  while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1])) != 0) {
    --e;
  }
  return s.substr(b, e - b);
}

std::string default_next_id(const std::string& conversation_id) {
  auto dot = conversation_id.rfind('.');
  if (dot != std::string::npos && dot > 0 && dot + 1 < conversation_id.size()) {
    const std::string tail = conversation_id.substr(dot + 1);
    bool digits = true;
    for (unsigned char c : tail) {
      if (c < '0' || c > '9') {
        digits = false;
        break;
      }
    }
    if (digits && tail.size() < 18) {
      return conversation_id.substr(0, dot) + "." + std::to_string(std::stoll(tail) + 1);
    }
  }
  return conversation_id + ".1";
}

}  // namespace

HandoffRouter::HandoffRouter(ChatGateway& gateway, const HandoffRouterOptions& options)
    : gateway_(gateway),
      options_(options),
      nonces_(options.registry ? options.registry
                               : std::make_shared<std::map<std::string, NonceEntry>>()) {}

NextConversationId HandoffRouter::next_conversation_id() const {
  if (options_.next_conversation_id) {
    return options_.next_conversation_id;
  }
  return default_next_id;
}

void HandoffRouter::prune_locked() {
  double cutoff = detail::monotonic_seconds() - options_.nonce_ttl;
  for (auto it = nonces_->begin(); it != nonces_->end();) {
    it = (it->second.issued_at < cutoff) ? nonces_->erase(it) : std::next(it);
  }
}

NonceEntry* HandoffRouter::lookup_locked(const std::string& nonce) {
  if (nonce.empty()) {
    return nullptr;
  }
  prune_locked();
  auto it = nonces_->find(nonce);
  if (it == nonces_->end() || it->second.redeemed) {
    return nullptr;
  }
  return &it->second;
}

void HandoffRouter::register_(const std::string& nonce, const std::string& conversation_id,
                              const std::optional<std::string>& call_id) {
  if (nonce.empty()) {
    return;
  }
  std::optional<NonceEntry> existing;
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    prune_locked();
    auto it = nonces_->find(nonce);
    if (it == nonces_->end()) {
      NonceEntry entry;
      entry.conversation_id = conversation_id;
      entry.call_id = call_id;
      entry.issued_at = detail::monotonic_seconds();
      nonces_->emplace(nonce, entry);
    } else {
      existing = it->second;
    }
  }
  if (existing) {
    if (existing->redeemed || existing->conversation_id != conversation_id ||
        existing->call_id != call_id) {
      get_logger().warn(
          "handoff_nonce_already_registered conversation_id=" + existing->conversation_id +
          " redeemed=" + (existing->redeemed ? "true" : "false"));
    }
    return;
  }
  get_logger().info("handoff_nonce_registered conversation_id=" + conversation_id);
}

bool HandoffRouter::capture(const std::string& conversation_id, const std::string& medium) {
  if (!options_.capture_leg) {
    return false;
  }
  // Bounded wait: run the application's capture on its own thread so a slow
  // capture cannot hold the route past capture_timeout. A capture that outlives
  // the wait keeps running; its result is dropped.
  auto promise = std::make_shared<std::promise<bool>>();
  std::future<bool> result = promise->get_future();
  auto capture_leg = options_.capture_leg;
  // The body catches everything; clang-tidy cannot see through the lambda
  // boundary to that, hence the suppression.
  // NOLINTNEXTLINE(bugprone-exception-escape)
  std::thread([promise, capture_leg, conversation_id, medium]() noexcept {
    try {
      promise->set_value(capture_leg(conversation_id, medium));
    } catch (...) {
      try {
        promise->set_exception(std::current_exception());
      } catch (...) {  // NOLINT(bugprone-empty-catch): the promise is already satisfied
      }
    }
  }).detach();
  auto wait = std::chrono::duration<double>(options_.capture_timeout);
  if (result.wait_for(wait) != std::future_status::ready) {
    get_logger().warn("handoff_capture_timeout conversation_id=" + conversation_id + " medium=" +
                      medium + " note=starting the next medium without this leg's record");
    return false;
  }
  try {
    return result.get();
  } catch (const std::exception& e) {
    get_logger().error("handoff_capture_failed conversation_id=" + conversation_id +
                       " error=" + e.what());
  } catch (...) {
    get_logger().error("handoff_capture_failed conversation_id=" + conversation_id);
  }
  return false;
}

std::optional<std::string> HandoffRouter::redeem(const std::string& nonce) {
  NonceEntry entry;
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    NonceEntry* live = lookup_locked(nonce);
    if (live == nullptr) {
      return std::nullopt;
    }
    // Consumed even if what follows fails: a nonce is one attempt.
    live->redeemed = true;
    entry = *live;
  }
  if (entry.call_id && !entry.call_id->empty() && options_.end_call) {
    try {
      options_.end_call(*entry.call_id);
    } catch (const std::exception& e) {
      get_logger().warn(std::string("handoff_end_call_failed error=") + e.what());
    } catch (...) {
      get_logger().warn("handoff_end_call_failed");
    }
  }
  (void)capture(entry.conversation_id, "voice");
  std::string handle;
  try {
    handle = gateway_.mint_handle(next_conversation_id()(entry.conversation_id));
  } catch (const std::exception& e) {
    get_logger().error(std::string("handoff_mint_failed error=") + e.what());
    return std::nullopt;
  }
  get_logger().info("handoff_redeemed conversation_id=" + entry.conversation_id);
  return handle;
}

bool HandoffRouter::escalate(const std::string& handle) {
  std::string conversation_id;
  try {
    conversation_id = gateway_.read_handle(handle);
  } catch (const std::exception&) {
    return false;
  }
  (void)capture(conversation_id, "chat");
  get_logger().info("handoff_escalated conversation_id=" + conversation_id);
  return true;
}

bool HandoffRouter::say(const std::string& nonce, const std::string& text) {
  if (!options_.send_message) {
    return false;
  }
  const std::string cleaned = strip(text);
  if (cleaned.empty() || cleaned.size() > MAX_MESSAGE_BYTES) {
    return false;
  }
  NonceEntry entry;
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    NonceEntry* live = lookup_locked(nonce);
    if (live == nullptr || !live->call_id || live->call_id->empty()) {
      return false;
    }
    if (live->messages >= options_.max_messages_per_call) {
      get_logger().warn("handoff_say_cap_reached call_id=" + *live->call_id);
      return false;
    }
    // Take the message's slot before delivering, so overlapping requests can't
    // all pass the cap.
    ++live->messages;
    entry = *live;
  }
  bool delivered = false;
  try {
    // The return value is not a delivery verdict; only an exception is.
    (void)options_.send_message(*entry.call_id, cleaned);
    delivered = true;
  } catch (const std::exception& e) {
    get_logger().error(std::string("handoff_say_failed error=") + e.what());
  } catch (...) {
    get_logger().error("handoff_say_failed");
  }
  if (!delivered) {
    // Not delivered: give the slot back, if the table still holds this
    // registration.
    const std::lock_guard<std::mutex> lock(mutex_);
    auto it = nonces_->find(nonce);
    if (it != nonces_->end() && it->second.messages > 0 &&
        it->second.conversation_id == entry.conversation_id &&
        it->second.call_id == entry.call_id && it->second.issued_at == entry.issued_at) {
      --it->second.messages;
    }
    return false;
  }
  return true;
}

server::Router HandoffRouter::router() {
  return [this](httplib::Server& server, const std::string& prefix) {
    // 403 for a disallowed Origin, else nullopt (handled).
    auto forbidden_origin = [this](const httplib::Request& req, httplib::Response& res) {
      try {
        gateway_.check_origin(detail::header_value(req, "Origin"));
      } catch (const std::exception&) {
        detail::send_json(res, 403, json{{"error", "origin not allowed"}});
        return true;
      }
      return false;
    };
    // The JSON object sent, or {} for anything else. Throws
    // GatewayRejection(413) for a body over the size limit.
    auto read_body = [](const httplib::Request& req) {
      json data;
      try {
        data = detail::read_json_body(req, MAX_REQUEST_BODY_BYTES);
      } catch (const GatewayRejection&) {
        throw;
      } catch (const std::exception&) {
        return json::object();
      }
      return data.is_object() ? data : json::object();
    };

    server.Post(prefix + "/handoff", [this, forbidden_origin, read_body](
                                         const httplib::Request& req, httplib::Response& res) {
      if (forbidden_origin(req, res)) {
        return;
      }
      json data;
      try {
        data = read_body(req);
      } catch (const GatewayRejection& rej) {
        detail::send_json(res, rej.status(), json{{"error", rej.reason()}});
        return;
      }
      if (!data.contains("nonce") || !data["nonce"].is_string()) {
        detail::send_json(res, 404, json{{"error", "not found"}});
        return;
      }
      auto handle = redeem(data["nonce"].get<std::string>());
      if (!handle) {
        // Same answer for unknown, expired and already-redeemed.
        detail::send_json(res, 404, json{{"error", "not found"}});
        return;
      }
      detail::send_json(res, 200, json{{"handle", *handle}});
    });

    server.Post(prefix + "/escalate", [this, forbidden_origin, read_body](
                                          const httplib::Request& req, httplib::Response& res) {
      if (forbidden_origin(req, res)) {
        return;
      }
      json data;
      try {
        data = read_body(req);
      } catch (const GatewayRejection& rej) {
        detail::send_json(res, rej.status(), json{{"error", rej.reason()}});
        return;
      }
      if (!data.contains("handle") || !data["handle"].is_string() ||
          data["handle"].get_ref<const std::string&>().empty()) {
        detail::send_json(res, 400, json{{"error", "bad request"}});
        return;
      }
      if (!escalate(data["handle"].get<std::string>())) {
        detail::send_json(res, 404, json{{"error", "not found"}});
        return;
      }
      detail::send_json(res, 200, json{{"ok", true}});
    });

    server.Post(prefix + "/say", [this, forbidden_origin, read_body](const httplib::Request& req,
                                                                     httplib::Response& res) {
      if (forbidden_origin(req, res)) {
        return;
      }
      json data;
      try {
        data = read_body(req);
      } catch (const GatewayRejection& rej) {
        detail::send_json(res, rej.status(), json{{"error", rej.reason()}});
        return;
      }
      const json nonce = data.contains("nonce") ? data["nonce"] : json();
      const json text = data.contains("text") ? data["text"] : json("");
      if (!nonce.is_string() || !text.is_string()) {
        detail::send_json(res, 404, json{{"error", "not found"}});
        return;
      }
      if (text.get_ref<const std::string&>().size() > MAX_MESSAGE_BYTES) {
        detail::send_json(res, 413, json{{"error", "message too large"}});
        return;
      }
      if (!say(nonce.get<std::string>(), text.get<std::string>())) {
        detail::send_json(res, 404, json{{"error", "not found"}});
        return;
      }
      detail::send_json(res, 200, json{{"ok", true}});
    });
  };
}

}  // namespace ai_chat
}  // namespace signalwire
