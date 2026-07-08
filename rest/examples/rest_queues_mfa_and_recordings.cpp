// Copyright (c) 2025 SignalWire — MIT License
// REST: Queues, MFA, and recording management.

#include <signalwire/rest/rest_client.hpp>
#include <iostream>

using namespace signalwire::rest;
using json = nlohmann::json;

int main() {
    try {
        auto client = RestClient::from_env();

        // Create a queue
        auto queue = client.queues().create({
            {"name", "support-queue"}, {"max_size", 50}
        });
        std::cout << "Queue: " << queue.dump(2) << "\n";

        // List recordings
        auto recordings = client.recordings().list();
        std::cout << "Recordings: " << recordings.dump(2) << "\n";

        // MFA: send a verification code via SMS
        auto mfa = client.mfa().sms({
            .to = "+15551234567",
            .from = "+15559876543",
            .message = "Your code is {code}",
        });
        std::cout << "MFA request: " << mfa.dump(2) << "\n";

    } catch (const SignalWireRestError& e) {
        std::cerr << "Error " << e.status() << ": " << e.what() << "\n";
    }
}
