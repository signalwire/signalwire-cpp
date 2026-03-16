// Copyright (c) 2025 SignalWire — MIT License
// REST: IVR with collect and AI integration.

#include <signalwire/rest/signalwire_client.hpp>
#include <iostream>

using namespace signalwire::rest;
using json = nlohmann::json;

int main() {
    try {
        auto client = SignalWireClient::from_env();

        auto call = client.calling().dial({
            {"to", "+15551234567"}, {"from", "+15559876543"},
            {"url", "https://example.com/handler"}
        });
        std::string call_id = call.value("call_id", "");

        // Collect DTMF
        auto collected = client.calling().collect(call_id, {
            {"digits", {{"max", 1}, {"terminators", "#"}}},
            {"initial_timeout", 10}
        });
        std::cout << "Collected: " << collected.dump() << "\n";

        // Detect answering machine
        auto detect = client.calling().detect(call_id, {
            {"type", "machine"}, {"timeout", 30}
        });
        std::cout << "Detection: " << detect.dump() << "\n";

    } catch (const SignalWireRestError& e) {
        std::cerr << "Error " << e.status() << ": " << e.what() << "\n";
    }
}
