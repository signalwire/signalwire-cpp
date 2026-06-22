// Copyright (c) 2025 SignalWire — MIT License
// REST: Twilio-compatible LAML endpoints.

#include <signalwire/rest/rest_client.hpp>
#include <iostream>

using namespace signalwire::rest;
using json = nlohmann::json;

int main() {
    try {
        auto client = RestClient::from_env();

        // Create a call via compat API
        std::cout << "Creating call via LAML compat...\n";
        auto call = client.compat().calls.create({
            {"To", "+15551234567"},
            {"From", "+15559876543"},
            {"Url", "https://example.com/twiml"}
        });
        std::cout << "  Call: " << call.dump(2) << "\n";

        // Send a message
        std::cout << "\nSending message via LAML compat...\n";
        auto msg = client.compat().messages.create({
            {"To", "+15551234567"},
            {"From", "+15559876543"},
            {"Body", "Hello from SignalWire!"}
        });
        std::cout << "  Message: " << msg.dump(2) << "\n";

        // List calls
        auto calls = client.compat().calls.list();
        std::cout << "\nCalls: " << calls.dump(2) << "\n";

    } catch (const SignalWireRestError& e) {
        std::cerr << "Error " << e.status() << ": " << e.what() << "\n";
    }
}
