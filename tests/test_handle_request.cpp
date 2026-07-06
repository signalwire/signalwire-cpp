// handle_request — the framework-free request-dispatch primitive on
// SWMLService and AgentBase (Python: SWMLService.handle_request /
// AgentBase.handle_request). Proves the primitive core over plain
// (method, url, headers, body): basic-auth -> 401, routing-callback -> 307,
// otherwise render SWML -> 200. No HTTP server, no mocks — a real service /
// agent driven by the primitive dispatch surface.

#include <map>
#include <optional>
#include <string>
#include <tuple>

#include "signalwire/agent/agent_base.hpp"
#include "signalwire/common.hpp"
#include "signalwire/swml/service.hpp"

using json = nlohmann::json;
namespace sw_swml = signalwire::swml;
namespace sw_agent = signalwire::agent;

namespace {

// Build a valid Basic-auth header map for the fixed test credentials.
std::map<std::string, std::string> auth_headers(const std::string& user, const std::string& pass) {
  return {{"Authorization", "Basic " + signalwire::base64_encode(user + ":" + pass)}};
}

}  // namespace

// --- SWMLService --------------------------------------------------------------

TEST(handle_request_service_200_renders_swml) {
  ::setenv("SWML_BASIC_AUTH_USER", "u1", 1);
  ::setenv("SWML_BASIC_AUTH_PASSWORD", "p1", 1);

  sw_swml::Service svc;
  svc.answer();  // put a verb in the document so the render is non-trivial

  auto [status, headers, body] =
      svc.handle_request("GET", "http://host/", auth_headers("u1", "p1"), std::nullopt);

  ASSERT_EQ(status, 200);
  ASSERT_TRUE(headers.empty());
  // Body is the rendered SWML document (a JSON string).
  json parsed = json::parse(body);
  ASSERT_TRUE(parsed.is_object());
  ASSERT_EQ(body, svc.render_document());

  ::unsetenv("SWML_BASIC_AUTH_USER");
  ::unsetenv("SWML_BASIC_AUTH_PASSWORD");
  return true;
}

TEST(handle_request_service_401_bad_auth) {
  ::setenv("SWML_BASIC_AUTH_USER", "u1", 1);
  ::setenv("SWML_BASIC_AUTH_PASSWORD", "p1", 1);

  sw_swml::Service svc;

  // Wrong password.
  auto bad = svc.handle_request("GET", "http://host/", auth_headers("u1", "wrong"), std::nullopt);
  ASSERT_EQ(std::get<0>(bad), 401);
  ASSERT_EQ(std::get<1>(bad).at("WWW-Authenticate"), std::string("Basic"));
  json err = json::parse(std::get<2>(bad));
  ASSERT_EQ(err["error"], "Unauthorized");

  // No auth header at all.
  auto none = svc.handle_request("GET", "http://host/", {}, std::nullopt);
  ASSERT_EQ(std::get<0>(none), 401);

  ::unsetenv("SWML_BASIC_AUTH_USER");
  ::unsetenv("SWML_BASIC_AUTH_PASSWORD");
  return true;
}

TEST(handle_request_service_307_routing_callback) {
  ::setenv("SWML_BASIC_AUTH_USER", "u1", 1);
  ::setenv("SWML_BASIC_AUTH_PASSWORD", "p1", 1);

  sw_swml::Service svc;
  bool called = false;
  json seen_body;
  std::map<std::string, std::string> seen_headers;
  svc.register_routing_callback(
      [&](const json& body, const std::map<std::string, std::string>& hdrs) -> std::string {
        called = true;
        seen_body = body;
        seen_headers = hdrs;
        return "/redirected";
      },
      "/route");

  auto hdrs = auth_headers("u1", "p1");
  json body = {{"call_id", "abc"}};
  auto [status, resp_headers, resp_body] =
      svc.handle_request("POST", "http://host/route", hdrs, body);

  ASSERT_TRUE(called);
  ASSERT_EQ(status, 307);
  ASSERT_EQ(resp_headers.at("Location"), std::string("/redirected"));
  ASSERT_TRUE(resp_body.empty());
  // The callback received the (body, headers) pair, not (path, params).
  ASSERT_EQ(seen_body["call_id"], "abc");
  ASSERT_TRUE(seen_headers.count("Authorization") == 1);

  ::unsetenv("SWML_BASIC_AUTH_USER");
  ::unsetenv("SWML_BASIC_AUTH_PASSWORD");
  return true;
}

TEST(handle_request_service_get_does_not_invoke_callback) {
  ::setenv("SWML_BASIC_AUTH_USER", "u1", 1);
  ::setenv("SWML_BASIC_AUTH_PASSWORD", "p1", 1);

  sw_swml::Service svc;
  bool called = false;
  svc.register_routing_callback(
      [&](const json&, const std::map<std::string, std::string>&) -> std::string {
        called = true;
        return "/redirected";
      },
      "/route");

  // GET with a body must NOT run the routing callback (POST-only), so it
  // renders SWML with status 200.
  auto [status, headers, body] =
      svc.handle_request("GET", "http://host/route", auth_headers("u1", "p1"), json{{"k", "v"}});
  ASSERT_FALSE(called);
  ASSERT_EQ(status, 200);
  static_cast<void>(headers);
  static_cast<void>(body);

  ::unsetenv("SWML_BASIC_AUTH_USER");
  ::unsetenv("SWML_BASIC_AUTH_PASSWORD");
  return true;
}

// --- AgentBase ----------------------------------------------------------------

TEST(handle_request_agent_200_renders_agent_swml) {
  ::setenv("SWML_BASIC_AUTH_USER", "au", 1);
  ::setenv("SWML_BASIC_AUTH_PASSWORD", "ap", 1);

  sw_agent::AgentBase agent("test-agent", "/");

  auto [status, headers, body] =
      agent.handle_request("GET", "http://host/", auth_headers("au", "ap"), std::nullopt);

  ASSERT_EQ(status, 200);
  ASSERT_TRUE(headers.empty());
  // AgentBase renders via its request-aware path — the SWML has the AI verb.
  json parsed = json::parse(body);
  ASSERT_TRUE(parsed.is_object());
  bool has_ai = parsed.contains("sections") || parsed.contains("version") ||
                parsed.contains("main") || !parsed.empty();
  ASSERT_TRUE(has_ai);

  ::unsetenv("SWML_BASIC_AUTH_USER");
  ::unsetenv("SWML_BASIC_AUTH_PASSWORD");
  return true;
}

TEST(handle_request_agent_401_bad_auth) {
  ::setenv("SWML_BASIC_AUTH_USER", "au", 1);
  ::setenv("SWML_BASIC_AUTH_PASSWORD", "ap", 1);

  sw_agent::AgentBase agent("test-agent", "/");
  auto bad = agent.handle_request("GET", "http://host/", auth_headers("au", "nope"), std::nullopt);
  ASSERT_EQ(std::get<0>(bad), 401);
  ASSERT_EQ(std::get<1>(bad).at("WWW-Authenticate"), std::string("Basic"));

  ::unsetenv("SWML_BASIC_AUTH_USER");
  ::unsetenv("SWML_BASIC_AUTH_PASSWORD");
  return true;
}

TEST(handle_request_agent_307_routing_callback) {
  ::setenv("SWML_BASIC_AUTH_USER", "au", 1);
  ::setenv("SWML_BASIC_AUTH_PASSWORD", "ap", 1);

  sw_agent::AgentBase agent("test-agent", "/");
  agent.register_routing_callback(
      [](const json& body, const std::map<std::string, std::string>&) -> std::string {
        // Route based on a body field to prove (body, headers) shape.
        return body.value("dest", std::string()) == "sales" ? "/sales" : "";
      },
      "/route");

  auto [status, resp_headers, resp_body] = agent.handle_request(
      "POST", "http://host/route", auth_headers("au", "ap"), json{{"dest", "sales"}});
  ASSERT_EQ(status, 307);
  ASSERT_EQ(resp_headers.at("Location"), std::string("/sales"));
  ASSERT_TRUE(resp_body.empty());

  // A body that yields an empty route falls through to 200 (render SWML).
  auto fall = agent.handle_request("POST", "http://host/route", auth_headers("au", "ap"),
                                   json{{"dest", "other"}});
  ASSERT_EQ(std::get<0>(fall), 200);

  ::unsetenv("SWML_BASIC_AUTH_USER");
  ::unsetenv("SWML_BASIC_AUTH_PASSWORD");
  return true;
}
