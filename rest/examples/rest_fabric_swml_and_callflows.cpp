// Copyright (c) 2025 SignalWire — MIT License
// REST: Manage SWML scripts and call flows via Fabric API.

#include <signalwire/rest/signalwire_client.hpp>
#include <iostream>

using namespace signalwire::rest;
using json = nlohmann::json;

int main() {
    try {
        auto client = SignalWireClient::from_env();

        // Create a SWML script
        auto script = client.fabric().swml_scripts.create({
            {"name", "greeting-script"},
            {"content", {
                {"version", "1.0.0"},
                {"sections", {{"main", json::array({
                    {{"answer", json::object()}},
                    {{"play", {{"url", "https://example.com/greeting.mp3"}}}},
                    {{"hangup", json::object()}}
                })}}}
            }}
        });
        std::cout << "SWML script: " << script.dump(2) << "\n";

        // Create a call flow
        auto flow = client.fabric().call_flows.create({
            {"name", "main-flow"},
            {"steps", json::array({
                {{"type", "ai"}, {"prompt", "You are a helpful assistant."}}
            })}
        });
        std::cout << "Call flow: " << flow.dump(2) << "\n";

    } catch (const SignalWireRestError& e) {
        std::cerr << "Error " << e.status() << ": " << e.what() << "\n";
    }
}
