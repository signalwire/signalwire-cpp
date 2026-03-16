// Copyright (c) 2025 SignalWire — MIT License
// Call flow with verb pipeline (pre-answer, answer, post-answer, post-AI).

#include <signalwire/agent/agent_base.hpp>

using namespace signalwire;
using json = nlohmann::json;

int main() {
    agent::AgentBase agent("call-flow", "/call-flow");

    agent.prompt_add_section("Role", "You are a call center agent.");

    // 5-phase verb pipeline
    agent.add_pre_answer_verb("play", {{"url", "https://example.com/ringtone.mp3"}});
    agent.add_answer_verb("answer", {{"max_duration", 3600}});
    agent.add_post_answer_verb("record_call", {
        {"stereo", true}, {"format", "wav"}
    });
    agent.add_post_ai_verb("hangup", json::object());

    // Tools with call control actions
    agent.define_tool("transfer_call", "Transfer to another number",
        {{"type", "object"}, {"properties", {
            {"number", {{"type", "string"}, {"description", "Phone number"}}}
        }}},
        [](const json& args, const json& raw) -> swaig::FunctionResult {
            (void)raw;
            std::string num = args.value("number", "");
            return swaig::FunctionResult("Transferring to " + num)
                .connect(num, true);
        });

    agent.define_tool("end_call", "End the current call",
        {{"type", "object"}, {"properties", json::object()}},
        [](const json& args, const json& raw) -> swaig::FunctionResult {
            (void)args; (void)raw;
            return swaig::FunctionResult("Goodbye!").hangup();
        });

    std::cout << "Call flow demo at http://0.0.0.0:3000/call-flow\n";
    agent.run();
}
