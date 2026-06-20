// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#include "signalwire/prefabs/prefabs.hpp"

namespace signalwire {
namespace prefabs {

ReceptionistAgent::ReceptionistAgent(const std::string& name, const std::string& route,
                                     const std::string& host, int port)
    : AgentBase(name, route, host, port) {
  prompt_add_section("Personality",
                     "You are a professional receptionist. Greet callers warmly and help "
                     "route them to the correct department.");
  prompt_add_section(
      "Instructions", "",
      {"Greet the caller and ask how you can help", "Determine which department they need",
       "Transfer the call to the appropriate department", "If unsure, ask clarifying questions"});
}

ReceptionistAgent& ReceptionistAgent::set_departments(const json& departments) {
  // Set up swml_transfer skill with department mappings
  json transfers = json::object();
  std::vector<std::string> bullets;

  for (auto& [name, config] : departments.items()) {
    transfers[name] = config;
    std::string desc = name;
    if (config.contains("description")) {
      desc += " - " + config["description"].get<std::string>();
    }
    bullets.push_back(desc);
  }

  add_skill("swml_transfer",
            json::object({{"transfers", transfers}, {"description", "Transfer to department"}}));

  prompt_add_section("Available Departments", "", bullets);
  return *this;
}

ReceptionistAgent& ReceptionistAgent::set_greeting(const std::string& greeting) {
  prompt_add_to_section("Personality", "\nGreeting: " + greeting);
  return *this;
}

ReceptionistAgent& ReceptionistAgent::set_transfer_message(const std::string& msg) {
  prompt_add_to_section("Instructions", "", {"When transferring, say: " + msg});
  return *this;
}

}  // namespace prefabs
}  // namespace signalwire
