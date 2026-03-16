// Copyright (c) 2025 SignalWire — MIT License
// REST: Create an AI agent, assign a phone number, place a test call.

#include <signalwire/rest/signalwire_client.hpp>
#include <iostream>

using namespace signalwire::rest;
using json = nlohmann::json;

int main() {
    try {
        auto client = SignalWireClient::from_env();

        // Create an AI agent
        std::cout << "Creating AI agent...\n";
        auto agent = client.fabric().agents.create({
            {"name", "Demo Support Bot"},
            {"prompt", {{"text", "You are a friendly support agent."}}}
        });
        std::string agent_id = agent.value("id", "");
        std::cout << "  Created: " << agent_id << "\n";

        // List agents
        auto agents = client.fabric().agents.list();
        std::cout << "  Total agents: " << agents.dump() << "\n";

        // Search phone numbers
        auto numbers = client.phone_numbers().search({{"area_code", "512"}, {"max_results", "3"}});
        std::cout << "  Available numbers: " << numbers.dump() << "\n";

        // Place a test call
        auto call = client.calling().dial({
            {"to", "+15551234567"}, {"from", "+15559876543"},
            {"url", "https://example.com/handler"}
        });
        std::cout << "  Call: " << call.dump() << "\n";

        // Cleanup
        client.fabric().agents.del(agent_id);
        std::cout << "  Deleted agent\n";

    } catch (const SignalWireRestError& e) {
        std::cerr << "Error " << e.status() << ": " << e.what() << "\n";
    }
}
