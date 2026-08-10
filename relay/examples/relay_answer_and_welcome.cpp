// Copyright (c) 2025 SignalWire — MIT License
// RELAY: Answer an inbound call and play a TTS greeting.
// NOTE: Transport is stubbed; demonstrates the API surface.

#include <iostream>
#include <signalwire/relay/client.hpp>

using namespace signalwire::relay;

int main() {
  auto client = RelayClient::from_env();

  client.on_call([](Call& call) {
    std::cout << "Inbound call from " << call.from() << "\n";

    // Answer the call
    call.answer();

    // Play TTS greeting. wait() returns false if the action timed out instead of
    // completing — check it rather than assuming the greeting was heard.
    auto action =
        call.play({{{"type", "tts"},
                    {"params", {{"text", "Welcome to SignalWire! How can I help you today?"}}}}});
    if (!action.wait()) {
      std::cerr << "Greeting playback timed out\n";
    }

    // Hang up
    call.hangup();
    if (!call.wait_for_ended(10000)) {
      std::cerr << "Call did not reach the ended state within 10s\n";
    }
    std::cout << "Call ended\n";
  });

  std::cout << "Waiting for inbound calls...\n";
  client.run();
}
