// Copyright (c) 2025 SignalWire — MIT License
// Comprehensive SWAIG features: all FunctionResult action types.

#include <signalwire/agent/agent_base.hpp>

using namespace signalwire;
using json = nlohmann::json;

int main() {
    agent::AgentBase agent("swaig-features", "/swaig-features");

    agent.prompt_add_section("Role", "You demonstrate all SWAIG function result actions.");

    // Tool: state management
    agent.define_tool("manage_state", "Update conversation state",
        {{"type", "object"}, {"properties", {
            {"key", {{"type", "string"}, {"description", "State key"}}},
            {"value", {{"type", "string"}, {"description", "State value"}}}
        }}},
        [](const json& args, const json& raw) -> swaig::FunctionResult {
            (void)raw;
            return swaig::FunctionResult("State updated")
                .update_global_data({{args.value("key", "k"), args.value("value", "v")}})
                .set_metadata({{"last_update", "now"}});
        });

    // Tool: media actions
    agent.define_tool("play_music", "Play background music",
        {{"type", "object"}, {"properties", {
            {"url", {{"type", "string"}, {"description", "Audio URL"}}}
        }}},
        [](const json& args, const json& raw) -> swaig::FunctionResult {
            (void)raw;
            return swaig::FunctionResult("Playing music")
                .play_background_file(args.value("url", ""), false);
        });

    // Tool: speech control
    agent.define_tool("adjust_speech", "Adjust speech settings",
        {{"type", "object"}, {"properties", json::object()}},
        [](const json& args, const json& raw) -> swaig::FunctionResult {
            (void)args; (void)raw;
            return swaig::FunctionResult("Settings adjusted")
                .set_end_of_speech_timeout(2000)
                .set_speech_event_timeout(5000)
                .add_dynamic_hints({{"hints", json::array({"SignalWire", "SWML"})}});
        });

    // Tool: context switching
    agent.define_tool("switch", "Switch conversation context",
        {{"type", "object"}, {"properties", {
            {"prompt", {{"type", "string"}, {"description", "New system prompt"}}}
        }}},
        [](const json& args, const json& raw) -> swaig::FunctionResult {
            (void)raw;
            return swaig::FunctionResult("Switching context")
                .switch_context(args.value("prompt", ""), "", true, false);
        });

    // Tool: SMS
    agent.define_tool("send_text", "Send an SMS",
        {{"type", "object"}, {"properties", {
            {"to", {{"type", "string"}, {"description", "Phone number"}}},
            {"message", {{"type", "string"}, {"description", "Text message"}}}
        }}},
        [](const json& args, const json& raw) -> swaig::FunctionResult {
            (void)raw;
            return swaig::FunctionResult("Text sent")
                .send_sms(args.value("to", ""), "+15559876543", args.value("message", ""));
        });

    std::cout << "SWAIG features at http://0.0.0.0:3000/swaig-features\n";
    agent.run();
}
