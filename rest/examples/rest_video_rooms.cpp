// Copyright (c) 2025 SignalWire — MIT License
// REST: Manage video rooms, sessions, and recordings.

#include <signalwire/rest/signalwire_client.hpp>
#include <iostream>

using namespace signalwire::rest;
using json = nlohmann::json;

int main() {
    try {
        auto client = SignalWireClient::from_env();

        // Create a video room
        auto room = client.video().rooms.create({
            {"name", "team-meeting"},
            {"max_members", 10},
            {"quality", "1080p"}
        });
        std::cout << "Room: " << room.dump(2) << "\n";

        // List rooms
        auto rooms = client.video().rooms.list();
        std::cout << "All rooms: " << rooms.dump(2) << "\n";

        // List recordings
        auto recordings = client.video().recordings.list();
        std::cout << "Recordings: " << recordings.dump(2) << "\n";

    } catch (const SignalWireRestError& e) {
        std::cerr << "Error " << e.status() << ": " << e.what() << "\n";
    }
}
