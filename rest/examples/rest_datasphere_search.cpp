// Copyright (c) 2025 SignalWire — MIT License
// REST: Upload a document and run a semantic search via Datasphere.

#include <iostream>
#include <signalwire/rest/rest_client.hpp>

using namespace signalwire::rest;
using json = nlohmann::json;

int main() {
  // exception-escape guard: main() must not let an exception escape
  // (that is std::terminate, with no message). Report and exit nonzero.
  try {
    try {
      auto client = RestClient::from_env();

      // Create a document
      std::cout << "Creating document...\n";
      auto doc = client.datasphere().documents.create(
          {{"name", "product-docs"},
           {"content", "SignalWire AI Agents SDK enables building voice AI applications."}});
      std::cout << "  Document: " << doc.dump(2) << "\n";

      // Search
      std::cout << "\nSearching...\n";
      auto results = client.datasphere().documents.search({
          .query_string = "How to build AI agents?",
          .count = 5,
      });
      std::cout << "  Results: " << results.dump(2) << "\n";

    } catch (const SignalWireRestError& e) {
      std::cerr << "Error " << e.status_code() << ": " << e.what() << "\n";
    }
  } catch (const std::exception& e) {
    std::cerr << "fatal: " << e.what() << "\n";
    return 1;
  }
}
