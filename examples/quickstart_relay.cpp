// Copyright (c) 2025 SignalWire — MIT License
//
// quickstart_relay.cpp — the README "RELAY Client" quickstart, compiled.
//
// The `relay` region below is included byte-for-byte into README.md by the
// README-INCLUDE gate, so the doc code can never drift from working code.

// region: relay
#include <signalwire/relay/client.hpp>

#include <iostream>

using namespace signalwire::relay;

int main() {
    auto client = RelayClient::from_env();

    client.on_call([](Call& call) {
        call.answer();
        auto action = call.play({
            {{"type", "tts"}, {"params", {{"text", "Welcome to SignalWire!"}}}}
        });
        if (!action.wait()) {  // false = call ended before playback finished
            std::cerr << "playback interrupted\n";
        }
        call.hangup();
    });

    client.run();
}
// endregion: relay
