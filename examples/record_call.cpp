// Copyright (c) 2025 SignalWire — MIT License
// Record call demo: tools that start/stop call recording.

#include <signalwire/agent/agent_base.hpp>

using namespace signalwire;
using json = nlohmann::json;

int main() {
    agent::AgentBase agent("recorder", "/recorder");

    agent.prompt_add_section("Role", "You are an agent that can record calls.");
    agent.prompt_add_section("Instructions", "", {
        "Always ask for consent before recording",
        "Use start_recording to begin and stop_recording to end"
    });

    agent.define_tool("start_recording", "Start recording the call",
        {{"type", "object"}, {"properties", json::object()}},
        [](const json& args, const json& raw) -> swaig::FunctionResult {
            (void)args; (void)raw;
            return swaig::FunctionResult("Recording started.")
                .record_call("rec-001", true, "wav", "both", "", false, 44.0);
        });

    agent.define_tool("stop_recording", "Stop recording the call",
        {{"type", "object"}, {"properties", json::object()}},
        [](const json& args, const json& raw) -> swaig::FunctionResult {
            (void)args; (void)raw;
            return swaig::FunctionResult("Recording stopped.")
                .stop_record_call("rec-001");
        });

    std::cout << "Record call demo at http://0.0.0.0:3000/recorder\n";
    agent.run();
}
