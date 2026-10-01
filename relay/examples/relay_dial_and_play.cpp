// Copyright (c) 2025 SignalWire — MIT License
// RELAY: Dial an outbound call and play TTS.
// NOTE: Transport is stubbed; demonstrates the API surface.

#include <cstdlib>
#include <iostream>
#include <signalwire/relay/client.hpp>

using namespace signalwire::relay;
using json = nlohmann::json;

int main() {
  // exception-escape guard: main() must not let an exception escape
  // (that is std::terminate, with no message). Report and exit nonzero.
  try {
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
    json devices = {{{{"type", "phone"},
                      {"params", {{"to_number", to_number}, {"from_number", from_number}}}}}};
    Call call = client.dial(devices);
    std::cout << "Dialing " << to_number << " — call_id: " << call.call_id() << "\n";

    // Play TTS. wait() returns false on timeout rather than completion, so check
    // it — otherwise "Playback finished" prints even when it did not.
    auto action = call.play({{{"type", "tts"}, {"params", {{"text", "Hello from SignalWire!"}}}}});
    if (action.wait()) {
      std::cout << "Playback finished\n";
    } else {
      std::cerr << "Playback timed out\n";
    }

    call.hangup();
    if (!call.wait_for_ended()) {
      std::cerr << "Call did not reach the ended state before the timeout\n";
    }
    std::cout << "Call ended\n";

    client.disconnect();
  } catch (const std::exception& e) {
    std::cerr << "fatal: " << e.what() << "\n";
    return 1;
  }
}
