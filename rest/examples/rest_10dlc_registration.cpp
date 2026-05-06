// Copyright (c) 2025 SignalWire — MIT License
// REST: 10DLC registration workflow.

#include <signalwire/rest/rest_client.hpp>
#include <iostream>

using namespace signalwire::rest;
using json = nlohmann::json;

int main() {
    try {
        auto client = RestClient::from_env();

        // Check verified callers
        auto callers = client.verified_callers().list();
        std::cout << "Verified callers: " << callers.dump(2) << "\n";

        // Registry entries (10DLC brand registrations)
        auto registry = client.registry().brands.list();
        std::cout << "Registry brands: " << registry.dump(2) << "\n";

        // Number groups
        auto groups = client.number_groups().list();
        std::cout << "Number groups: " << groups.dump(2) << "\n";

    } catch (const SignalWireRestError& e) {
        std::cerr << "Error " << e.status() << ": " << e.what() << "\n";
    }
}
