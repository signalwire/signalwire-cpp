// Copyright (c) 2025 SignalWire — MIT License
// RELAY client demo: answer inbound calls and play TTS.

#include <signalwire/relay/client.hpp>
#include <iostream>

using namespace signalwire::relay;

int main() {
    auto client = RelayClient::from_env();

    client.on_call([](Call& call) {
        std::cout << "Inbound call: " << call.call_id() << "\n";
        call.answer();

        auto action = call.play({
            {{"type", "tts"}, {"params", {{"text", "Welcome to SignalWire!"}}}}
        });
        if (!action.wait()) {
            std::cout << "playback interrupted (call ended early)\n";
        }

        call.hangup();
        std::cout << "Call ended\n";
    });

    std::cout << "RELAY demo running\n";
    std::cout << "Set SIGNALWIRE_PROJECT_ID, SIGNALWIRE_API_TOKEN, SIGNALWIRE_SPACE\n";
    client.run();
}
