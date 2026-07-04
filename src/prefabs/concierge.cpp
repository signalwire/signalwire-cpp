// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#include <algorithm>
#include <cctype>

#include "signalwire/prefabs/prefabs.hpp"

namespace signalwire {
namespace prefabs {

namespace {
bool iequals(const std::string& a, const std::string& b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (size_t i = 0; i < a.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) !=
        std::tolower(static_cast<unsigned char>(b[i]))) {
      return false;
    }
  }
  return true;
}
}  // namespace

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
  venue_name_ = venue_name;
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

  amenities_.assign(amenities.begin(), amenities.end());
  update_global_data(json::object({{"amenities", amenity_data}}));
  prompt_add_section("Available Amenities", "", bullets);

  // Define availability + directions tools bound to the member handlers
  // (ported from the Java ConciergeAgent check_availability / get_directions).
  define_tool(
      "check_availability", "Check availability of an amenity or service",
      json::object(
          {{"type", "object"},
           {"properties",
            json::object({{"amenity",
                           json::object({{"type", "string"}, {"description", "Amenity to check"}})},
                          {"date", json::object({{"type", "string"},
                                                 {"description", "Date to check (YYYY-MM-DD)"}})},
                          {"time", json::object({{"type", "string"},
                                                 {"description", "Time to check (HH:MM)"}})}})},
           {"required", json::array({"amenity"})}}),
      [this](const json& args, const json& raw) { return check_availability(args, raw); });

  define_tool(
      "get_directions", "Get directions to a specific location or amenity",
      json::object(
          {{"type", "object"},
           {"properties",
            json::object(
                {{"location", json::object({{"type", "string"},
                                            {"description",
                                             "The location or amenity to get directions to"}})}})},
           {"required", json::array({"location"})}}),
      [this](const json& args, const json& raw) { return get_directions(args, raw); });

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

swaig::FunctionResult ConciergeAgent::check_availability(const json& args, const json&) {
  std::string amenity_name = args.value("amenity", "");
  std::string date = args.value("date", "today");
  std::string time = args.value("time", "now");

  for (const auto& amenity : amenities_) {
    if (iequals(amenity_name, amenity.value("name", ""))) {
      return swaig::FunctionResult(amenity_name + " is available on " + date + " at " + time +
                                   ". Would you like to make a reservation?");
    }
  }

  std::vector<std::string> names;
  for (const auto& amenity : amenities_) {
    names.push_back(amenity.value("name", ""));
  }
  std::string joined;
  for (size_t i = 0; i < names.size(); ++i) {
    if (i) {
      joined += ", ";
    }
    joined += names[i];
  }
  return swaig::FunctionResult("I'm sorry, we don't offer " + amenity_name + " at " + venue_name_ +
                               ". Our available amenities are: " + joined + ".");
}

swaig::FunctionResult ConciergeAgent::get_directions(const json& args, const json&) {
  std::string location = args.value("location", "");

  for (const auto& amenity : amenities_) {
    if (iequals(location, amenity.value("name", "")) && amenity.contains("location")) {
      std::string amenity_location = amenity["location"].get<std::string>();
      return swaig::FunctionResult("The " + location + " is located at " + amenity_location +
                                   ". From the main entrance, follow the signs to " +
                                   amenity_location + ".");
    }
  }
  return swaig::FunctionResult("I don't have specific directions to " + location +
                               ". You can ask our staff at the front desk for assistance.");
}

ConciergeAgent& ConciergeAgent::on_summary(agent::SummaryCallback cb) {
  AgentBase::on_summary(std::move(cb));
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
