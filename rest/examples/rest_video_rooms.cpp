// Copyright (c) 2025 SignalWire — MIT License
// REST: Manage video rooms, sessions, and recordings.

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

      // Create a video room
      auto room = client.video().rooms.create(
          {{"name", "team-meeting"}, {"max_members", 10}, {"quality", "1080p"}});
      std::cout << "Room: " << room.dump(2) << "\n";

      // List rooms
      auto rooms = client.video().rooms.list();
      std::cout << "All rooms: " << rooms.dump(2) << "\n";

      // List recordings
      auto recordings = client.video().room_recordings.list();
      std::cout << "Recordings: " << recordings.dump(2) << "\n";

    } catch (const SignalWireRestError& e) {
      std::cerr << "Error " << e.status_code() << ": " << e.what() << "\n";
    }
  } catch (const std::exception& e) {
    std::cerr << "fatal: " << e.what() << "\n";
    return 1;
  }
}
