// Prefab Concierge tests
#include "signalwire/prefabs/prefabs.hpp"
using namespace signalwire::prefabs;
using json = nlohmann::json;

TEST(prefab_concierge_default) {
  ConciergeAgent agent;
  ASSERT_EQ(agent.name(), "concierge");
  return true;
}

TEST(prefab_concierge_named) {
  ConciergeAgent agent("hotel", "/hotel");
  ASSERT_EQ(agent.name(), "hotel");
  return true;
}

TEST(prefab_concierge_has_personality) {
  ConciergeAgent agent;
  ASSERT_TRUE(agent.prompt_has_section("Personality"));
  return true;
}

TEST(prefab_concierge_set_venue_name) {
  ConciergeAgent agent;
  agent.set_venue_name("The Grand Hotel");
  // Prompt should update
  return true;
}

TEST(prefab_concierge_set_amenities_full) {
  ConciergeAgent agent;
  agent.set_amenities(
      {json::object({{"name", "Pool"}, {"location", "Floor 2"}, {"available", true}}),
       json::object({{"name", "Gym"}, {"location", "Floor 1"}, {"available", true}}),
       json::object({{"name", "Spa"}, {"location", "Floor 3"}, {"available", false}})});
  ASSERT_TRUE(agent.has_tool("check_amenity"));
  ASSERT_TRUE(agent.prompt_has_section("Available Amenities"));
  return true;
}

TEST(prefab_concierge_set_hours_section) {
  ConciergeAgent agent;
  agent.set_hours(json::object({{"Monday", "9:00 AM - 9:00 PM"},
                                {"Tuesday", "9:00 AM - 9:00 PM"},
                                {"Wednesday", "9:00 AM - 9:00 PM"}}));
  ASSERT_TRUE(agent.prompt_has_section("Venue Hours"));
  return true;
}

TEST(prefab_concierge_full_config) {
  ConciergeAgent agent;
  agent.set_venue_name("Resort");
  agent.set_amenities(
      {json::object({{"name", "Beach"}, {"location", "Ground"}, {"available", true}})});
  agent.set_hours(json::object({{"Daily", "8 AM - 10 PM"}}));
  json swml = agent.render_swml();
  ASSERT_TRUE(swml.contains("version"));
  auto& main = swml["sections"]["main"];
  bool has_ai = false;
  for (const auto& v : main) {
    if (v.contains("ai")) {
      has_ai = true;
    }
  }
  ASSERT_TRUE(has_ai);
  return true;
}

// Reference parity: ConciergeAgent.__init__ stores venue_name / services /
// amenities / hours_of_operation / special_instructions as public instance
// attributes. The port carried only venue_name + amenities.
TEST(prefab_concierge_construction_params_readable) {
  ConciergeAgent agent;
  // reference default: {"default": "9 AM - 5 PM"}
  ASSERT_EQ(agent.hours_of_operation()["default"], "9 AM - 5 PM");
  ASSERT_EQ(agent.services().size(), 0u);
  ASSERT_EQ(agent.special_instructions().size(), 0u);

  agent.set_venue_name("Grand Hotel");
  agent.set_amenities({json::object({{"name", "pool"}, {"location", "level 3"}})});
  agent.set_services({"valet", "room service"});
  agent.set_hours(json::object({{"monday", "8 AM - 6 PM"}}));
  agent.set_special_instructions({"Always greet by name."});

  ASSERT_EQ(agent.venue_name(), "Grand Hotel");
  ASSERT_EQ(agent.amenities().size(), 1u);
  ASSERT_EQ(agent.amenities()[0]["name"], "pool");
  ASSERT_EQ(agent.services().size(), 2u);
  ASSERT_EQ(agent.services()[0], "valet");
  ASSERT_EQ(agent.hours_of_operation()["monday"], "8 AM - 6 PM");
  ASSERT_EQ(agent.special_instructions().size(), 1u);
  return true;
}

// An empty hours object keeps the reference default (`hours_of_operation or {…}`).
TEST(prefab_concierge_hours_empty_falls_back) {
  ConciergeAgent agent;
  agent.set_hours(json::object());
  ASSERT_EQ(agent.hours_of_operation()["default"], "9 AM - 5 PM");
  return true;
}
