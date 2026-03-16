// Copyright (c) 2025 SignalWire — MIT License
// REST: Upload a document and run a semantic search via Datasphere.

#include <signalwire/rest/signalwire_client.hpp>
#include <iostream>

using namespace signalwire::rest;
using json = nlohmann::json;

int main() {
    try {
        auto client = SignalWireClient::from_env();

        // Create a document
        std::cout << "Creating document...\n";
        auto doc = client.datasphere().documents.create({
            {"name", "product-docs"},
            {"content", "SignalWire AI Agents SDK enables building voice AI applications."}
        });
        std::cout << "  Document: " << doc.dump(2) << "\n";

        // Search
        std::cout << "\nSearching...\n";
        auto results = client.datasphere().search({
            {"query", "How to build AI agents?"},
            {"limit", 5}
        });
        std::cout << "  Results: " << results.dump(2) << "\n";

    } catch (const SignalWireRestError& e) {
        std::cerr << "Error " << e.status() << ": " << e.what() << "\n";
    }
}
