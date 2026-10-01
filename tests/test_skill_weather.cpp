// Weather API skill tests
#include "signalwire/skills/skill_registry.hpp"
namespace sw_skills = signalwire::skills;
using json = nlohmann::json;

TEST(skill_weather_name) {
  auto skill = sw_skills::SkillRegistry::instance().create("weather_api");
  ASSERT_EQ(skill->skill_name(), "weather_api");
  return true;
}

TEST(skill_weather_setup_with_key) {
  auto skill = sw_skills::SkillRegistry::instance().create("weather_api");
  ASSERT_TRUE(skill->setup(json::object({{"api_key", "test-key"}})));
  return true;
}

TEST(skill_weather_returns_datamap) {
  auto skill = sw_skills::SkillRegistry::instance().create("weather_api");
  ASSERT_TRUE(skill->setup(json::object({{"api_key", "test-key"}})));
  auto dm = skill->get_datamap_functions();
  ASSERT_EQ(dm.size(), 1u);
  ASSERT_EQ(dm[0]["function"].get<std::string>(), "get_weather");
  return true;
}

TEST(skill_weather_custom_tool_name) {
  auto skill = sw_skills::SkillRegistry::instance().create("weather_api");
  ASSERT_TRUE(skill->setup(json::object({{"api_key", "k"}, {"tool_name", "check_weather"}})));
  auto dm = skill->get_datamap_functions();
  ASSERT_EQ(dm[0]["function"].get<std::string>(), "check_weather");
  return true;
}

TEST(skill_weather_datamap_has_webhook) {
  auto skill = sw_skills::SkillRegistry::instance().create("weather_api");
  ASSERT_TRUE(skill->setup(json::object({{"api_key", "k"}})));
  auto dm = skill->get_datamap_functions();
  ASSERT_TRUE(dm[0]["data_map"].contains("webhooks"));
  ASSERT_EQ(dm[0]["data_map"]["webhooks"].size(), 1u);
  auto url = dm[0]["data_map"]["webhooks"][0]["url"].get<std::string>();
  ASSERT_TRUE(url.find("weatherapi.com") != std::string::npos);
  return true;
}

TEST(skill_weather_has_location_param) {
  auto skill = sw_skills::SkillRegistry::instance().create("weather_api");
  ASSERT_TRUE(skill->setup(json::object({{"api_key", "k"}})));
  auto dm = skill->get_datamap_functions();
  ASSERT_TRUE(dm[0]["parameters"]["properties"].contains("location"));
  return true;
}

TEST(skill_weather_no_webhook_tools) {
  auto skill = sw_skills::SkillRegistry::instance().create("weather_api");
  ASSERT_TRUE(skill->setup(json::object({{"api_key", "k"}})));
  ASSERT_EQ(skill->register_tools().size(), 0u);
  return true;
}
