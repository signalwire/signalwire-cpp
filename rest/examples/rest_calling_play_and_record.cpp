// Copyright (c) 2025 SignalWire — MIT License
// REST: Place a call, play audio, and record.

#include <iostream>
#include <signalwire/rest/rest_client.hpp>

using namespace signalwire::rest;
using json = nlohmann::json;

int main() {
  // exception-escape guard: main() must not let an exception escape
  // (that is std::terminate, with no message). Report and exit nonzero.
  try {
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

      // Play audio. Each call returns the API's response body — keep it; that is
      // where the control id you need to stop/inspect the action comes back.
      auto play = client.calling().play(
          call_id, {
                       .play = json::array({{{"type", "tts"},
                                             {"params", {{"text", "Recording will begin now."}}}}}),
                   });
      std::cout << "  Play: " << play.dump() << "\n";

      // Start recording
      auto recording = client.calling().record(
          call_id, {
                       .extras = {{"record", {{"stereo", true}, {"format", "wav"}}}},
                   });
      std::cout << "  Recording: " << recording.dump() << "\n";

      std::cout << "Playing and recording on call " << call_id << "\n";

    } catch (const SignalWireRestError& e) {
      std::cerr << "Error " << e.status_code() << ": " << e.what() << "\n";
    }
  } catch (const std::exception& e) {
    std::cerr << "fatal: " << e.what() << "\n";
    return 1;
  }
}
