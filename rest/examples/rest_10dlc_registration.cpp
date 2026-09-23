// Copyright (c) 2025 SignalWire — MIT License
// REST: 10DLC registration workflow.

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
      std::cerr << "Error " << e.status_code() << ": " << e.what() << "\n";
    }
  } catch (const std::exception& e) {
    std::cerr << "fatal: " << e.what() << "\n";
    return 1;
  }
}
