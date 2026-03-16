// Copyright (c) 2025 SignalWire — MIT License
// Session state management with global data and callbacks.

#include <signalwire/agent/agent_base.hpp>

using namespace signalwire;
using json = nlohmann::json;

int main() {
    agent::AgentBase agent("stateful", "/stateful");

    agent.prompt_add_section("Role", "You are a stateful assistant that remembers context.");
    agent.prompt_add_section("Instructions", "", {
        "Track conversation topics in global data",
        "Use session tokens for secure tool calls"
    });

    agent.set_global_data({
        {"session_type", "demo"},
        {"interaction_count", 0}
    });

    // Summary callback
    agent.on_summary([](const json& summary, const json& raw) {
        (void)raw;
        std::cout << "Conversation summary: " << summary.dump(2) << "\n";
    });

    // Debug event callback
    agent.enable_debug_events(true);
    agent.on_debug_event([](const json& event) {
        std::cout << "Debug: " << event.dump() << "\n";
    });

    // Tool that updates state
    agent.define_tool("update_topic", "Track the current topic",
        {{"type", "object"}, {"properties", {
            {"topic", {{"type", "string"}, {"description", "Current topic"}}}
        }}},
        [](const json& args, const json& raw) -> swaig::FunctionResult {
            (void)raw;
            std::string topic = args.value("topic", "general");
            return swaig::FunctionResult("Topic updated to: " + topic)
                .update_global_data({{"current_topic", topic}})
                .set_metadata({{"last_topic_update", topic}});
        }, true /* secure */);

    std::cout << "Session state demo at http://0.0.0.0:3000/stateful\n";
    agent.run();
}
