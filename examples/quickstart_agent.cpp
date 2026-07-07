// Copyright (c) 2025 SignalWire — MIT License
//
// quickstart_agent.cpp — the README "AI Agents" quickstart, compiled.
//
// The `agent` region below is included byte-for-byte into README.md by the
// README-INCLUDE gate, so the doc code can never drift from working code.

// region: agent
#include <signalwire/agent/agent_base.hpp>
#include <ctime>

using namespace signalwire;
using json = nlohmann::json;

class MyAgent : public agent::AgentBase {
public:
    MyAgent() : AgentBase("my-agent", "/agent") {
        add_language({"English", "en-US", "inworld.Mark"});
        prompt_add_section("Role", "You are a helpful assistant.");

        define_tool("get_time", "Get the current time",
            {{"type", "object"}, {"properties", json::object()}},
            [](const json& /*args*/, const json& /*raw*/) -> swaig::FunctionResult {
                auto now = std::time(nullptr);
                char buf[32];
                std::strftime(buf, sizeof(buf), "%H:%M:%S", std::localtime(&now));
                return swaig::FunctionResult(std::string("The time is ") + buf);
            });
    }
};

int main() {
    MyAgent agent;
    agent.run();  // Serves on http://0.0.0.0:3000/agent
}
// endregion: agent
