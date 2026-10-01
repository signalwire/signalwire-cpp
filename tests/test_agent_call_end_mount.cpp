// AgentBase.on_call_end / add_per_call_config / mount (signalwire-python
// core/agent_base.py on_call_end, core/mixins/web_mixin.py add_per_call_config
// and mount).

#include <atomic>
#include <chrono>
#include <thread>

#include "httplib.h"
#include "signalwire/agent/agent_base.hpp"

using signalwire::agent::AgentBase;
using json = nlohmann::json;

namespace call_end_test {

inline json swaig_function(const json& swml, const std::string& name) {
  for (const auto& verb : swml["sections"]["main"]) {
    if (verb.contains("ai") && verb["ai"].contains("SWAIG") &&
        verb["ai"]["SWAIG"].contains("functions")) {
      for (const auto& f : verb["ai"]["SWAIG"]["functions"]) {
        if (f.value("function", "") == name) {
          return f;
        }
      }
    }
  }
  return json(nullptr);
}

inline json ai_params(const json& swml) {
  for (const auto& verb : swml["sections"]["main"]) {
    if (verb.contains("ai")) {
      return verb["ai"].value("params", json::object());
    }
  }
  return json::object();
}

}  // namespace call_end_test

TEST(agent_on_call_end_registers_hangup_hook_and_post_conversation) {
  AgentBase agent("demo", "/demo");
  agent.on_call_end([](const json&, const json&) {});
  json swml = agent.render_swml();
  json hook = call_end_test::swaig_function(swml, "hangup_hook");
  ASSERT_TRUE(hook.is_object());
  ASSERT_EQ(hook["description"], json("Internal: fires when the call ends."));
  ASSERT_EQ(call_end_test::ai_params(swml)["swaig_post_conversation"], json(true));
  return true;
}

TEST(agent_on_call_end_leaves_explicit_false_alone) {
  AgentBase agent("demo", "/demo");
  agent.set_param("swaig_post_conversation", false);
  agent.on_call_end([](const json&, const json&) {});
  ASSERT_EQ(call_end_test::ai_params(agent.render_swml())["swaig_post_conversation"], json(false));
  return true;
}

TEST(agent_on_call_end_handlers_run_in_order_with_call_log_isolated) {
  AgentBase agent("demo", "/demo");
  std::vector<std::string> seen;
  agent.on_call_end([&seen](const json& log, const json&) {
    seen.push_back("first:" + std::to_string(log.size()));
    throw std::runtime_error("boom");
  });
  agent.on_call_end([&seen](const json& log, const json& raw) {
    seen.push_back("second:" + log[0]["content"].get<std::string>() + ":" +
                   raw["call_id"].get<std::string>());
  });
  json raw = {{"call_id", "c-1"},
              {"raw_call_log", json::array({{{"role", "user"}, {"content", "hi"}}})}};
  auto result = agent.on_function_call("hangup_hook", json::object(), raw);
  (void)result;
  ASSERT_EQ(seen.size(), static_cast<size_t>(2));
  ASSERT_EQ(seen[0], std::string("first:1"));
  ASSERT_EQ(seen[1], std::string("second:hi:c-1"));
  return true;
}

TEST(agent_add_per_call_config_runs_all_in_registration_order) {
  AgentBase agent("demo", "/demo");
  agent.set_prompt_text("Original");
  std::vector<int> order;
  agent.add_per_call_config([&order](const std::map<std::string, std::string>&, const json&,
                                     const std::map<std::string, std::string>&, AgentBase& copy) {
    order.push_back(1);
    copy.set_prompt_text("first");
  });
  agent.add_per_call_config([&order](const std::map<std::string, std::string>&, const json&,
                                     const std::map<std::string, std::string>&, AgentBase& copy) {
    order.push_back(2);
    // Sees what the earlier callback configured.
    copy.set_prompt_text(copy.get_prompt() + "+second");
  });
  json swml = agent.render_swml_for_request({}, json::object(), {});
  ASSERT_EQ(order, (std::vector<int>{1, 2}));
  ASSERT_EQ(agent.get_prompt(), std::string("Original"));
  ASSERT_TRUE(swml.dump().find("first+second") != std::string::npos);
  return true;
}

TEST(agent_set_dynamic_config_callback_replaces_the_chain) {
  AgentBase agent("demo", "/demo");
  std::vector<int> order;
  agent.add_per_call_config([&order](const std::map<std::string, std::string>&, const json&,
                                     const std::map<std::string, std::string>&,
                                     AgentBase&) { order.push_back(1); });
  agent.set_dynamic_config_callback([&order](const std::map<std::string, std::string>&, const json&,
                                             const std::map<std::string, std::string>&,
                                             AgentBase&) { order.push_back(2); });
  (void)agent.render_swml_for_request({}, json::object(), {});
  ASSERT_EQ(order, (std::vector<int>{2}));
  return true;
}

TEST(agent_mount_serves_extra_routes_under_prefix) {
  AgentBase agent("demo", "/demo");
  agent.mount(
      [](httplib::Server& server, const std::string& prefix) {
        server.Get(prefix + "/ping", [](const httplib::Request&, httplib::Response& res) {
          res.set_content("pong", "text/plain");
        });
      },
      "/chat/");
  auto router = agent.as_router();
  int port = router->bind_to_any_port("127.0.0.1");
  ASSERT_TRUE(port > 0);
  std::thread server_thread([&router]() { router->listen_after_bind(); });
  for (int i = 0; i < 100 && !router->is_running(); ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  std::string body;
  int status = 0;
  {
    httplib::Client cli("127.0.0.1", port);
    cli.set_connection_timeout(2, 0);
    auto res = cli.Get("/chat/ping");
    if (res) {
      status = res->status;
      body = res->body;
    }
  }
  router->stop();
  server_thread.join();
  ASSERT_EQ(status, 200);
  ASSERT_EQ(body, std::string("pong"));
  return true;
}
