// Copyright (c) 2025 SignalWire — MIT License
// Kubernetes-ready agent with health checks and graceful shutdown.

#include <signalwire/agent/agent_base.hpp>
#include <csignal>

using namespace signalwire;
using json = nlohmann::json;

static agent::AgentBase* g_agent = nullptr;

void signal_handler(int sig) {
    (void)sig;
    if (g_agent) g_agent->stop();
}

int main() {
    // Read port from env (Kubernetes can set this)
    int port = 3000;
    const char* port_env = std::getenv("PORT");
    if (port_env) port = std::atoi(port_env);

    agent::AgentBase agent("k8s-agent", "/", "0.0.0.0", port);
    g_agent = &agent;

    agent.prompt_add_section("Role", "You are a production-ready assistant.");
    agent.set_params({{"ai_model", "gpt-4.1-nano"}});
    agent.enable_debug_routes(true);

    // Health check tool
    agent.define_tool("health_check", "Check agent health",
        {{"type", "object"}, {"properties", json::object()}},
        [](const json& args, const json& raw) -> swaig::FunctionResult {
            (void)args; (void)raw;
            return swaig::FunctionResult("Agent is healthy and running.");
        });

    // Graceful shutdown
    std::signal(SIGTERM, signal_handler);
    std::signal(SIGINT, signal_handler);

    std::cout << "Kubernetes-ready agent at http://0.0.0.0:" << port << "/\n";
    agent.run();
}
