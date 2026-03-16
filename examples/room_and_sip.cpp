// Copyright (c) 2025 SignalWire — MIT License
// Room/SIP demo: join video rooms and SIP refer.

#include <signalwire/agent/agent_base.hpp>

using namespace signalwire;
using json = nlohmann::json;

int main() {
    agent::AgentBase agent("room-sip", "/room-sip");

    agent.prompt_add_section("Role", "You manage video rooms and SIP routing.");
    agent.enable_sip_routing(true);
    agent.register_sip_username("conference");

    agent.define_tool("join_meeting", "Join a video room",
        {{"type", "object"}, {"properties", {
            {"room", {{"type", "string"}, {"description", "Room name"}}}
        }}},
        [](const json& args, const json& raw) -> swaig::FunctionResult {
            (void)raw;
            std::string room = args.value("room", "default");
            return swaig::FunctionResult("Joining room: " + room)
                .join_room(room);
        });

    agent.define_tool("sip_transfer", "Transfer via SIP REFER",
        {{"type", "object"}, {"properties", {
            {"uri", {{"type", "string"}, {"description", "SIP URI"}}}
        }}},
        [](const json& args, const json& raw) -> swaig::FunctionResult {
            (void)raw;
            std::string uri = args.value("uri", "");
            return swaig::FunctionResult("Transferring via SIP to " + uri)
                .sip_refer(uri);
        });

    agent.define_tool("start_conference", "Start a conference call",
        {{"type", "object"}, {"properties", {
            {"name", {{"type", "string"}, {"description", "Conference name"}}}
        }}},
        [](const json& args, const json& raw) -> swaig::FunctionResult {
            (void)raw;
            std::string name = args.value("name", "default-conf");
            return swaig::FunctionResult("Starting conference: " + name)
                .join_conference(name, false, "true");
        });

    std::cout << "Room/SIP demo at http://0.0.0.0:3000/room-sip\n";
    agent.run();
}
