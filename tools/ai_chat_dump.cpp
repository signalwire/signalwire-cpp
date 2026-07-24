// Copyright (c) 2026 SignalWire
// SPDX-License-Identifier: MIT
//
// ai_chat_dump.cpp — the C++ port's AI-CHAT dump program for the cross-port
// wire-behavioral gate (porting-sdk/scripts/diff_port_ai_chat.py, on the
// `ai-chat-client` branch — a COORDINATED pass).
//
// The gate boots the in-process mock_ai_chat server, exports MOCK_AI_CHAT_URL +
// SIGNALWIRE_PROJECT_ID / SIGNALWIRE_API_TOKEN into this program's env, runs the
// built binary, and asserts the JSON it prints (+ the wire requests the mock
// recorded) speak the AI Chat protocol per the vendored spec
// (ai-chat-specs/ai-chat.yaml).
//
// This mirrors porting-sdk/scripts/ai_chat_dump_reference.py EXACTLY: it drives
// the C++ AIChatClient through the shared ai_chat_corpus and emits ONE JSON
// object to stdout (nothing else), keyed by corpus step:
//
//   success steps (create/chat/end/delete/log/summarize):
//       { wire_method, decoded: { <spec result fields> } }
//   summarize_failed (the summarize {error} one_of branch — must SURFACE):
//       { wire_method:"summarize", raised:true, error_type, message }
//   error steps (err_notfound/err_ratelimit/err_inprogress/err_auth/err_unmapped):
//       { raised:true, error_code, error_type }
//
// The corpus (steps + SUMMARIZE_ERROR_ID + ERROR_STEPS + force_error_id) is data,
// identical for every language; it is mirrored inline here from ai_chat_corpus.py.
//
// Run from the signalwire-cpp repo root against a running mock (the built binary):
//     MOCK_AI_CHAT_URL=http://127.0.0.1:PORT/api/ai/chat ./build/ai_chat_dump
//
// Nothing but the JSON object is written to stdout on success.

#include <cstdlib>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>
#include <typeinfo>
#include <utility>
#include <vector>

#include "signalwire/ai_chat/ai_chat_client.hpp"

using json = nlohmann::json;
using signalwire::ai_chat::AIChatClient;
using signalwire::ai_chat::AIChatClientOptions;
using signalwire::ai_chat::AIChatError;
using signalwire::ai_chat::ChatLog;
using signalwire::ai_chat::ChatOptions;
using signalwire::ai_chat::ChatResponse;
using signalwire::ai_chat::ConversationInfo;
using signalwire::ai_chat::CreateConversationOptions;
using signalwire::ai_chat::SummarizeOptions;
using signalwire::ai_chat::SummaryError;

namespace {

// ── the shared corpus (mirror of porting-sdk/scripts/ai_chat_corpus.py) ──────

// The sentinel conversation id that makes summarize return its {error} branch.
const std::string kSummarizeErrorId = "__summarize_error";

// (step_id, JSON-RPC code) the port's raised error MUST carry, in corpus order.
const std::vector<std::pair<std::string, int>> kErrorSteps = {
    {"err_notfound", -32001},    // ConversationNotFound
    {"err_ratelimit", -32005},   // RateLimit
    {"err_inprogress", -32007},  // ChatInProgress
    {"err_auth", -32009},        // Authentication
    {"err_unmapped", -32602},    // base AIChatError (unmapped code)
};

std::string force_error_id(int code) { return "__err_" + std::to_string(code); }

// The concrete error subclass name the port raised, matching the python
// dump's ``type(e).__name__`` (typeid().name() is a mangled/compiler-specific
// string, so map explicitly to the stable public class names the gate reads).
std::string error_type_name(const AIChatError& e) {
  if (dynamic_cast<const signalwire::ai_chat::ConversationNotFoundError*>(&e) != nullptr) {
    return "ConversationNotFoundError";
  }
  if (dynamic_cast<const signalwire::ai_chat::RateLimitError*>(&e) != nullptr) {
    return "RateLimitError";
  }
  if (dynamic_cast<const signalwire::ai_chat::ChatInProgressError*>(&e) != nullptr) {
    return "ChatInProgressError";
  }
  if (dynamic_cast<const signalwire::ai_chat::AuthenticationError*>(&e) != nullptr) {
    return "AuthenticationError";
  }
  if (dynamic_cast<const signalwire::ai_chat::SummaryError*>(&e) != nullptr) {
    return "SummaryError";
  }
  return "AIChatError";
}

json run(const std::string& url) {
  json out = json::object();
  AIChatClientOptions opts;
  opts.url = url;
  AIChatClient client(opts);

  // ── success steps ──────────────────────────────────────────────────
  CreateConversationOptions co;
  co.config_url = "http://cfg";
  co.timeout = 30;
  co.reinit = true;
  ConversationInfo info = client.create_conversation("conv-1", co);
  out["create"] = {
      {"wire_method", "create_conversation"},
      {"decoded",
       {{"status", info.status},
        {"id", info.id},
        {"initial_message",
         info.has_initial_message ? json(info.initial_message) : json(nullptr)}}},
  };

  ChatOptions ch;
  ch.timeout = 30;
  ch.reinit = true;
  ChatResponse reply = client.chat("conv-1", "hello", ch);
  out["chat"] = {
      {"wire_method", "chat"},
      {"decoded", {{"response", reply.text}, {"user_event", reply.user_event}}},
  };

  // end/delete return bool idiomatically; the wire result also carries the
  // conversation id (the caller's own input, echoed). Report both the derived
  // status and the id operated on — mirroring the reference dump.
  bool ended = client.end("conv-1");
  out["end"] = {
      {"wire_method", "end_conversation"},
      {"decoded", {{"status", ended ? "ended" : "?"}, {"id", "conv-1"}}},
  };

  bool deleted = client.del("conv-1");
  out["delete"] = {
      {"wire_method", "delete"},
      {"decoded", {{"status", deleted ? "deleted" : "?"}, {"id", "conv-1"}}},
  };

  ChatLog cl = client.log("conv-1");
  out["log"] = {
      {"wire_method", "chat_log"},
      {"decoded", {{"chat_log", cl.messages}, {"call_timeline", cl.call_timeline}}},
  };

  std::string summary = client.summarize("conv-1");
  out["summarize"] = {{"wire_method", "summarize"}, {"decoded", {{"summary", summary}}}};

  // ── summarize one_of {error} branch: must SURFACE, not swallow ───────
  try {
    std::string swallowed = client.summarize(kSummarizeErrorId);
    out["summarize_failed"] = {
        {"wire_method", "summarize"},
        {"raised", false},
        {"decoded", {{"summary", swallowed}}},
    };
  } catch (const SummaryError& e) {
    out["summarize_failed"] = {
        {"wire_method", "summarize"},
        {"raised", true},
        {"error_type", error_type_name(e)},
        {"message", e.server_message()},
    };
  }

  // ── error-code steps (JSON-RPC error object) ─────────────────────────
  for (const auto& [step, code] : kErrorSteps) {
    try {
      client.chat(force_error_id(code), "x");
      out[step] = {{"raised", false}};
    } catch (const AIChatError& e) {
      out[step] = {
          {"raised", true},
          {"error_code", e.has_code() ? json(e.code()) : json(nullptr)},
          {"error_type", error_type_name(e)},
      };
    }
  }

  return out;
}

}  // namespace

int main() {
  const char* url = std::getenv("MOCK_AI_CHAT_URL");
  if (url == nullptr || *url == '\0') {
    std::cerr << "MOCK_AI_CHAT_URL not set\n";
    return 2;
  }
  try {
    json out = run(url);
    std::cout << out.dump() << "\n";
  } catch (const std::exception& e) {
    std::cerr << "ai_chat_dump: " << e.what() << "\n";
    return 1;
  }
  return 0;
}
