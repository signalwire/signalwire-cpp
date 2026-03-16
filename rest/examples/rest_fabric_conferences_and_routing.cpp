// Copyright (c) 2025 SignalWire — MIT License
// REST: Manage Fabric conferences and routing rules.

#include <signalwire/rest/signalwire_client.hpp>
#include <iostream>

using namespace signalwire::rest;
using json = nlohmann::json;

int main() {
    try {
        auto client = SignalWireClient::from_env();

        // Create a conference
        auto conf = client.fabric().conferences.create({
            {"name", "team-standup"},
            {"max_members", 10}
        });
        std::cout << "Conference: " << conf.dump(2) << "\n";

        // Create routing rules
        auto route = client.fabric().routing.create({
            {"name", "sales-route"},
            {"pattern", "+1512*"},
            {"destination", "sip:sales@example.com"}
        });
        std::cout << "Route: " << route.dump(2) << "\n";

        // List routing rules
        auto routes = client.fabric().routing.list();
        std::cout << "All routes: " << routes.dump(2) << "\n";

    } catch (const SignalWireRestError& e) {
        std::cerr << "Error " << e.status() << ": " << e.what() << "\n";
    }
}
