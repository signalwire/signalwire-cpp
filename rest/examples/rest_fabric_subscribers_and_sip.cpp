// Copyright (c) 2025 SignalWire — MIT License
// REST: Manage Fabric subscribers and SIP endpoints.

#include <signalwire/rest/signalwire_client.hpp>
#include <iostream>

using namespace signalwire::rest;
using json = nlohmann::json;

int main() {
    try {
        auto client = SignalWireClient::from_env();

        // Create a subscriber
        auto sub = client.fabric().subscribers.create({
            {"first_name", "John"}, {"last_name", "Doe"},
            {"email", "john@example.com"}
        });
        std::cout << "Subscriber: " << sub.dump(2) << "\n";

        // Create a SIP endpoint
        auto sip = client.fabric().sip_endpoints.create({
            {"name", "office-phone"}, {"username", "john"},
            {"password", "secure123"}
        });
        std::cout << "SIP endpoint: " << sip.dump(2) << "\n";

        // List endpoints
        auto endpoints = client.fabric().sip_endpoints.list();
        std::cout << "All SIP endpoints: " << endpoints.dump(2) << "\n";

    } catch (const SignalWireRestError& e) {
        std::cerr << "Error " << e.status() << ": " << e.what() << "\n";
    }
}
