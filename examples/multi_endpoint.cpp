// Copyright (c) 2025 SignalWire — MIT License
// Multi-endpoint agent with multiple webhook URLs and query params.

#include <signalwire/agent/agent_base.hpp>

using namespace signalwire;
using json = nlohmann::json;

int main() {
    agent::AgentBase agent("multi-endpoint", "/multi-endpoint");

    agent.prompt_add_section("Role", "You are a multi-endpoint demo agent.");

    // Manual webhook and proxy URLs
    agent.manual_set_proxy_url("https://public.example.com");
    agent.set_webhook_url("https://public.example.com/multi-endpoint/swaig");

    // SWAIG query params (appended to all tool webhook URLs)
    agent.add_swaig_query_param("tenant_id", "acme-corp");
    agent.add_swaig_query_param("env", "production");

    // Function includes from remote servers
    agent.add_function_include({
        {"url", "https://tools.example.com/functions"},
        {"functions", {"translate", "summarize"}}
    });

    agent.define_tool("local_tool", "A locally-handled tool",
        {{"type", "object"}, {"properties", {
            {"input", {{"type", "string"}, {"description", "Input text"}}}
        }}},
        [](const json& args, const json& raw) -> swaig::FunctionResult {
            (void)raw;
            return swaig::FunctionResult("Processed: " + args.value("input", ""));
        });

    std::cout << "Multi-endpoint at http://0.0.0.0:3000/multi-endpoint\n";
    agent.run();
}
