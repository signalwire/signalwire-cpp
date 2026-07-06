// Copyright (c) 2025 SignalWire — MIT License
// REST: IVR with collect and AI integration.

#include <signalwire/rest/rest_client.hpp>
#include <iostream>

using namespace signalwire::rest;
using json = nlohmann::json;

int main() {
    try {
        auto client = RestClient::from_env();

        auto call = client.calling().dial({
            .from = "+15559876543",
            .to = "+15551234567",
            .url = "https://example.com/handler",
        });
        std::string call_id = call.value("call_id", "");

        // Collect DTMF
        auto collected = client.calling().collect(call_id, {
            .initial_timeout = 10,
            .digits = json{{"max", 1}, {"terminators", "#"}},
        });
        std::cout << "Collected: " << collected.dump() << "\n";

        // Detect answering machine
        auto detect = client.calling().detect(call_id, {
            .detect = {{"type", "machine"}},
            .timeout = 30,
        });
        std::cout << "Detection: " << detect.dump() << "\n";

    } catch (const SignalWireRestError& e) {
        std::cerr << "Error " << e.status() << ": " << e.what() << "\n";
    }
}
