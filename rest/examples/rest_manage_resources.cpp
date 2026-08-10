// Copyright (c) 2025 SignalWire — MIT License
// REST: Create an AI agent, assign a phone number, place a test call.

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

      // Create an AI agent
      std::cout << "Creating AI agent...\n";
      auto agent = client.fabric().ai_agents.create(
          {{"name", "Demo Support Bot"},
           {"prompt", {{"text", "You are a friendly support agent."}}}});
      std::string agent_id = agent.value("id", "");
      std::cout << "  Created: " << agent_id << "\n";

      // List agents
      auto agents = client.fabric().ai_agents.list();
      std::cout << "  Total agents: " << agents.dump() << "\n";

      // Search phone numbers
      auto numbers = client.phone_numbers().search({{"areacode", "512"}, {"max_results", "3"}});
      std::cout << "  Available numbers: " << numbers.dump() << "\n";

      // Place a test call
      auto call = client.calling().dial({
          .from = "+15559876543",
          .to = "+15551234567",
          .url = "https://example.com/handler",
      });
      std::cout << "  Call: " << call.dump() << "\n";

      // Cleanup. delete_ returns the API's response body; keep it rather than
      // announcing a deletion you never looked at.
      auto deleted = client.fabric().ai_agents.delete_(agent_id);
      std::cout << "  Deleted agent: " << deleted.dump() << "\n";

    } catch (const SignalWireRestError& e) {
      std::cerr << "Error " << e.status_code() << ": " << e.what() << "\n";
    }
  } catch (const std::exception& e) {
    std::cerr << "fatal: " << e.what() << "\n";
    return 1;
  }
}
