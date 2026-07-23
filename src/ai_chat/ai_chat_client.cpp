// Copyright (c) 2026 SignalWire
// SPDX-License-Identifier: MIT
#include "signalwire/ai_chat/ai_chat_client.hpp"

#include <chrono>

#include "httplib.h"
#include "signalwire/common.hpp"

namespace signalwire {
namespace ai_chat {

namespace {

constexpr const char* kDefaultPath = "/api/ai/chat";
constexpr const char* kUserAgent = "signalwire-cpp-ai-chat";

// JSON-RPC error code -> the typed error this port raises. An unmapped code
// falls to the base AIChatError. Mirrors python's _ERROR_BY_CODE.
[[noreturn]] void raise_for_code(int code, const std::string& message) {
  switch (code) {
    case -32001:
      throw ConversationNotFoundError(code, message);
    case -32005:
    case -32006:
      throw RateLimitError(code, message);
    case -32007:
      throw ChatInProgressError(code, message);
    case -32009:
      throw AuthenticationError(code, message);
    default:
      throw AIChatError(code, message);
  }
}

// Split a full URL into "scheme://host[:port]" and the remaining path. The
// httplib::Client is constructed from the scheme+host; the path is passed to
// Post(). A URL with no path component yields kDefaultPath.
void split_url(const std::string& url, std::string& host_out, std::string& path_out) {
  std::string scheme = "http";
  std::string rest = url;
  auto pos = rest.find("://");
  if (pos != std::string::npos) {
    scheme = rest.substr(0, pos);
    rest = rest.substr(pos + 3);
  }
  auto slash = rest.find('/');
  if (slash == std::string::npos) {
    host_out = scheme + "://" + rest;
    path_out = kDefaultPath;
  } else {
    host_out = scheme + "://" + rest.substr(0, slash);
    path_out = rest.substr(slash);
  }
}

}  // namespace

std::string AIChatClient::resolve_url(const std::string& url, const std::string& space) {
  if (!url.empty()) {
    return url;
  }
  // RAILS_DEV_MODE doubles as the service's persona switch: plain booleans mean
  // "on" without carrying a URL, so only a real URL-looking value overrides the
  // target here (mirrors the python reference).
  std::string dev_url = signalwire::get_env("RAILS_DEV_MODE");
  // trim surrounding whitespace
  auto b = dev_url.find_first_not_of(" \t\r\n");
  auto e = dev_url.find_last_not_of(" \t\r\n");
  dev_url = (b == std::string::npos) ? "" : dev_url.substr(b, e - b + 1);
  if (!dev_url.empty()) {
    std::string lower = dev_url;
    for (auto& c : lower) {
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    static const std::set<std::string> kBooleanish = {"false", "0", "no",  "off",
                                                      "true",  "1", "yes", "on"};
    if (kBooleanish.count(lower) == 0) {
      return dev_url;
    }
  }
  if (!space.empty()) {
    return "https://" + space + ".signalwire.com" + kDefaultPath;
  }
  throw std::invalid_argument(
      "No service URL: provide url=, set RAILS_DEV_MODE to a full URL, or provide "
      "space= / SIGNALWIRE_SPACE.");
}

AIChatClient::AIChatClient(const AIChatClientOptions& options)
    : read_idle_timeout_seconds_(options.read_idle_timeout_seconds),
      connect_timeout_seconds_(options.connect_timeout_seconds) {
  std::string project =
      !options.project.empty() ? options.project : signalwire::get_env("SIGNALWIRE_PROJECT_ID");
  std::string token =
      !options.token.empty() ? options.token : signalwire::get_env("SIGNALWIRE_API_TOKEN");
  std::string space =
      !options.space.empty() ? options.space : signalwire::get_env("SIGNALWIRE_SPACE");

  if (project.empty()) {
    throw std::invalid_argument(
        "project is required. Provide it as an option or set the "
        "SIGNALWIRE_PROJECT_ID environment variable.");
  }

  url_ = resolve_url(options.url, space);
  split_url(url_, host_, path_);
  auth_header_ = "Basic " + signalwire::base64_encode(project + ":" + token);
  user_agent_ = kUserAgent;
}

AIChatClient::~AIChatClient() = default;

// cpp-httplib is stateless (per-request Client), so there is nothing to
// release -- a well-defined no-op mirroring the python reference's
// close() (and the TS no-op close()). Kept explicit so the reference's
// context-manager-exit / explicit-release shape maps 1:1.
void AIChatClient::close() {}

json AIChatClient::request(const std::string& method, const json& params) {
  ++request_counter_;
  json payload = {
      {"jsonrpc", "2.0"},
      {"method", method},
      {"params", params},
      {"id", "req-" + std::to_string(request_counter_)},
  };

  httplib::Client cli(host_);
  // Byte-driven liveness (python sock_read=60): set_read_timeout is a PER-READ
  // idle timeout — each streamed keepalive-whitespace read resets it — so a
  // slow-but-live turn is never severed, only a truly dead connection is. There
  // is deliberately NO overall/write cap a heartbeat couldn't reset. The
  // connect is bounded (python connect=10). A read_idle of 0 disables the read
  // cap entirely.
  cli.set_connection_timeout(connect_timeout_seconds_, 0);
  if (read_idle_timeout_seconds_ > 0) {
    cli.set_read_timeout(read_idle_timeout_seconds_, 0);
  }
  cli.set_keep_alive(false);

  httplib::Headers headers = {
      {"Authorization", auth_header_},
      {"Content-Type", "application/json"},
      {"Accept", "application/json"},
      {"User-Agent", user_agent_},
  };

  httplib::Result res = cli.Post(path_, headers, payload.dump(), "application/json");
  if (!res) {
    throw AIChatError(0, "transport failure: no response from " + host_);
  }

  // Success/failure is decided by the JSON-RPC BODY, NOT the HTTP status: the
  // keepalive heartbeat commits 200 before the turn's outcome is known, so a
  // slow error arrives as 200 + {"error": …}. Never gate on res->status here.
  // Leading keepalive whitespace is valid JSON, so a plain parse handles it.
  json body = json::parse(res->body, nullptr, false);
  if (body.is_discarded() || !body.is_object()) {
    throw AIChatError(res->status, "non-JSON response (HTTP " + std::to_string(res->status) + ")");
  }

  if (body.contains("error") && !body["error"].is_null()) {
    const json& error = body["error"];
    std::string message = error.value("message", "");
    if (error.contains("code") && error["code"].is_number_integer()) {
      raise_for_code(error["code"].get<int>(), message);
    }
    throw AIChatError(message);
  }

  if (body.contains("result") && body["result"].is_object()) {
    return body["result"];
  }
  return json::object();
}

ConversationInfo AIChatClient::create_conversation(const std::string& conversation_id,
                                                   const CreateConversationOptions& options) {
  json params = {{"id", conversation_id}, {"config_url", options.config_url}};
  if (!options.user_message.empty()) {
    params["user_message"] = options.user_message;
  }
  if (options.timeout) {
    params["conversation_timeout"] = options.timeout;
  }
  if (!options.user_metadata.is_null()) {
    params["user_meta_data"] = options.user_metadata;
  }
  if (options.reinit) {
    params["reinit"] = true;
  }

  json result = request("create_conversation", params);
  ConversationInfo info;
  info.id = conversation_id;
  info.status = result.value("status", "created");
  if (result.contains("initial_message") && result["initial_message"].is_string()) {
    info.has_initial_message = true;
    info.initial_message = result["initial_message"].get<std::string>();
  }
  return info;
}

ChatResponse AIChatClient::chat(const std::string& conversation_id, const std::string& message,
                                const ChatOptions& options) {
  json params = {
      {"id", conversation_id},
      {"message", message},
      {"role", options.role},
  };
  if (!options.config_url.empty()) {
    params["config_url"] = options.config_url;
  }
  if (!options.user_metadata.is_null()) {
    params["user_meta_data"] = options.user_metadata;
  }
  if (options.timeout) {
    params["conversation_timeout"] = options.timeout;
  }
  if (options.reinit) {
    params["reinit"] = true;
  }

  json result = request("chat", params);
  ChatResponse reply;
  reply.text = result.value("response", "");
  reply.conversation_id = conversation_id;
  if (result.contains("user_event") && result["user_event"].is_object()) {
    reply.user_event = result["user_event"];
  }
  return reply;
}

bool AIChatClient::end(const std::string& conversation_id) {
  json result = request("end_conversation", {{"id", conversation_id}});
  return result.value("status", "") == "ended";
}

bool AIChatClient::del(const std::string& conversation_id) {
  json result = request("delete", {{"id", conversation_id}});
  return result.value("status", "") == "deleted";
}

ChatLog AIChatClient::log(const std::string& conversation_id) {
  json result = request("chat_log", {{"id", conversation_id}});
  ChatLog out;
  if (result.contains("chat_log") && result["chat_log"].is_array()) {
    for (const auto& m : result["chat_log"]) {
      out.messages.push_back(m);
    }
  }
  if (result.contains("call_timeline") && result["call_timeline"].is_array()) {
    for (const auto& t : result["call_timeline"]) {
      out.call_timeline.push_back(t);
    }
  }
  return out;
}

std::string AIChatClient::summarize(const std::string& conversation_id,
                                    const SummarizeOptions& options) {
  json params = {{"id", conversation_id}};
  if (!options.summary_prompt.empty()) {
    params["summary_prompt"] = options.summary_prompt;
  }
  if (options.has_temperature) {
    params["temperature"] = options.temperature;
  }
  if (options.has_top_p) {
    params["top_p"] = options.top_p;
  }
  if (options.has_frequency_penalty) {
    params["frequency_penalty"] = options.frequency_penalty;
  }
  if (options.has_presence_penalty) {
    params["presence_penalty"] = options.presence_penalty;
  }
  if (options.has_max_tokens) {
    params["max_tokens"] = options.max_tokens;
  }

  json result = request("summarize", params);
  // summarize returns EXACTLY ONE of {summary} or {error} — both on the SUCCESS
  // envelope. A failed generation must surface as a typed failure, never an
  // empty string. summary wins when both are somehow present (mirrors python).
  if (result.contains("error") && !result.contains("summary")) {
    std::string msg =
        result["error"].is_string() ? result["error"].get<std::string>() : result["error"].dump();
    throw SummaryError(msg);
  }
  if (result.contains("summary")) {
    if (result["summary"].is_string()) {
      return result["summary"].get<std::string>();
    }
    return result["summary"].dump();
  }
  return "";
}

}  // namespace ai_chat
}  // namespace signalwire
