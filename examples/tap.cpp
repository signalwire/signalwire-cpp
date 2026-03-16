// Copyright (c) 2025 SignalWire — MIT License
// Tap demo: stream call audio to an external URI.

#include <signalwire/agent/agent_base.hpp>

using namespace signalwire;
using json = nlohmann::json;

int main() {
    agent::AgentBase agent("tap-demo", "/tap-demo");

    agent.prompt_add_section("Role", "You can tap call audio for monitoring.");

    agent.define_tool("start_tap", "Start audio tap",
        {{"type", "object"}, {"properties", {
            {"uri", {{"type", "string"}, {"description", "RTP destination URI"}}}
        }}},
        [](const json& args, const json& raw) -> swaig::FunctionResult {
            (void)raw;
            std::string uri = args.value("uri", "rtp://monitor.example.com:5000");
            return swaig::FunctionResult("Tap started to " + uri)
                .tap(uri, "tap-001", "both", "PCMU", 20);
        });

    agent.define_tool("stop_tap", "Stop audio tap",
        {{"type", "object"}, {"properties", json::object()}},
        [](const json& args, const json& raw) -> swaig::FunctionResult {
            (void)args; (void)raw;
            return swaig::FunctionResult("Tap stopped")
                .stop_tap("tap-001");
        });

    std::cout << "Tap demo at http://0.0.0.0:3000/tap-demo\n";
    agent.run();
}
