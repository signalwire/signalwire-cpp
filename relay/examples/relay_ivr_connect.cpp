// Copyright (c) 2025 SignalWire — MIT License
// RELAY: IVR with DTMF collection and call connect.
// NOTE: Transport is stubbed; demonstrates the API surface.

#include <signalwire/relay/client.hpp>
#include <iostream>

using namespace signalwire::relay;
using json = nlohmann::json;

int main() {
    auto client = RelayClient::from_env();

    client.on_call([](Call& call) {
        std::cout << "Inbound call: " << call.call_id() << "\n";
        call.answer();

        // Play IVR menu
        auto menu = call.play({
            {{"type", "tts"}, {"params", {{"text",
                "Press 1 for sales, 2 for support, or 3 for billing."}}}}
        });
        menu.wait();

        // Collect DTMF
        auto collect = call.collect({
            {"digits", {{"max", 1}, {"terminators", "#"}}},
            {"speech", {{"hints", json::array({"sales", "support", "billing"})}}},
            {"initial_timeout", 5.0},
            {"partial_results", true}
        });
        collect.wait();

        // Route based on input (stub: always route to sales)
        std::cout << "Routing call...\n";
        auto connect_action = call.connect({{
            {{"type", "phone"}, {"params", {{"to_number", "+15551001"}}}}
        }});
        connect_action.wait();

        call.hangup();
        std::cout << "Call ended\n";
    });

    std::cout << "IVR demo waiting for calls...\n";
    client.run();
}
