// Copyright (c) 2025 SignalWire — MIT License
// REST: Search, purchase, and manage phone numbers.

#include <signalwire/rest/signalwire_client.hpp>
#include <iostream>

using namespace signalwire::rest;
using json = nlohmann::json;

int main() {
    try {
        auto client = SignalWireClient::from_env();

        // Search for numbers
        std::cout << "Searching numbers in area code 512...\n";
        auto available = client.phone_numbers().search({
            {"area_code", "512"}, {"max_results", "5"}
        });
        std::cout << "Available: " << available.dump(2) << "\n";

        // List owned numbers
        auto owned = client.phone_numbers().list();
        std::cout << "\nOwned numbers: " << owned.dump(2) << "\n";

    } catch (const SignalWireRestError& e) {
        std::cerr << "Error " << e.status() << ": " << e.what() << "\n";
    }
}
