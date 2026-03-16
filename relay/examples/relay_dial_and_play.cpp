// Copyright (c) 2025 SignalWire — MIT License
// RELAY: Dial an outbound call and play TTS.
// NOTE: Transport is stubbed; demonstrates the API surface.

#include <signalwire/relay/client.hpp>
#include <cstdlib>
#include <iostream>

using namespace signalwire::relay;
using json = nlohmann::json;

int main() {
    const char* from_env = std::getenv("RELAY_FROM_NUMBER");
    const char* to_env = std::getenv("RELAY_TO_NUMBER");
    if (!from_env || !to_env) {
        std::cerr << "Set RELAY_FROM_NUMBER and RELAY_TO_NUMBER\n";
        return 1;
    }
    std::string from_number = from_env;
    std::string to_number = to_env;

    auto client = RelayClient::from_env();
    client.connect();
    std::cout << "Connected\n";

    // Dial
    json devices = {{
        {{"type", "phone"}, {"params", {{"to_number", to_number}, {"from_number", from_number}}}}
    }};
    Call call = client.dial(devices);
    std::cout << "Dialing " << to_number << " — call_id: " << call.call_id() << "\n";

    // Play TTS
    auto action = call.play({
        {{"type", "tts"}, {"params", {{"text", "Hello from SignalWire!"}}}}
    });
    action.wait();
    std::cout << "Playback finished\n";

    call.hangup();
    call.wait_for_ended();
    std::cout << "Call ended\n";

    client.disconnect();
}
