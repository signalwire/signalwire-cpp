// Copyright (c) 2025 SignalWire — MIT License
// Agent with a custom HTTP route path.

#include <signalwire/agent/agent_base.hpp>

using namespace signalwire;

int main() {
    agent::AgentBase agent("custom-path", "/api/v2/my-custom-agent", "0.0.0.0", 8080);

    agent.prompt_add_section("Role", "You are an agent at a custom path.");
    agent.set_params({{"ai_model", "gpt-4.1-nano"}});

    agent.define_tool("ping", "Respond with pong",
        {{"type", "object"}, {"properties", nlohmann::json::object()}},
        [](const nlohmann::json& args, const nlohmann::json& raw) -> swaig::FunctionResult {
            (void)args; (void)raw;
            return swaig::FunctionResult("pong!");
        });

    std::cout << "Custom path agent at http://0.0.0.0:8080/api/v2/my-custom-agent\n";
    agent.run();
}
