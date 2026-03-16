// Copyright (c) 2025 SignalWire — MIT License
// REST client demo: manage resources, place calls.

#include <signalwire/rest/signalwire_client.hpp>
#include <iostream>

using namespace signalwire::rest;
using json = nlohmann::json;

int main() {
    try {
        auto client = SignalWireClient::from_env();

        // List AI agents
        std::cout << "Listing AI agents...\n";
        auto agents = client.fabric().agents.list();
        std::cout << "  Found: " << agents.dump(2) << "\n";

        // Search phone numbers
        std::cout << "\nSearching phone numbers...\n";
        auto numbers = client.phone_numbers().search({{"area_code", "512"}});
        std::cout << "  Results: " << numbers.dump(2) << "\n";

        // Place a test call
        std::cout << "\nPlacing test call...\n";
        auto result = client.calling().dial({
            {"to", "+15551234567"},
            {"from", "+15559876543"},
            {"url", "https://example.com/handler"}
        });
        std::cout << "  Call: " << result.dump(2) << "\n";

    } catch (const SignalWireRestError& e) {
        std::cerr << "REST error " << e.status() << ": " << e.what() << "\n";
    }
}
