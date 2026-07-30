// Prefab Receptionist tests
#include "signalwire/prefabs/prefabs.hpp"
using namespace signalwire::prefabs;
using json = nlohmann::json;

TEST(prefab_receptionist_default) {
  ReceptionistAgent agent;
  ASSERT_EQ(agent.name(), "receptionist");
  return true;
}

TEST(prefab_receptionist_named) {
  ReceptionistAgent agent("frontdesk", "/frontdesk");
  ASSERT_EQ(agent.name(), "frontdesk");
  return true;
}

TEST(prefab_receptionist_has_personality) {
  ReceptionistAgent agent;
  ASSERT_TRUE(agent.prompt_has_section("Personality"));
  return true;
}

TEST(prefab_receptionist_set_departments_and_skill) {
  ReceptionistAgent agent;
  agent.set_departments(json::object(
      {{"sales", json::object({{"url", "https://example.com/sales"}, {"description", "Sales"}})},
       {"support", json::object({{"url", "https://example.com/support"}})}}));
  ASSERT_TRUE(agent.has_skill("swml_transfer"));
  return true;
}

TEST(prefab_receptionist_set_greeting) {
  ReceptionistAgent agent;
  agent.set_greeting("Welcome to our company!");
  return true;
}

TEST(prefab_receptionist_set_transfer_message) {
  ReceptionistAgent agent;
  agent.set_transfer_message("Connecting you now...");
  return true;
}

TEST(prefab_receptionist_renders_swml) {
  ReceptionistAgent agent;
  json swml = agent.render_swml();
  ASSERT_TRUE(swml.contains("version"));
  return true;
}
