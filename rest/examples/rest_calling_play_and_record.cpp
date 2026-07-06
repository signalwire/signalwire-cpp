// Copyright (c) 2025 SignalWire — MIT License
// REST: Place a call, play audio, and record.

#include <signalwire/rest/rest_client.hpp>
#include <iostream>

using namespace signalwire::rest;
using json = nlohmann::json;

int main() {
    try {
        auto client = RestClient::from_env();

        // Dial
        auto call = client.calling().dial({
            .from = "+15559876543",
            .to = "+15551234567",
            .url = "https://example.com/handler",
        });
        std::string call_id = call.value("call_id", "");
        std::cout << "Call ID: " << call_id << "\n";

        // Play audio
        client.calling().play(call_id, {
            .play = json::array({{{"type", "tts"},
                                  {"params", {{"text", "Recording will begin now."}}}}}),
        });

        // Start recording
        client.calling().record(call_id, {
            .extras = {{"record", {{"stereo", true}, {"format", "wav"}}}},
        });

        std::cout << "Playing and recording on call " << call_id << "\n";

    } catch (const SignalWireRestError& e) {
        std::cerr << "Error " << e.status() << ": " << e.what() << "\n";
    }
}
