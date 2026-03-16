// Copyright (c) 2025 SignalWire — MIT License
// RELAY: Answer an inbound call and play a TTS greeting.
// NOTE: Transport is stubbed; demonstrates the API surface.

#include <signalwire/relay/client.hpp>
#include <iostream>

using namespace signalwire::relay;

int main() {
    auto client = RelayClient::from_env();

    client.on_call([](Call& call) {
        std::cout << "Inbound call from " << call.from() << "\n";

        // Answer the call
        call.answer();

        // Play TTS greeting
        auto action = call.play({
            {{"type", "tts"}, {"params", {{"text", "Welcome to SignalWire! How can I help you today?"}}}}
        });
        action.wait();

        // Hang up
        call.hangup();
        call.wait_for_ended(10000);
        std::cout << "Call ended\n";
    });

    std::cout << "Waiting for inbound calls...\n";
    client.run();
}
