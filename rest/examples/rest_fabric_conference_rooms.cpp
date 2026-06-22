// Copyright (c) 2025 SignalWire — MIT License
// REST: Manage Fabric conference rooms.

#include <signalwire/rest/rest_client.hpp>
#include <iostream>

using namespace signalwire::rest;
using json = nlohmann::json;

int main() {
    try {
        auto client = RestClient::from_env();

        // Create a conference room
        auto conf = client.fabric().conference_rooms.create({
            {"name", "team-standup"},
            {"max_members", 10}
        });
        std::cout << "Conference room: " << conf.dump(2) << "\n";
        std::string room_id = conf.value("id", "");

        // List conference rooms
        auto rooms = client.fabric().conference_rooms.list();
        std::cout << "All rooms: " << rooms.dump(2) << "\n";

        // List the addresses attached to the room
        if (!room_id.empty()) {
            auto addresses = client.fabric().conference_rooms.list_addresses(room_id);
            std::cout << "Room addresses: " << addresses.dump(2) << "\n";
        }

    } catch (const SignalWireRestError& e) {
        std::cerr << "Error " << e.status() << ": " << e.what() << "\n";
    }
}
