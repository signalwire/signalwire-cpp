// Copyright (c) 2025 SignalWire — MIT License
// Kubernetes-ready agent with health checks and graceful shutdown.

#include <csignal>
#include <iostream>
#include <signalwire/agent/agent_base.hpp>
#include <stdexcept>
#include <string>

using namespace signalwire;
using json = nlohmann::json;

static agent::AgentBase* g_agent = nullptr;

void signal_handler(int sig) {
  (void)sig;
  if (g_agent) {
    g_agent->stop();
  }
}

int main() {
  // exception-escape guard: main() must not let an exception escape
  // (that is std::terminate, with no message). Report and exit nonzero.
  try {
    // Read port from env (Kubernetes can set this)
    int port = 3000;
    const char* port_env = std::getenv("PORT");
    if (port_env) {
      // atoi() reports NOTHING on a non-numeric value -- it just returns 0,
      // so PORT=abc used to bind port 0. Parse it properly and say so.
      try {
        port = std::stoi(port_env);
      } catch (const std::exception&) {
        std::cerr << "PORT=\"" << port_env << "\" is not a number; using " << port << "\n";
      }
    }

    agent::AgentBase agent("k8s-agent", "/", "0.0.0.0", port);
    g_agent = &agent;

    agent.prompt_add_section("Role", "You are a production-ready assistant.");
    agent.set_params({{"ai_model", "gpt-4.1-nano"}});
    agent.enable_debug_routes(true);

    // Health check tool
    agent.define_tool("health_check", "Check agent health",
                      {{"type", "object"}, {"properties", json::object()}},
                      [](const json& args, const json& raw) -> swaig::FunctionResult {
                        (void)args;
                        (void)raw;
                        return swaig::FunctionResult("Agent is healthy and running.");
                      });

    // Graceful shutdown
    std::signal(SIGTERM, signal_handler);
    std::signal(SIGINT, signal_handler);

    std::cout << "Kubernetes-ready agent at http://0.0.0.0:" << port << "/\n";
    agent.run();
  } catch (const std::exception& e) {
    std::cerr << "fatal: " << e.what() << "\n";
    return 1;
  }
}
