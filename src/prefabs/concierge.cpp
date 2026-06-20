// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#include "signalwire/prefabs/prefabs.hpp"

namespace signalwire {
namespace prefabs {

ConciergeAgent::ConciergeAgent(const std::string& name, const std::string& route,
                               const std::string& host, int port)
    : AgentBase(name, route, host, port) {
  prompt_add_section("Personality",
                     "You are a friendly and knowledgeable concierge. Help guests with "
                     "information about the venue, amenities, and services.");
  prompt_add_section(
      "Instructions", "",
      {"Provide information about available amenities", "Help with directions within the venue",
       "Answer questions about hours and availability", "Be warm and welcoming in your responses"});
}

ConciergeAgent& ConciergeAgent::set_venue_name(const std::string& venue_name) {
  prompt_add_to_section("Personality", "\nYou work at " + venue_name + ".");
  update_global_data(json::object({{"venue_name", venue_name}}));
  return *this;
}

ConciergeAgent& ConciergeAgent::set_amenities(const std::vector<json>& amenities) {
  json amenity_data = json::array();
  std::vector<std::string> bullets;

  for (const auto& a : amenities) {
    amenity_data.push_back(a);
    std::string name = a.value("name", "");
    std::string desc = a.value("description", "");
    std::string bullet = name;
    if (!desc.empty()) {
      bullet += " - ";
      bullet += desc;
    }
    bullets.push_back(bullet);
  }

  update_global_data(json::object({{"amenities", amenity_data}}));
  prompt_add_section("Available Amenities", "", bullets);

  // Define tool to check amenity availability
  define_tool(
      "check_amenity", "Check availability of a venue amenity",
      json::object(
          {{"type", "object"},
           {"properties",
            json::object({{"amenity_name", json::object({{"type", "string"},
                                                         {"description", "Name of amenity"}})}})},
           {"required", json::array({"amenity_name"})}}),
      [amenity_data](const json& args, const json&) -> swaig::FunctionResult {
        std::string name = args.value("amenity_name", "");
        std::string lower_name = name;
        std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);

        for (const auto& a : amenity_data) {
          std::string an = a.value("name", "");
          std::string lower_an = an;
          std::transform(lower_an.begin(), lower_an.end(), lower_an.begin(), ::tolower);

          if (lower_an.find(lower_name) != std::string::npos) {
            std::string info = an;
            if (a.contains("location")) {
              info += " - Location: " + a["location"].get<std::string>();
            }
            if (a.contains("hours")) {
              info += " - Hours: " + a["hours"].get<std::string>();
            }
            if (a.contains("available")) {
              info += a["available"].get<bool>() ? " (Available)" : " (Currently unavailable)";
            }
            return swaig::FunctionResult(info);
          }
        }

        return swaig::FunctionResult("I couldn't find an amenity matching '" + name +
                                     "'. Please check the available amenities.");
      });

  return *this;
}

ConciergeAgent& ConciergeAgent::set_hours(const json& hours) {
  update_global_data(json::object({{"venue_hours", hours}}));

  std::vector<std::string> bullets;
  for (auto& [day, time] : hours.items()) {
    bullets.push_back(day + ": " + time.get<std::string>());
  }
  prompt_add_section("Venue Hours", "", bullets);

  return *this;
}

}  // namespace prefabs
}  // namespace signalwire
