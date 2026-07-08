// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
//
// Tier-2 behavioral contract tests (porting-sdk/BEHAVIORAL_CONTRACTS.md).
// These assert observable behavior where the method SIGNATURE matched the
// oracle (DRIFT-clean) but the body was a STUB the surface/drift gates cannot
// see. Each test forces the real implementation:
//
//   #2 set_prompt_llm_params / set_post_prompt_llm_params MERGE (not replace)
//   #3 InfoGatherer submit_answer STATE MACHINE
//   #4 native_vector_search REMOTE HTTP (real POST, not a "[Would query…]" stub)
//   #5 serverless per-platform DISPATCH (lambda + cgi + gcf, not detection-only)
//   #6 SIP routing DISPATCH over the served /sip path (not stored-but-unconsulted)
//
// Contract #1 (served-path routing 307) lives in test_served_routing.cpp.

#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>

#include "httplib.h"
#include "signalwire/agent/agent_base.hpp"
#include "signalwire/common.hpp"
#include "signalwire/prefabs/prefabs.hpp"
#include "signalwire/skills/skill_registry.hpp"
#include "signalwire/utils/serverless.hpp"

using json = nlohmann::json;

namespace {

int tier2_pick_free_port() {
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

std::string tier2_basic_auth(const std::string& user, const std::string& pass) {
  return "Basic " + signalwire::base64_encode(user + ":" + pass);
}

// Find the AI verb's prompt object in rendered SWML.
json tier2_ai_prompt(const json& swml) {
  const auto& main = swml["sections"]["main"];
  for (const auto& verb : main) {
    if (verb.contains("ai") && verb["ai"].contains("prompt")) {
      return verb["ai"]["prompt"];
    }
  }
  return json::object();
}

}  // namespace

// ============================================================================
// #2 — set_prompt_llm_params / set_post_prompt_llm_params MERGE (not replace)
// ============================================================================

TEST(tier2_set_prompt_llm_params_merges_distinct_keys) {
  signalwire::agent::AgentBase agent("m", "/");
  // Two calls with DISTINCT keys must ACCUMULATE both (Python .update()).
  // A replace-stub would drop temperature when top_p is set second.
  agent.set_prompt_llm_params(json({{"temperature", 0.5}}));
  agent.set_prompt_llm_params(json({{"top_p", 0.9}}));

  json swml = agent.render_swml();
  json prompt = tier2_ai_prompt(swml);

  ASSERT_TRUE(prompt.contains("temperature"));
  ASSERT_TRUE(prompt.contains("top_p"));
  ASSERT_EQ(prompt["temperature"].get<double>(), 0.5);
  ASSERT_EQ(prompt["top_p"].get<double>(), 0.9);
  return true;
}

TEST(tier2_set_post_prompt_llm_params_merges_distinct_keys) {
  signalwire::agent::AgentBase agent("m", "/");
  agent.set_post_prompt("summarize");  // ensure post_prompt is emitted
  agent.set_post_prompt_llm_params(json({{"temperature", 0.3}}));
  agent.set_post_prompt_llm_params(json({{"top_p", 0.7}}));

  json swml = agent.render_swml();
  // Locate the ai verb; post_prompt params render under ai["post_prompt"].
  const auto& main = swml["sections"]["main"];
  json post;
  for (const auto& verb : main) {
    if (verb.contains("ai") && verb["ai"].contains("post_prompt")) {
      post = verb["ai"]["post_prompt"];
    }
  }
  ASSERT_TRUE(post.contains("temperature"));
  ASSERT_TRUE(post.contains("top_p"));
  ASSERT_EQ(post["temperature"].get<double>(), 0.3);
  ASSERT_EQ(post["top_p"].get<double>(), 0.7);
  return true;
}

// ============================================================================
// #3 — InfoGatherer submit_answer STATE MACHINE
// ============================================================================

TEST(tier2_info_gatherer_submit_answer_advances_state) {
  signalwire::prefabs::InfoGathererAgent agent;
  agent.set_questions({
      json({{"key_name", "name"}, {"question_text", "What is your name?"}}),
      json({{"key_name", "city"}, {"question_text", "What city are you in?"}}),
  });

  // Simulate the SWAIG runtime handing back global_data at index 0.
  json raw = json({{"global_data",
                    json({{"questions",
                           json::array({json({{"key_name", "name"},
                                              {"question_text", "What is your name?"}}),
                                        json({{"key_name", "city"},
                                              {"question_text", "What city are you in?"}})})},
                          {"question_index", 0},
                          {"answers", json::array()}})}});

  auto result = agent.submit_answer(json({{"answer", "Ada"}}), raw).to_json();

  // (a) the answer is RECORDED in global_data.answers
  // (b) question_index ADVANCED to 1
  // (c) the result PRESENTS the 2nd question
  ASSERT_TRUE(result.contains("action"));
  json gd;
  for (const auto& action : result["action"]) {
    if (action.contains("set_global_data")) {
      gd = action["set_global_data"];
    }
  }
  ASSERT_TRUE(gd.contains("answers"));
  ASSERT_EQ(gd["answers"].size(), 1u);
  ASSERT_EQ(gd["answers"][0]["key_name"].get<std::string>(), "name");
  ASSERT_EQ(gd["answers"][0]["answer"].get<std::string>(), "Ada");
  ASSERT_TRUE(gd.contains("question_index"));
  ASSERT_EQ(gd["question_index"].get<int>(), 1);
  // The response text presents the second question.
  ASSERT_TRUE(result.contains("response"));
  ASSERT_TRUE(result["response"].get<std::string>().find("city") != std::string::npos);
  return true;
}

// ============================================================================
// #4 — native_vector_search REMOTE HTTP (real POST, not a stub string)
// ============================================================================

TEST(tier2_native_vector_search_remote_http_post) {
  // Mock remote search server: capture the POST body, return formatted results.
  httplib::Server srv;
  std::atomic<bool> got_post{false};
  std::string captured_query;
  std::string captured_path;
  srv.Post("/search", [&](const httplib::Request& req, httplib::Response& res) {
    got_post = true;
    captured_path = req.path;
    try {
      json body = json::parse(req.body);
      captured_query = body.value("query", "");
    } catch (...) {
    }
    json out = json({{"results",
                      json::array({json({{"content", "The capital of France is Paris."},
                                         {"score", 0.97},
                                         {"metadata", json({{"filename", "geo.md"}})}})})}});
    res.set_content(out.dump(), "application/json");
  });

  // Bind an ephemeral port on the server itself (no pick-then-rebind race).
  int port = srv.bind_to_any_port("127.0.0.1");
  ASSERT_TRUE(port > 0);
  const std::string base = "http://127.0.0.1:" + std::to_string(port);

  std::thread server_thread([&]() { srv.listen_after_bind(); });
  struct Guard {
    httplib::Server& s;
    std::thread& t;
    ~Guard() {
      s.stop();
      if (t.joinable()) t.join();
    }
  } guard{srv, server_thread};

  // Wait for the mock to accept connections.
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (!srv.is_running() && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  ASSERT_TRUE(srv.is_running());

  // Configure the skill in network mode and invoke its tool.
  auto skill = signalwire::skills::SkillRegistry::instance().create("native_vector_search");
  ASSERT_TRUE(skill->setup(json({{"remote_url", base}, {"index_name", "kb"}})));
  auto tools = skill->register_tools();
  ASSERT_EQ(tools.size(), 1u);
  ASSERT_EQ(tools[0].name, std::string("search_knowledge"));
  ASSERT_TRUE(static_cast<bool>(tools[0].handler));

  auto result = tools[0].handler(json({{"query", "capital of France"}}), json::object());
  json rj = result.to_json();

  // A real POST to <remote_url>/search happened with the query in the body.
  ASSERT_TRUE(got_post.load());
  ASSERT_EQ(captured_path, std::string("/search"));
  ASSERT_EQ(captured_query, std::string("capital of France"));

  // The mock's results are FORMATTED into the FunctionResult (NOT a
  // "[Would query…]" stub string).
  ASSERT_TRUE(rj.contains("response"));
  std::string resp = rj["response"].get<std::string>();
  ASSERT_TRUE(resp.find("Paris") != std::string::npos);
  ASSERT_TRUE(resp.find("Would query") == std::string::npos);
  return true;
}

// ============================================================================
// #5 — Serverless per-platform DISPATCH (lambda + cgi + gcf)
// ============================================================================

TEST(tier2_serverless_dispatch_lambda_cgi_gcf) {
  ::setenv("SWML_BASIC_AUTH_USER", "su", 1);
  ::setenv("SWML_BASIC_AUTH_PASSWORD", "sp", 1);
  struct EnvGuard {
    ~EnvGuard() {
      ::unsetenv("SWML_BASIC_AUTH_USER");
      ::unsetenv("SWML_BASIC_AUTH_PASSWORD");
    }
  } env_guard;

  const std::string auth = tier2_basic_auth("su", "sp");

  // ---- Lambda (API Gateway HTTP API v2 proxy event) ----
  {
    signalwire::agent::AgentBase agent("sl", "/");
    json event = json({{"rawPath", "/"},
                       {"requestContext", json({{"http", json({{"method", "GET"}})}})},
                       {"headers", json({{"Authorization", auth}})}});
    auto resp = signalwire::utils::handle_lambda(agent, event, json::object());
    ASSERT_EQ(resp.status, 200);
    json doc = json::parse(resp.body);
    ASSERT_TRUE(doc.is_object());
    ASSERT_TRUE(doc.contains("sections"));
  }

  // ---- CGI (env + body) ----
  {
    signalwire::agent::AgentBase agent("sl", "/");
    std::map<std::string, std::string> env = {
        {"REQUEST_METHOD", "GET"}, {"PATH_INFO", "/"}, {"HTTP_AUTHORIZATION", auth}};
    auto resp = signalwire::utils::handle_cgi(agent, env, std::nullopt);
    ASSERT_EQ(resp.status, 200);
    json doc = json::parse(resp.body);
    ASSERT_TRUE(doc.contains("sections"));
  }

  // ---- Google Cloud Function (method/path/headers/body tuple) ----
  {
    signalwire::agent::AgentBase agent("sl", "/");
    std::map<std::string, std::string> headers = {{"Authorization", auth}};
    auto resp = signalwire::utils::handle_gcf(agent, "GET", "/", headers, std::nullopt);
    ASSERT_EQ(resp.status, 200);
    json doc = json::parse(resp.body);
    ASSERT_TRUE(doc.contains("sections"));
  }

  // ---- Dispatcher (forced mode) routes to the right handler ----
  {
    signalwire::agent::AgentBase agent("sl", "/");
    json event = json({{"rawPath", "/"},
                       {"requestContext", json({{"http", json({{"method", "GET"}})}})},
                       {"headers", json({{"Authorization", auth}})}});
    auto resp =
        signalwire::utils::handle_serverless_request(agent, event, json::object(), "lambda");
    ASSERT_EQ(resp.status, 200);
  }
  return true;
}

// ============================================================================
// #6 — SIP routing DISPATCH over the served /sip path
// ============================================================================

TEST(tier2_sip_routing_served_dispatch) {
  ::setenv("SWML_BASIC_AUTH_USER", "su", 1);
  ::setenv("SWML_BASIC_AUTH_PASSWORD", "sp", 1);
  ::unsetenv("PORT");

  int port = tier2_pick_free_port();

  signalwire::agent::AgentBase agent("support", "/");
  agent.set_host("127.0.0.1").set_port(port);
  agent.enable_sip_routing(true);
  agent.register_sip_username("alice");

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
    httplib::Client cli(base);
    cli.set_connection_timeout(1, 0);
    auto res = cli.Get("/health");
    if (res && res->status == 200) {
      up = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  ASSERT_TRUE(up);

  httplib::Client cli(base);
  cli.set_connection_timeout(2, 0);
  cli.set_read_timeout(2, 0);
  cli.set_follow_location(false);
  httplib::Headers h = {{"Authorization", tier2_basic_auth("su", "sp")},
                        {"Content-Type", "application/json"}};

  // POST a SIP-shaped body to the served /sip endpoint. The registered SIP
  // routing callback must FIRE (extract the username, consult the registered
  // set) and the request is routed to a real SWML response — NOT a 404 (which
  // is what a stored-but-unconsulted mapping / unmounted /sip route produces).
  json sip_body = json({{"call", json({{"to", "sip:alice@example.com"}})}});
  auto res = cli.Post("/sip", h, sip_body.dump(), "application/json");
  ASSERT_TRUE(static_cast<bool>(res));
  ASSERT_EQ(res->status, 200);
  json doc = json::parse(res->body);
  ASSERT_TRUE(doc.is_object());
  ASSERT_TRUE(doc.contains("sections"));

  // Bad auth on /sip -> 401 (proves the served /sip runs the full handle_request
  // pipeline, not a bypass).
  {
    httplib::Headers bad = {{"Authorization", tier2_basic_auth("su", "wrong")},
                            {"Content-Type", "application/json"}};
    auto r2 = cli.Post("/sip", bad, sip_body.dump(), "application/json");
    ASSERT_TRUE(static_cast<bool>(r2));
    ASSERT_EQ(r2->status, 401);
  }
  return true;
}
