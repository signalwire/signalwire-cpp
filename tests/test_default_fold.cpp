// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
//
// Default-VALUE fold to the reference (wave6). These divergences were invisible
// while cpp's enumerator reported every default as null; they surfaced the
// moment it began emitting real default VALUES.
//
// Every assertion here is BEHAVIOURAL, not constructional: the prefab routes are
// proven by SERVING the agent and observing which URL actually answers 200
// (a stored-but-unmounted string would pass a getter test and fail this one),
// the routing-callback default is proven by the registered/mounted path
// answering the callback, and pay(postal_code) is proven on the rendered WIRE
// payload rather than on the parameter.
//
// Reference sources for the expected values:
//   prefabs/survey.py:64          route = "/survey"
//   prefabs/concierge.py:54       route = "/concierge"
//   prefabs/faq_bot.py:54         route = "/faq"
//   prefabs/receptionist.py:42    route = "/receptionist"
//   prefabs/info_gatherer.py:45   route = "/info_gatherer"
//   core/mixins/web_mixin.py:1284 path  = "/sip"
//   core/swml_service.py:921      path  = "/sip"
//   core/function_result.py:819   postal_code: bool | str = True
//                          :881   pay_params["postal_code"] = str(v).lower()

#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <string>
#include <thread>

#include "httplib.h"
#include "signalwire/agent/agent_base.hpp"
#include "signalwire/common.hpp"
#include "signalwire/prefabs/prefabs.hpp"
#include "signalwire/swaig/function_result.hpp"
#include "signalwire/swml/service.hpp"

using json = nlohmann::json;

namespace {

int dfold_pick_free_port() {
  int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = 0;
  ::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
  socklen_t len = sizeof(addr);
  ::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len);
  int port = ntohs(addr.sin_port);
  ::close(fd);
  return port;
}

std::string dfold_basic_auth(const std::string& user, const std::string& pass) {
  return "Basic " + signalwire::base64_encode(user + ":" + pass);
}

// Serve a DEFAULT-CONSTRUCTED prefab and report which of the candidate paths
// actually answers 200 with a rendered SWML document. Returns the answering
// path, or "" when none did. This is what makes the assertion behavioural: the
// route must be MOUNTED, not merely stored.
std::string dfold_served_route(signalwire::agent::AgentBase& agent,
                               const std::vector<std::string>& candidates) {
  ::setenv("SWML_BASIC_AUTH_USER", "du", 1);
  ::setenv("SWML_BASIC_AUTH_PASSWORD", "dp", 1);
  ::unsetenv("PORT");

  int port = dfold_pick_free_port();
  agent.set_host("127.0.0.1").set_port(port);

  std::thread server_thread([&agent]() { agent.serve(); });
  struct Guard {
    signalwire::agent::AgentBase& a;
    std::thread& t;
    ~Guard() {
      a.stop();
      if (t.joinable()) t.join();
      ::unsetenv("SWML_BASIC_AUTH_USER");
      ::unsetenv("SWML_BASIC_AUTH_PASSWORD");
    }
  } guard{agent, server_thread};

  const std::string base = "http://127.0.0.1:" + std::to_string(port);

  bool up = false;
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (std::chrono::steady_clock::now() < deadline) {
    httplib::Client probe(base);
    probe.set_connection_timeout(1, 0);
    auto res = probe.Get("/health");
    if (res && res->status == 200) {
      up = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  if (!up) {
    return "";
  }

  httplib::Client cli(base);
  cli.set_connection_timeout(2, 0);
  cli.set_read_timeout(5, 0);
  cli.set_follow_location(false);
  httplib::Headers h = {{"Authorization", dfold_basic_auth("du", "dp")}};

  std::string answered;
  for (const auto& path : candidates) {
    auto res = cli.Get(path, h);
    if (res && res->status == 200) {
      json parsed = json::parse(res->body, nullptr, false);
      if (parsed.is_object() && parsed.contains("sections")) {
        if (!answered.empty()) {
          return "<multiple>";  // ambiguous — the test must fail loudly
        }
        answered = path;
      }
    }
  }
  return answered;
}

// Every prefab's default route, plus "/" — the pre-fold value. Serving one
// prefab and probing the WHOLE set means a regression to "/" (or to another
// prefab's path) is caught, not just a miss on the expected path.
const std::vector<std::string>& dfold_all_routes() {
  static const std::vector<std::string> routes = {
      "/", "/survey", "/concierge", "/faq", "/receptionist", "/info_gatherer"};
  return routes;
}

}  // namespace

// ---------------------------------------------------------------------------
// Change 1 — the five prefab `route` defaults, proven by what actually SERVES.
// ---------------------------------------------------------------------------

TEST(default_fold_survey_agent_serves_slash_survey) {
  signalwire::prefabs::SurveyAgent agent;
  ASSERT_EQ(dfold_served_route(agent, dfold_all_routes()), std::string("/survey"));
  return true;
}

TEST(default_fold_concierge_agent_serves_slash_concierge) {
  signalwire::prefabs::ConciergeAgent agent;
  ASSERT_EQ(dfold_served_route(agent, dfold_all_routes()), std::string("/concierge"));
  return true;
}

TEST(default_fold_faq_bot_agent_serves_slash_faq) {
  signalwire::prefabs::FAQBotAgent agent;
  ASSERT_EQ(dfold_served_route(agent, dfold_all_routes()), std::string("/faq"));
  return true;
}

TEST(default_fold_receptionist_agent_serves_slash_receptionist) {
  signalwire::prefabs::ReceptionistAgent agent;
  ASSERT_EQ(dfold_served_route(agent, dfold_all_routes()), std::string("/receptionist"));
  return true;
}

TEST(default_fold_info_gatherer_agent_serves_slash_info_gatherer) {
  signalwire::prefabs::InfoGathererAgent agent;
  ASSERT_EQ(dfold_served_route(agent, dfold_all_routes()), std::string("/info_gatherer"));
  return true;
}

// The rendered full URL (Python ``get_full_url``) must carry the same path —
// this is the value handed to the platform as the agent's endpoint.
TEST(default_fold_prefab_full_urls_carry_the_reference_route) {
  signalwire::prefabs::SurveyAgent survey;
  signalwire::prefabs::ConciergeAgent concierge;
  signalwire::prefabs::FAQBotAgent faq;
  signalwire::prefabs::ReceptionistAgent receptionist;
  signalwire::prefabs::InfoGathererAgent info;

  ASSERT_TRUE(survey.get_full_url().find("/survey") != std::string::npos);
  ASSERT_TRUE(concierge.get_full_url().find("/concierge") != std::string::npos);
  ASSERT_TRUE(faq.get_full_url().find("/faq") != std::string::npos);
  ASSERT_TRUE(receptionist.get_full_url().find("/receptionist") != std::string::npos);
  ASSERT_TRUE(info.get_full_url().find("/info_gatherer") != std::string::npos);
  return true;
}

// ---------------------------------------------------------------------------
// Change 2 — register_routing_callback(path) defaults to "/sip".
// ---------------------------------------------------------------------------

// SWMLService: the DEFAULTED registration must land under "/sip" in the
// registry the served dispatcher consults.
TEST(default_fold_service_routing_callback_defaults_to_sip) {
  signalwire::swml::Service svc("svc", "/");
  svc.register_routing_callback(
      [](const json&, const std::map<std::string, std::string>&) -> std::string { return ""; });

  auto paths = svc.get_routing_callback_paths();
  ASSERT_EQ(paths.size(), static_cast<size_t>(1));
  ASSERT_EQ(paths[0], std::string("/sip"));
  return true;
}

// AgentBase: prove the DEFAULTED path is actually MOUNTED and CONSULTED — a
// POST to /sip must reach the callback and produce its 307 redirect. Serving an
// agent whose route is "/" means a callback left at "/" would collide with the
// base route and never demonstrate /sip.
TEST(default_fold_agent_routing_callback_defaults_to_sip_and_is_served) {
  ::setenv("SWML_BASIC_AUTH_USER", "du", 1);
  ::setenv("SWML_BASIC_AUTH_PASSWORD", "dp", 1);
  ::unsetenv("PORT");

  int port = dfold_pick_free_port();
  signalwire::agent::AgentBase agent("sip-default-agent", "/");
  agent.set_host("127.0.0.1").set_port(port);

  // NOTE: no path argument — the default is the whole point of this test.
  agent.register_routing_callback(
      [](const json& body, const std::map<std::string, std::string>&) -> std::string {
        return body.value("dest", std::string()) == "sales" ? "/sales" : "";
      });

  std::thread server_thread([&agent]() { agent.serve(); });
  struct Guard {
    signalwire::agent::AgentBase& a;
    std::thread& t;
    ~Guard() {
      a.stop();
      if (t.joinable()) t.join();
      ::unsetenv("SWML_BASIC_AUTH_USER");
      ::unsetenv("SWML_BASIC_AUTH_PASSWORD");
    }
  } guard{agent, server_thread};

  const std::string base = "http://127.0.0.1:" + std::to_string(port);
  bool up = false;
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (std::chrono::steady_clock::now() < deadline) {
    httplib::Client probe(base);
    probe.set_connection_timeout(1, 0);
    auto res = probe.Get("/health");
    if (res && res->status == 200) {
      up = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  ASSERT_TRUE(up);

  httplib::Client cli(base);
  cli.set_connection_timeout(2, 0);
  cli.set_read_timeout(5, 0);
  cli.set_follow_location(false);
  httplib::Headers h = {{"Authorization", dfold_basic_auth("du", "dp")},
                        {"Content-Type", "application/json"}};

  auto res = cli.Post("/sip", h, json({{"dest", "sales"}}).dump(), "application/json");
  ASSERT_TRUE(static_cast<bool>(res));
  ASSERT_EQ(res->status, 307);
  ASSERT_EQ(res->get_header_value("Location"), std::string("/sales"));
  return true;
}

// ---------------------------------------------------------------------------
// Change 3 — pay(postal_code) accepts bool|string, like the reference.
//
// The WIRE form for BOTH arms is a JSON string: the schema declares
// `postal_code: anyOf[boolean,string]` but the reference emits
// `str(postal_code).lower()`, measured as `"postal_code": "true"`. These
// assertions pin the emitted TYPE (is_string) as well as the value, so a
// regression to a raw JSON boolean fails here.
// ---------------------------------------------------------------------------

namespace {

json dfold_pay_params(const signalwire::swaig::FunctionResult& r) {
  json j = r.to_json();
  return j["action"][0]["SWML"]["sections"]["main"][1]["pay"];
}

}  // namespace

TEST(default_fold_pay_postal_code_default_is_the_string_true) {
  signalwire::swaig::FunctionResult r("Processing payment");
  r.pay("https://pay.example.com/process");
  json p = dfold_pay_params(r);

  ASSERT_TRUE(p.contains("postal_code"));
  ASSERT_TRUE(p["postal_code"].is_string());
  ASSERT_EQ(p["postal_code"].get<std::string>(), std::string("true"));
  return true;
}

TEST(default_fold_pay_postal_code_bool_false_lowercases) {
  signalwire::swaig::FunctionResult r("Processing payment");
  r.pay("https://pay.example.com/process", "dtmf", "", "credit-card", 5, 1, true, false);
  json p = dfold_pay_params(r);

  ASSERT_TRUE(p["postal_code"].is_string());
  ASSERT_EQ(p["postal_code"].get<std::string>(), std::string("false"));
  return true;
}

// The string arm of the union: an explicit postcode passes through verbatim.
TEST(default_fold_pay_postal_code_string_passes_through) {
  signalwire::swaig::FunctionResult r("Processing payment");
  r.pay("https://pay.example.com/process", "dtmf", "", "credit-card", 5, 1, true,
        std::string("94103"));
  json p = dfold_pay_params(r);

  ASSERT_TRUE(p["postal_code"].is_string());
  ASSERT_EQ(p["postal_code"].get<std::string>(), std::string("94103"));
  return true;
}
