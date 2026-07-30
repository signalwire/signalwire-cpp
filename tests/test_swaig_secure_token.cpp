// Copyright (c) 2025 SignalWire
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.
//
// `secure=true` SWAIG token enforcement, on EVERY transport.
//
// A tool registered with `secure=true` REQUIRES a valid `__token`. An ABSENT
// token is refused exactly like a forged one -- omitting the credential must
// never be weaker than presenting a wrong one, or `secure` would be a flag that
// permits anonymous calls. A missing `call_id` counts as UNVALIDATED (there is
// nothing to check the token against), never as a bypass. An `secure=false`
// tool runs ungated in all of those cases.
//
// The refusal is a 200 + FunctionResult body, NOT an HTTP error status: the
// engine has no handling for a SWAIG refusal status, so the tool reports that
// it cannot execute and the model relays that to the caller.
//
// The credential rides the QUERY STRING; the `call_id` rides the POST BODY.
// That split is identical on the HTTP endpoint and on every serverless mode,
// so serverless is not a weaker transport -- just a different envelope.

#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>

#include <httplib.h>

#include "signalwire/agent/agent_base.hpp"
#include "signalwire/common.hpp"
#include "signalwire/swaig/function_result.hpp"
#include "signalwire/utils/serverless.hpp"

namespace {

using json = nlohmann::json;
using signalwire::agent::AgentBase;
using signalwire::swaig::FunctionResult;

constexpr const char* kSecUser = "tuser";
constexpr const char* kSecPass = "tpass";

std::string sec_basic_auth(const std::string& u, const std::string& p) {
  return "Basic " + signalwire::base64_encode(u + ":" + p);
}

// An agent with one secure tool ("say_hello") and one insecure tool
// ("open_hello"), both returning a distinctive marker so a test can tell a RUN
// apart from a REFUSAL by the body alone.
class SecureTokenAgent : public AgentBase {
 public:
  SecureTokenAgent() : AgentBase("demo", "/demo") {
    set_auth(kSecUser, kSecPass);
    signalwire::swaig::ToolHandler handler = [](const json&, const json&) {
      return FunctionResult("HANDLER RAN");
    };
    // define_tool defaults to secure=true.
    define_tool("say_hello", "greet", json::object(), handler);

    signalwire::swaig::ToolDefinition open_tool;
    open_tool.name = "open_hello";
    open_tool.description = "greet, ungated";
    open_tool.parameters = json::object();
    open_tool.secure = false;
    open_tool.handler = handler;
    define_tool(open_tool);
  }

  // Expose the protected HTTP /swaig dispatcher so a test can drive the same
  // handler the served route mounts, without standing up a real server.
  void dispatch_swaig(const httplib::Request& req, httplib::Response& res) {
    handle_swaig_request(req, res);
  }
};

// Did the response body carry the handler's marker (the handler RAN)?
bool body_ran(const std::string& body) {
  json parsed = json::parse(body, nullptr, false);
  if (parsed.is_discarded() || !parsed.is_object()) {
    return false;
  }
  return parsed.value("response", std::string()) == "HANDLER RAN";
}

// Did the response body carry the refusal (the handler did NOT run)?
bool body_refused(const std::string& body) {
  json parsed = json::parse(body, nullptr, false);
  if (parsed.is_discarded() || !parsed.is_object()) {
    return false;
  }
  const std::string response = parsed.value("response", std::string());
  return response.find("security token") != std::string::npos;
}

// Build the lambda event for one SWAIG call. `token` empty -> no query string
// at all; `call_id` empty -> the body carries no call_id key.
json lambda_event(const std::string& function_name, const std::string& token,
                  const std::string& call_id) {
  json body = json::object();
  body["function"] = function_name;
  body["argument"] = json{{"parsed", json::array({json::object()})}};
  if (!call_id.empty()) {
    body["call_id"] = call_id;
  }
  json event = json::object();
  event["rawPath"] = "/swaig";
  event["headers"] = json{{"authorization", sec_basic_auth(kSecUser, kSecPass)}};
  event["body"] = body.dump();
  if (!token.empty()) {
    event["queryStringParameters"] = json{{"__token", token}};
  }
  return event;
}

}  // namespace

// ============================================================================
// SERVERLESS (lambda) -- valid / forged / absent / no-call-id, secure tool
// ============================================================================

TEST(swaig_secure_token_serverless_lambda_valid_token_runs) {
  SecureTokenAgent agent;
  const std::string token = agent.create_tool_token("say_hello", "c1");
  ASSERT_FALSE(token.empty());
  auto resp = signalwire::utils::handle_lambda(agent, lambda_event("say_hello", token, "c1"));
  ASSERT_EQ(resp.status, 200);
  ASSERT_TRUE(body_ran(resp.body));
  return true;
}

TEST(swaig_secure_token_serverless_lambda_forged_token_refused) {
  SecureTokenAgent agent;
  auto resp = signalwire::utils::handle_lambda(
      agent, lambda_event("say_hello", "deadbeefdeadbeefdeadbeef", "c1"));
  // 200 + FunctionResult refusal, NOT an HTTP error status.
  ASSERT_EQ(resp.status, 200);
  ASSERT_FALSE(body_ran(resp.body));
  ASSERT_TRUE(body_refused(resp.body));
  return true;
}

TEST(swaig_secure_token_serverless_lambda_absent_token_refused) {
  SecureTokenAgent agent;
  auto resp = signalwire::utils::handle_lambda(agent, lambda_event("say_hello", "", "c1"));
  ASSERT_EQ(resp.status, 200);
  ASSERT_FALSE(body_ran(resp.body));
  ASSERT_TRUE(body_refused(resp.body));
  return true;
}

TEST(swaig_secure_token_serverless_lambda_absent_call_id_refused) {
  SecureTokenAgent agent;
  // A genuinely-minted token, but no call_id to validate it against. There is
  // nothing to check it against, so it counts as unvalidated -- never a bypass.
  const std::string token = agent.create_tool_token("say_hello", "c1");
  auto resp = signalwire::utils::handle_lambda(agent, lambda_event("say_hello", token, ""));
  ASSERT_EQ(resp.status, 200);
  ASSERT_FALSE(body_ran(resp.body));
  ASSERT_TRUE(body_refused(resp.body));
  return true;
}

TEST(swaig_secure_token_serverless_lambda_token_for_another_call_refused) {
  SecureTokenAgent agent;
  const std::string token = agent.create_tool_token("say_hello", "OTHER_CALL");
  auto resp = signalwire::utils::handle_lambda(agent, lambda_event("say_hello", token, "c1"));
  ASSERT_EQ(resp.status, 200);
  ASSERT_TRUE(body_refused(resp.body));
  return true;
}

TEST(swaig_secure_token_serverless_lambda_token_for_another_function_refused) {
  SecureTokenAgent agent;
  const std::string token = agent.create_tool_token("open_hello", "c1");
  auto resp = signalwire::utils::handle_lambda(agent, lambda_event("say_hello", token, "c1"));
  ASSERT_EQ(resp.status, 200);
  ASSERT_TRUE(body_refused(resp.body));
  return true;
}

// The HTTP API v2 raw query-string shape, as an alternative to the parsed
// `queryStringParameters` mapping.
TEST(swaig_secure_token_serverless_lambda_raw_query_string_accepted) {
  SecureTokenAgent agent;
  const std::string token = agent.create_tool_token("say_hello", "c1");
  json event = lambda_event("say_hello", "", "c1");
  event["rawQueryString"] = "__token=" + token;
  auto resp = signalwire::utils::handle_lambda(agent, event);
  ASSERT_EQ(resp.status, 200);
  ASSERT_TRUE(body_ran(resp.body));
  return true;
}

// The reference reads `__token` first and falls back to the bare `token`.
TEST(swaig_secure_token_serverless_lambda_bare_token_spelling_accepted) {
  SecureTokenAgent agent;
  const std::string token = agent.create_tool_token("say_hello", "c1");
  json event = lambda_event("say_hello", "", "c1");
  event["queryStringParameters"] = json{{"token", token}};
  auto resp = signalwire::utils::handle_lambda(agent, event);
  ASSERT_EQ(resp.status, 200);
  ASSERT_TRUE(body_ran(resp.body));
  return true;
}

// ============================================================================
// SERVERLESS (lambda) -- the INSECURE tool runs ungated in every case
// ============================================================================

TEST(swaig_secure_token_serverless_lambda_insecure_tool_runs_ungated) {
  SecureTokenAgent agent;
  const std::string good = agent.create_tool_token("open_hello", "c1");

  // valid token
  {
    auto resp = signalwire::utils::handle_lambda(agent, lambda_event("open_hello", good, "c1"));
    ASSERT_EQ(resp.status, 200);
    ASSERT_TRUE(body_ran(resp.body));
  }
  // forged token
  {
    auto resp =
        signalwire::utils::handle_lambda(agent, lambda_event("open_hello", "deadbeef", "c1"));
    ASSERT_EQ(resp.status, 200);
    ASSERT_TRUE(body_ran(resp.body));
  }
  // absent token
  {
    auto resp = signalwire::utils::handle_lambda(agent, lambda_event("open_hello", "", "c1"));
    ASSERT_EQ(resp.status, 200);
    ASSERT_TRUE(body_ran(resp.body));
  }
  // absent call_id
  {
    auto resp = signalwire::utils::handle_lambda(agent, lambda_event("open_hello", "", ""));
    ASSERT_EQ(resp.status, 200);
    ASSERT_TRUE(body_ran(resp.body));
  }
  return true;
}

// ============================================================================
// SERVERLESS (lambda) -- path-based routing reaches the SAME check
// ============================================================================

TEST(swaig_secure_token_serverless_lambda_path_routing_enforced) {
  SecureTokenAgent agent;
  // Case 2 of the lambda dispatcher: the function name is the PATH, not a body
  // key. It must reach the identical decision, or the check is bypassable by
  // choosing the other routing shape.
  json event = lambda_event("say_hello", "", "c1");
  event["rawPath"] = "/say_hello";
  auto resp = signalwire::utils::handle_lambda(agent, event);
  ASSERT_EQ(resp.status, 200);
  ASSERT_TRUE(body_refused(resp.body));

  const std::string token = agent.create_tool_token("say_hello", "c1");
  json ok_event = lambda_event("say_hello", token, "c1");
  ok_event["rawPath"] = "/say_hello";
  auto ok = signalwire::utils::handle_lambda(agent, ok_event);
  ASSERT_EQ(ok.status, 200);
  ASSERT_TRUE(body_ran(ok.body));
  return true;
}

// ============================================================================
// HTTP -- the same four rows, over the httplib endpoint
// ============================================================================

namespace {

// Drive the HTTP /swaig endpoint the way the served route does, through the
// public handler surface. Returns (status, body).
std::pair<int, std::string> http_swaig(SecureTokenAgent& agent, const std::string& function_name,
                                       const std::string& token, const std::string& call_id) {
  json body = json::object();
  body["function"] = function_name;
  body["argument"] = json{{"parsed", json::array({json::object()})}};
  if (!call_id.empty()) {
    body["call_id"] = call_id;
  }
  httplib::Request req;
  req.method = "POST";
  req.path = "/demo/swaig";
  req.body = body.dump();
  req.set_header("Authorization", sec_basic_auth(kSecUser, kSecPass));
  req.set_header("Content-Type", "application/json");
  if (!token.empty()) {
    req.params.emplace("__token", token);
  }
  httplib::Response res;
  agent.dispatch_swaig(req, res);
  // httplib leaves `status` at its -1 sentinel when a handler never sets one
  // and fills in 200 on the wire; model that here so the assertion is about
  // the status the CALLER sees, not the sentinel.
  return {res.status == -1 ? 200 : res.status, res.body};
}

}  // namespace

TEST(swaig_secure_token_http_valid_token_runs) {
  SecureTokenAgent agent;
  const std::string token = agent.create_tool_token("say_hello", "c1");
  auto [status, body] = http_swaig(agent, "say_hello", token, "c1");
  ASSERT_EQ(status, 200);
  ASSERT_TRUE(body_ran(body));
  return true;
}

TEST(swaig_secure_token_http_forged_token_refused) {
  SecureTokenAgent agent;
  auto [status, body] = http_swaig(agent, "say_hello", "deadbeefdeadbeef", "c1");
  // 200 + FunctionResult refusal, NOT 403 -- the engine has no handling for a
  // SWAIG refusal status, so a non-200 is dropped rather than relayed.
  ASSERT_EQ(status, 200);
  ASSERT_FALSE(body_ran(body));
  ASSERT_TRUE(body_refused(body));
  return true;
}

TEST(swaig_secure_token_http_absent_token_refused) {
  SecureTokenAgent agent;
  auto [status, body] = http_swaig(agent, "say_hello", "", "c1");
  ASSERT_EQ(status, 200);
  ASSERT_FALSE(body_ran(body));
  ASSERT_TRUE(body_refused(body));
  return true;
}

TEST(swaig_secure_token_http_absent_call_id_refused) {
  SecureTokenAgent agent;
  const std::string token = agent.create_tool_token("say_hello", "c1");
  auto [status, body] = http_swaig(agent, "say_hello", token, "");
  ASSERT_EQ(status, 200);
  ASSERT_FALSE(body_ran(body));
  ASSERT_TRUE(body_refused(body));
  return true;
}

TEST(swaig_secure_token_http_insecure_tool_runs_ungated) {
  SecureTokenAgent agent;
  const std::string good = agent.create_tool_token("open_hello", "c1");
  {
    auto [status, body] = http_swaig(agent, "open_hello", good, "c1");
    ASSERT_EQ(status, 200);
    ASSERT_TRUE(body_ran(body));
  }
  {
    auto [status, body] = http_swaig(agent, "open_hello", "deadbeef", "c1");
    ASSERT_EQ(status, 200);
    ASSERT_TRUE(body_ran(body));
  }
  {
    auto [status, body] = http_swaig(agent, "open_hello", "", "c1");
    ASSERT_EQ(status, 200);
    ASSERT_TRUE(body_ran(body));
  }
  {
    auto [status, body] = http_swaig(agent, "open_hello", "", "");
    ASSERT_EQ(status, 200);
    ASSERT_TRUE(body_ran(body));
  }
  return true;
}

// ============================================================================
// CGI / GCF / AZURE -- these envelopes do not dispatch SWAIG at all
// ============================================================================
//
// Unlike the lambda envelope (which has its own SWAIG dispatcher), cgi / gcf /
// azure route straight to `AgentBase::handle_request`, whose only outcomes are
// 401, a 307 routing-callback redirect, and a rendered SWML document. There is
// no SWAIG dispatch on those paths, so a tool handler is UNREACHABLE over them
// — which is why there is no token check to make: an unreachable handler cannot
// be reached without a credential either.
//
// These tests pin that property. If a future change adds SWAIG dispatch to
// `handle_request`, they go RED and the token check has to come with it — that
// is exactly the regression they exist to catch.

namespace {

// Does the body look like a rendered SWML document rather than a SWAIG result?
bool body_is_swml(const std::string& body) {
  json parsed = json::parse(body, nullptr, false);
  return !parsed.is_discarded() && parsed.is_object() && parsed.contains("sections");
}

}  // namespace

TEST(swaig_secure_token_serverless_cgi_does_not_dispatch_swaig) {
  SecureTokenAgent agent;
  json body = json{{"function", "say_hello"},
                   {"argument", json{{"parsed", json::array({json::object()})}}},
                   {"call_id", "c1"}};
  std::map<std::string, std::string> env = {
      {"REQUEST_METHOD", "POST"},
      {"PATH_INFO", "/say_hello"},
      {"CONTENT_TYPE", "application/json"},
      {"HTTP_AUTHORIZATION", sec_basic_auth(kSecUser, kSecPass)}};

  auto resp = signalwire::utils::handle_cgi(agent, env, body.dump());
  ASSERT_EQ(resp.status, 200);
  // The handler is unreachable over this envelope: SWML back, never the marker.
  ASSERT_FALSE(body_ran(resp.body));
  ASSERT_TRUE(body_is_swml(resp.body));
  return true;
}

TEST(swaig_secure_token_serverless_gcf_does_not_dispatch_swaig) {
  SecureTokenAgent agent;
  json body = json{{"function", "say_hello"},
                   {"argument", json{{"parsed", json::array({json::object()})}}},
                   {"call_id", "c1"}};
  std::map<std::string, std::string> headers = {
      {"Authorization", sec_basic_auth(kSecUser, kSecPass)},
      {"Content-Type", "application/json"}};

  auto resp = signalwire::utils::handle_gcf(agent, "POST", "/say_hello", headers, body.dump());
  ASSERT_EQ(resp.status, 200);
  ASSERT_FALSE(body_ran(resp.body));
  ASSERT_TRUE(body_is_swml(resp.body));
  return true;
}

TEST(swaig_secure_token_serverless_azure_does_not_dispatch_swaig) {
  SecureTokenAgent agent;
  json body = json{{"function", "say_hello"},
                   {"argument", json{{"parsed", json::array({json::object()})}}},
                   {"call_id", "c1"}};
  json headers = json{{"Authorization", sec_basic_auth(kSecUser, kSecPass)},
                      {"Content-Type", "application/json"}};

  json req = json{{"method", "POST"},
                  {"url", "https://fn.azurewebsites.net/api/say_hello"},
                  {"headers", headers},
                  {"body", body.dump()}};
  auto resp = signalwire::utils::handle_azure(agent, req);
  ASSERT_EQ(resp.status, 200);
  ASSERT_FALSE(body_ran(resp.body));
  ASSERT_TRUE(body_is_swml(resp.body));
  return true;
}
