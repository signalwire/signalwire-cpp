// Copyright (c) 2025 SignalWire — MIT License
// Receptionist prefab: department routing with call transfer.

#include <iostream>
#include <signalwire/prefabs/prefabs.hpp>

using namespace signalwire;
using json = nlohmann::json;

int main() {
  // exception-escape guard: main() must not let an exception escape
  // (that is std::terminate, with no message). Report and exit nonzero.
  try {
    prefabs::ReceptionistAgent agent("receptionist", "/receptionist");

    agent.set_greeting("Welcome to Acme Corporation! How may I direct your call?");
    agent.set_departments(
        {{"sales", {{"number", "+15551001"}, {"description", "Sales and new accounts"}}},
         {"support", {{"number", "+15551002"}, {"description", "Technical support"}}},
         {"billing", {{"number", "+15551003"}, {"description", "Billing and payments"}}},
         {"hr", {{"number", "+15551004"}, {"description", "Human resources"}}}});
    agent.set_transfer_message("I'll transfer you now. Please hold.");

    std::cout << "Receptionist at http://0.0.0.0:3000/receptionist\n";
    agent.run();
  } catch (const std::exception& e) {
    std::cerr << "fatal: " << e.what() << "\n";
    return 1;
  }
}
