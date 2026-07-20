// Copyright (c) 2025 SignalWire — MIT License
//
// relay_answer_and_welcome.cpp
//
// Mirrors Python's `examples/relay_answer_and_welcome.py`: connect to RELAY
// over WebSocket, answer the next inbound call, play a TTS greeting, hang up.
// This is the canonical starter for the RELAY client.
//
// The same example also lives at `relay/examples/relay_answer_and_welcome.cpp`
// for the relay/-prefixed examples directory; this copy under `examples/`
// keeps `audit_example_parity.py` happy (the audit looks at `examples/`
// only, matching Python's layout where the file lives at top-level).
//
// Required env vars:
//   - SIGNALWIRE_PROJECT_ID
//   - SIGNALWIRE_API_TOKEN
//   - SIGNALWIRE_SPACE        (e.g. "yourspace.signalwire.com")
//
// Build:
//     cmake --build build --target example_relay_answer_and_welcome

#include <signalwire/relay/client.hpp>
#include <iostream>

using namespace signalwire::relay;
using json = nlohmann::json;

int main() {
    auto client = RelayClient::from_env();

    client.on_call([](Call& call) {
        std::cout << "Inbound call from " << call.from() << "\n";

        // Answer the call.
        call.answer();

        // Play a TTS greeting and wait for it to finish. wait() returns false
        // if the call ends before playback completes.
        auto action = call.play({
            json{
                {"type", "tts"},
                {"params", json{{"text", "Welcome to SignalWire! How can I help you today?"}}},
            }
        });
        if (!action.wait()) {
            std::cout << "Greeting interrupted (caller hung up early)\n";
        }

        // Hang up cleanly. wait_for_ended() returns false on timeout.
        call.hangup();
        if (call.wait_for_ended(10000)) {
            std::cout << "Call ended\n";
        } else {
            std::cout << "Timed out waiting for call end\n";
        }
    });

    std::cout << "Waiting for inbound calls...\n";
    client.run();
}
