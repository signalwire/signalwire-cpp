// Copyright (c) 2025 SignalWire — MIT License
//
// quickstart_rest.cpp — the README "REST Client" quickstart, compiled.
//
// The `rest` region below is included byte-for-byte into README.md by the
// README-INCLUDE gate, so the doc code can never drift from working code.
// dial() and documents.search() take typed params structs (designated
// initializers), not JSON maps — the region is the real, compiled call shape.

// region: rest
#include <signalwire/rest/rest_client.hpp>

using namespace signalwire::rest;
using json = nlohmann::json;

int main() {
    auto client = RestClient::from_env();

    auto agents = client.fabric().ai_agents.list();
    auto call   = client.calling().dial({
        .from = "+15559876543", .to = "+15551234567",
        .url = "https://example.com/handler",
    });
    auto numbers = client.phone_numbers().search({{"areacode", "512"}});
    auto results = client.datasphere().documents.search({
        .query_string = "billing policy",
    });
}
// endregion: rest
