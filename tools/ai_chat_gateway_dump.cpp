// Copyright (c) 2026 SignalWire
// SPDX-License-Identifier: MIT
//
// ai_chat_gateway_dump — the AI-CHAT-GATEWAY gate's dump program
// (porting-sdk/scripts/diff_port_ai_chat_gateway.py). The C++ mirror of
// porting-sdk/scripts/ai_chat_gateway_dump_reference.py: builds an AIChatClient
// pointed at MOCK_AI_CHAT_URL, a ChatGateway with the corpus's fixed settings
// and a HandoffRouter whose callbacks record their calls; serves both routers
// under the corpus PREFIX on a loopback ephemeral port; runs the corpus STEPS
// (ai_chat_gateway_corpus.py, mirrored below) in order; and prints ONE JSON
// object mapping step id -> observation.
//
//     MOCK_AI_CHAT_URL=http://127.0.0.1:PORT/api/ai/chat ./build/ai_chat_gateway_dump

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>
#include <vector>

#include "httplib.h"
#include "signalwire/ai_chat/gateway.hpp"
#include "signalwire/ai_chat/handoff.hpp"
#include "signalwire/logging.hpp"

using json = nlohmann::json;
using signalwire::ai_chat::AIChatClient;
using signalwire::ai_chat::AIChatClientOptions;
using signalwire::ai_chat::ChatGateway;
using signalwire::ai_chat::ChatGatewayOptions;
using signalwire::ai_chat::HandoffRouter;
using signalwire::ai_chat::HandoffRouterOptions;

namespace {

// ---- the corpus (porting-sdk/scripts/ai_chat_gateway_corpus.py) -------------
const std::string kPrefix = "/chat";
const std::string kConfigUrl = "https://agent.example.com/swml";
const std::string kKey = "pk_gateway_corpus";
const std::string kSecret = "gateway-corpus-secret";
const std::string kAllowedOrigin = "https://shop.example.com";
const std::string kEscalateConversation = "conv-root.5";
const std::vector<std::string> kCheckedHeaders = {"x-chat-handle", "access-control-allow-origin",
                                                  "access-control-expose-headers"};

std::map<std::string, std::map<std::string, std::string>> header_sets() {
  return {
      {"browser",
       {{"Authorization", "Bearer " + kKey},
        {"Origin", kAllowedOrigin},
        {"Content-Type", "application/json"}}},
      {"bad_key",
       {{"Authorization", "Bearer pk_wrong"},
        {"Origin", kAllowedOrigin},
        {"Content-Type", "application/json"}}},
      {"bad_origin",
       {{"Authorization", "Bearer " + kKey},
        {"Origin", "https://evil.example"},
        {"Content-Type", "application/json"}}},
      {"preflight", {{"Origin", kAllowedOrigin}, {"Access-Control-Request-Method", "POST"}}},
      {"plain", {{"Content-Type", "application/json"}}},
  };
}

json steps() {
  json page = {{"metadata",
                {{"page", {{"url", "https://shop.example.com/pricing"}, {"title", "Pricing"}}}}}};
  return json::array({
      {{"id", "gateway_preflight"},
       {"method", "OPTIONS"},
       {"path", "/"},
       {"headers", "preflight"},
       {"check_headers", true}},
      {{"id", "gateway_start"},
       {"method", "POST"},
       {"path", "/"},
       {"headers", "browser"},
       {"body", {{"method", "start"}, {"user_meta_data", page}}},
       {"check_headers", true}},
      {{"id", "gateway_chat"},
       {"method", "POST"},
       {"path", "/"},
       {"headers", "browser"},
       {"body", {{"message", "hello"}, {"handle", "{handle}"}}},
       {"check_headers", true}},
      {{"id", "gateway_log"},
       {"method", "POST"},
       {"path", "/"},
       {"headers", "browser"},
       {"body", {{"method", "log"}, {"handle", "{handle}"}}}},
      {{"id", "gateway_end"},
       {"method", "POST"},
       {"path", "/"},
       {"headers", "browser"},
       {"body", {{"method", "end"}, {"handle", "{handle}"}}}},
      {{"id", "gateway_bad_key"},
       {"method", "POST"},
       {"path", "/"},
       {"headers", "bad_key"},
       {"body", {{"message", "hi"}}},
       {"check_headers", true}},
      {{"id", "gateway_bad_origin"},
       {"method", "POST"},
       {"path", "/"},
       {"headers", "bad_origin"},
       {"body", {{"message", "hi"}}},
       {"check_headers", true}},
      {{"id", "gateway_forged_handle"},
       {"method", "POST"},
       {"path", "/"},
       {"headers", "browser"},
       {"body", {{"message", "hi"}, {"handle", "{forged_handle}"}}}},
      {{"id", "gateway_method_not_allowed"},
       {"method", "POST"},
       {"path", "/"},
       {"headers", "browser"},
       {"body", {{"method", "summarize"}}}},
      {{"id", "gateway_metadata_not_object"},
       {"method", "POST"},
       {"path", "/"},
       {"headers", "browser"},
       {"body", {{"method", "start"}, {"user_meta_data", json::array({"not", "an", "object"})}}}},
      {{"id", "gateway_end_needs_handle"},
       {"method", "POST"},
       {"path", "/"},
       {"headers", "browser"},
       {"body", {{"method", "end"}}}},
      {{"id", "handoff_redeem"},
       {"method", "POST"},
       {"path", "/handoff"},
       {"headers", "plain"},
       {"setup", {{"register", json::array({"n1", "conv-root", "call-9"})}}},
       {"body", {{"nonce", "n1"}}}},
      {{"id", "handoff_spent"},
       {"method", "POST"},
       {"path", "/handoff"},
       {"headers", "plain"},
       {"body", {{"nonce", "n1"}}}},
      {{"id", "handoff_unknown"},
       {"method", "POST"},
       {"path", "/handoff"},
       {"headers", "plain"},
       {"body", {{"nonce", "never-registered"}}}},
      {{"id", "handoff_escalate"},
       {"method", "POST"},
       {"path", "/escalate"},
       {"headers", "plain"},
       {"body", {{"handle", "{escalate_handle}"}}}},
      {{"id", "handoff_escalate_forged"},
       {"method", "POST"},
       {"path", "/escalate"},
       {"headers", "plain"},
       {"body", {{"handle", "forged"}}}},
      {{"id", "handoff_escalate_missing"},
       {"method", "POST"},
       {"path", "/escalate"},
       {"headers", "plain"},
       {"body", json::object()}},
      {{"id", "handoff_say"},
       {"method", "POST"},
       {"path", "/say"},
       {"headers", "plain"},
       {"setup", {{"register", json::array({"n2", "conv-root", "call-9"})}}},
       {"body", {{"nonce", "n2"}, {"text", "  hello there  "}}}},
      {{"id", "handoff_say_empty"},
       {"method", "POST"},
       {"path", "/say"},
       {"headers", "plain"},
       {"body", {{"nonce", "n2"}, {"text", "   "}}}},
      {{"id", "handoff_say_unknown"},
       {"method", "POST"},
       {"path", "/say"},
       {"headers", "plain"},
       {"body", {{"nonce", "guessed"}, {"text", "hello"}}}},
  });
}

// Replace every "handle" value with a placeholder: handles are random.
json scrub(const json& value) {
  if (value.is_object()) {
    json out = json::object();
    for (auto it = value.begin(); it != value.end(); ++it) {
      out[it.key()] =
          (it.key() == "handle" && it.value().is_string()) ? json("<handle>") : scrub(it.value());
    }
    return out;
  }
  if (value.is_array()) {
    json out = json::array();
    for (const auto& v : value) {
      out.push_back(scrub(v));
    }
    return out;
  }
  return value;
}

void replace_all(std::string& text, const std::string& from, const std::string& to) {
  size_t pos = 0;
  while ((pos = text.find(from, pos)) != std::string::npos) {
    text.replace(pos, from.size(), to);
    pos += to.size();
  }
}

std::string strip(const std::string& s) {
  size_t b = s.find_first_not_of(" \t\r\n");
  if (b == std::string::npos) {
    return "";
  }
  size_t e = s.find_last_not_of(" \t\r\n");
  return s.substr(b, e - b + 1);
}

json run(const std::string& url) {
  std::mutex calls_mutex;
  json calls = json::array();
  auto record = [&](json call) {
    const std::lock_guard<std::mutex> lock(calls_mutex);
    calls.push_back(std::move(call));
  };

  AIChatClientOptions copts;
  copts.url = url;
  auto client = std::make_shared<AIChatClient>(copts);

  ChatGatewayOptions gopts;
  gopts.config_url = kConfigUrl;
  gopts.key = kKey;
  gopts.allowed_origins = {kAllowedOrigin};
  gopts.client = client;
  gopts.secret = kSecret;
  ChatGateway gateway(gopts);

  HandoffRouterOptions hopts;
  hopts.capture_leg = [&](const std::string& conversation_id, const std::string& medium) {
    record(json::array({"capture", conversation_id, medium}));
    return true;
  };
  hopts.end_call = [&](const std::string& call_id) { record(json::array({"end_call", call_id})); };
  hopts.send_message = [&](const std::string& call_id, const std::string& text) {
    record(json::array({"send_message", call_id, text}));
    return true;
  };
  HandoffRouter handoff(gateway, hopts);

  httplib::Server server;
  gateway.router()(server, kPrefix);
  handoff.router()(server, kPrefix);
  int port = server.bind_to_any_port("127.0.0.1");
  std::thread server_thread([&server]() { server.listen_after_bind(); });
  for (int i = 0; i < 200 && !server.is_running(); ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  json out = json::object();
  std::string minted;
  const auto sets = header_sets();
  for (const auto& step : steps()) {
    {
      const std::lock_guard<std::mutex> lock(calls_mutex);
      calls = json::array();
    }
    if (step.contains("setup") && step["setup"].contains("register")) {
      const json& reg = step["setup"]["register"];
      handoff.register_(reg[0].get<std::string>(), reg[1].get<std::string>(),
                        reg[2].get<std::string>());
    }

    std::string text;
    bool has_body = step.contains("body");
    if (has_body) {
      text = step["body"].dump();
      if (!minted.empty()) {
        replace_all(text, "{handle}", minted);
        replace_all(text, "{forged_handle}", minted.substr(0, minted.find('.')) + ".AAAA");
      }
      replace_all(text, "{escalate_handle}",
                  gateway.mint_handle(std::string(kEscalateConversation)));
    }

    httplib::Client cli("127.0.0.1", port);
    cli.set_connection_timeout(5, 0);
    cli.set_read_timeout(30, 0);
    httplib::Headers headers;
    std::string content_type = "application/json";
    for (const auto& [k, v] : sets.at(step["headers"].get<std::string>())) {
      if (k == "Content-Type") {
        content_type = v;
      } else {
        headers.emplace(k, v);
      }
    }
    const std::string path = kPrefix + step["path"].get<std::string>();
    httplib::Result res;
    if (step["method"] == "OPTIONS") {
      res = cli.Options(path, headers);
    } else {
      res = cli.Post(path, headers, text, content_type);
    }

    json obs = json::object();
    if (!res) {
      obs["status"] = 0;
      out[step["id"].get<std::string>()] = obs;
      continue;
    }
    if (step["id"] == "gateway_start" && res->has_header("X-Chat-Handle")) {
      minted = res->get_header_value("X-Chat-Handle");
    }
    obs["status"] = res->status;
    std::string raw = strip(res->body);
    json parsed = raw.empty() ? json(nullptr) : json::parse(raw, nullptr, false);
    if (parsed.is_discarded()) {
      parsed = json(raw);
    }
    if (parsed.is_object() && parsed.contains("jsonrpc")) {
      parsed.erase("id");
    }
    if (parsed.is_object() && parsed.contains("handle") && parsed["handle"].is_string()) {
      obs["handle_names"] = gateway.read_handle(parsed["handle"].get<std::string>());
    }
    obs["body"] = scrub(parsed);
    if (step.value("check_headers", false)) {
      json hdrs = json::object();
      for (const auto& name : kCheckedHeaders) {
        if (name == "x-chat-handle") {
          hdrs[name] = res->has_header(name);
        } else {
          hdrs[name] = res->has_header(name) ? json(res->get_header_value(name)) : json(nullptr);
        }
      }
      obs["headers"] = hdrs;
    }
    {
      const std::lock_guard<std::mutex> lock(calls_mutex);
      obs["callbacks"] = calls;
    }
    out[step["id"].get<std::string>()] = obs;
  }

  server.stop();
  server_thread.join();
  client->close();
  return out;
}

}  // namespace

int main() {
  // exception-escape guard: main() must not let an exception escape.
  try {
    // stdout carries the JSON document; keep SDK log lines off it.
    signalwire::Logger::instance().suppress();
    const char* url = std::getenv("MOCK_AI_CHAT_URL");
    if (url == nullptr || *url == '\0') {
      std::cerr << "MOCK_AI_CHAT_URL is not set\n";
      return 2;
    }
    std::cout << run(url).dump() << "\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "fatal: " << e.what() << "\n";
    return 1;
  }
}
