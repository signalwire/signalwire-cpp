// http_dump.cpp — the C++ port's HTTP dump program for the cross-port HTTP
// differ (porting-sdk/scripts/diff_port_http.py).
//
// For each http_corpus case it feeds a synthetic request into the C++ SDK's
// framework-free dispatch core (SWMLService::handle_request,
// Service::extract_sip_username, the webhook Validate decision core, and the
// serverless lambda adapter) and prints ONE JSON object mapping
//
//     case-id -> reduced-artifact
//
// to stdout, reduced to the same shape the python oracle emits. The differ
// canonicalizes both sides and byte-compares. Only stdout carries JSON.
//
// The corpus sentinels (__AUTH__/__AUTH_BAD__ Basic headers, __SIG__ webhook
// signature, __REDIRECT_CB__ routing callback, __HELLO_HANDLER__ SWAIG handler,
// __JSON__: lambda body prefix) are materialized here as the oracle does.
// Mirrors the Go dump signalwire-go/cmd/http-dump.

#include <openssl/hmac.h>

#include <array>
#include <iostream>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

#include "signalwire/agent/agent_base.hpp"
#include "signalwire/common.hpp"
#include "signalwire/security/webhook_validator.hpp"
#include "signalwire/swaig/function_result.hpp"
#include "signalwire/swml/service.hpp"
#include "signalwire/utils/serverless.hpp"

using json = nlohmann::json;
using signalwire::agent::AgentBase;
using signalwire::security::Validate;
using signalwire::swaig::FunctionResult;
using signalwire::swml::Service;

namespace {

const std::string kUser = "user";
const std::string kPassword = "pass";
const std::string kSigningKey = "PSK-fixed-signing-key";
const std::string kWhUrl = "https://agent.example.com/webhook";
const std::string kWhBody = R"({"event":"call.created","id":"abc"})";

std::string basic_auth(const std::string& u, const std::string& p) {
  return "Basic " + signalwire::base64_encode(u + ":" + p);
}

// hex(HMAC-SHA1(key, url+body)) — the correct webhook signature (__SIG__).
std::string webhook_sig(const std::string& url, const std::string& body, const std::string& key) {
  std::array<unsigned char, EVP_MAX_MD_SIZE> md{};
  unsigned int md_len = 0;
  std::string data = url + body;
  HMAC(EVP_sha1(), key.data(), static_cast<int>(key.size()),
       reinterpret_cast<const unsigned char*>(data.data()), data.size(), md.data(), &md_len);
  static const char* kHex = "0123456789abcdef";
  std::string out;
  for (unsigned int i = 0; i < md_len; ++i) {
    out.push_back(kHex[md[i] >> 4]);
    out.push_back(kHex[md[i] & 0x0F]);
  }
  return out;
}

// observe_response reduces a (status, headers, body) triple to a comparable
// artifact — the mirror of diff_port_http._observe_response.
json observe_response(int status, const std::map<std::string, std::string>& headers,
                      const std::string& body_str, const std::string& kind) {
  json out = json::object();
  out["status"] = status;
  std::vector<std::string> keys;
  for (const auto& [k, v] : headers) keys.push_back(k);
  out["header_keys"] = keys;  // std::map already sorted
  if (headers.count("Location")) out["location"] = headers.at("Location");
  if (headers.count("WWW-Authenticate")) out["www_authenticate"] = headers.at("WWW-Authenticate");
  if (kind == "response_full") {
    if (body_str.empty()) {
      out["body"] = "";
    } else {
      json parsed = json::parse(body_str, nullptr, false);
      out["body"] = parsed.is_discarded() ? json(body_str) : parsed;
    }
  }
  return out;
}

// Service is non-copyable (holds a unique_ptr), so configure in place.
void init_service(Service& svc) {
  svc.set_name("demo");
  svc.set_route("/swml");
  svc.set_auth(kUser, kPassword);
}

// The redirect callback: redirect one specific 'to', else pass through ("").
std::string redirect_cb(const json& body, const std::map<std::string, std::string>&) {
  if (body.is_object() && body.contains("call") && body["call"].is_object() &&
      body["call"].contains("to") && body["call"]["to"].is_string() &&
      body["call"]["to"].get<std::string>() == "sip:redirect-me@space") {
    return "/other-route";
  }
  return "";
}

json extract_username(const json& body) {
  std::string u = Service::extract_sip_username(body);
  json out = json::object();
  if (u.empty()) {
    out["username"] = nullptr;
  } else {
    out["username"] = u;
  }
  return out;
}

json webhook_decision(const std::string& method, const std::string& url, const std::string& body,
                      const std::map<std::string, std::string>& headers, const std::string& key) {
  auto rej = Validate(method, url, headers, body, key);
  json out = json::object();
  if (!rej.has_value()) {
    out["decision"] = "pass";
  } else {
    out["decision"] = "reject";
    out["status"] = std::get<0>(*rej);
  }
  return out;
}

// reduce_serverless reduces a ServerlessResponse to {status, body(parsed)} —
// mirroring the oracle's serverless_result observer.
json reduce_serverless(const signalwire::utils::ServerlessResponse& res) {
  json out = json::object();
  out["status"] = res.status;
  if (res.body.empty()) {
    out["body"] = res.body;
  } else {
    json parsed = json::parse(res.body, nullptr, false);
    out["body"] = parsed.is_discarded() ? json(res.body) : parsed;
  }
  return out;
}

}  // namespace

int main() {
  json out = json::object();

  // ---- handle_request: 200 SWML happy path ----
  {
    Service svc;
    init_service(svc);
    auto [status, headers, body] =
        svc.handle_request("GET", "http://localhost:3000/swml",
                           {{"Authorization", basic_auth(kUser, kPassword)}}, std::nullopt);
    out["http_handle_request_200_swml"] = observe_response(status, headers, body, "response_full");
  }
  // ---- handle_request: 401 no auth ----
  {
    Service svc;
    init_service(svc);
    auto [status, headers, body] =
        svc.handle_request("GET", "http://localhost:3000/swml", {}, std::nullopt);
    out["http_handle_request_401_no_auth"] =
        observe_response(status, headers, body, "response_full");
  }
  // ---- handle_request: 401 bad password (status+headers only) ----
  {
    Service svc;
    init_service(svc);
    auto [status, headers, body] =
        svc.handle_request("GET", "http://localhost:3000/swml",
                           {{"Authorization", basic_auth(kUser, "wrong")}}, std::nullopt);
    out["http_handle_request_401_bad_password"] =
        observe_response(status, headers, body, "response_status_headers");
  }
  // ---- handle_request: 307 redirect via routing callback ----
  {
    Service svc;
    init_service(svc);
    svc.register_routing_callback(redirect_cb, "/sip");
    json body = json{{"call", {{"to", "sip:redirect-me@space"}}}};
    auto [status, headers, body_str] =
        svc.handle_request("POST", "http://localhost:3000/swml/sip",
                           {{"Authorization", basic_auth(kUser, kPassword)}}, body);
    out["http_handle_request_307_redirect"] =
        observe_response(status, headers, body_str, "response_full");
  }
  // ---- handle_request: callback returns "" -> normal 200 SWML ----
  {
    Service svc;
    init_service(svc);
    svc.register_routing_callback(redirect_cb, "/sip");
    json body = json{{"call", {{"to", "sip:keep@space"}}}};
    auto [status, headers, body_str] =
        svc.handle_request("POST", "http://localhost:3000/swml/sip",
                           {{"Authorization", basic_auth(kUser, kPassword)}}, body);
    out["http_handle_request_callback_passthrough_200"] =
        observe_response(status, headers, body_str, "response_full");
  }

  // ---- extract_sip_username: pure extractor ----
  out["http_extract_sip_username_sip"] =
      extract_username(json{{"call", {{"to", "sip:alice@agents.signalwire.com"}}}});
  out["http_extract_sip_username_tel"] =
      extract_username(json{{"call", {{"to", "tel:+15551234567"}}}});
  out["http_extract_sip_username_plain"] = extract_username(json{{"call", {{"to", "support"}}}});
  out["http_extract_sip_username_missing"] = extract_username(json{{"vars", json::object()}});

  // ---- webhook validate ----
  out["http_webhook_validate_ok"] = webhook_decision(
      "POST", kWhUrl, kWhBody,
      {{"x-signalwire-signature", webhook_sig(kWhUrl, kWhBody, kSigningKey)}}, kSigningKey);
  {
    std::string bad;
    for (int i = 0; i < 5; ++i) bad += "deadbeef";
    out["http_webhook_validate_bad_sig"] =
        webhook_decision("POST", kWhUrl, kWhBody, {{"x-signalwire-signature", bad}}, kSigningKey);
  }
  out["http_webhook_validate_missing_sig"] =
      webhook_decision("POST", kWhUrl, kWhBody, {}, kSigningKey);
  out["http_webhook_validate_twilio_alias"] = webhook_decision(
      "POST", kWhUrl, kWhBody, {{"x-twilio-signature", webhook_sig(kWhUrl, kWhBody, kSigningKey)}},
      kSigningKey);

  // ---- serverless (lambda) ----
  {
    // SWAIG dispatch. Matches the oracle ctor (name "demo", route "/demo").
    AgentBase a("demo", "/demo");
    a.set_auth(kUser, kPassword);
    signalwire::swaig::ToolHandler handler = [](const json&, const json&) {
      return FunctionResult("hello there");
    };
    a.define_tool("say_hello", "greet", json::object(), handler);
    json event = {
        {"rawPath", "/swaig"},
        {"headers",
         {{"authorization", basic_auth(kUser, kPassword)}, {"content-type", "application/json"}}},
        {"body", R"({"function":"say_hello","argument":{"parsed":[{}]},"call_id":"c1"})"}};
    auto res = signalwire::utils::handle_lambda(a, event);
    out["http_serverless_lambda_swaig"] = reduce_serverless(res);
  }
  {
    AgentBase a("demo", "/demo");
    a.set_auth(kUser, kPassword);
    json event = {{"rawPath", "/"}, {"headers", json::object()}, {"body", nullptr}};
    auto res = signalwire::utils::handle_lambda(a, event);
    out["http_serverless_lambda_noauth_401"] = reduce_serverless(res);
  }

  std::cout << out.dump() << "\n";
  return 0;
}
