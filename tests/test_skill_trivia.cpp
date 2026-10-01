// API Ninjas trivia skill tests
#include "signalwire/skills/skill_registry.hpp"
namespace sw_skills = signalwire::skills;
using json = nlohmann::json;

TEST(skill_trivia_name) {
  auto skill = sw_skills::SkillRegistry::instance().create("api_ninjas_trivia");
  ASSERT_EQ(skill->skill_name(), "api_ninjas_trivia");
  return true;
}

TEST(skill_trivia_multi_instance) {
  auto skill = sw_skills::SkillRegistry::instance().create("api_ninjas_trivia");
  ASSERT_TRUE(skill->supports_multiple_instances());
  return true;
}

TEST(skill_trivia_setup_with_key) {
  auto skill = sw_skills::SkillRegistry::instance().create("api_ninjas_trivia");
  ASSERT_TRUE(skill->setup(json::object({{"api_key", "test-key"}})));
  return true;
}

TEST(skill_trivia_returns_datamap) {
  auto skill = sw_skills::SkillRegistry::instance().create("api_ninjas_trivia");
  ASSERT_TRUE(skill->setup(json::object({{"api_key", "key"}})));
  auto dm = skill->get_datamap_functions();
  ASSERT_EQ(dm.size(), 1u);
  ASSERT_EQ(dm[0]["function"].get<std::string>(), "get_trivia");
  return true;
}

TEST(skill_trivia_datamap_has_webhook) {
  auto skill = sw_skills::SkillRegistry::instance().create("api_ninjas_trivia");
  ASSERT_TRUE(skill->setup(json::object({{"api_key", "key"}})));
  auto dm = skill->get_datamap_functions();
  ASSERT_TRUE(dm[0]["data_map"].contains("webhooks"));
  auto url = dm[0]["data_map"]["webhooks"][0]["url"].get<std::string>();
  ASSERT_TRUE(url.find("api-ninjas.com") != std::string::npos);
  return true;
}

TEST(skill_trivia_has_category_param) {
  auto skill = sw_skills::SkillRegistry::instance().create("api_ninjas_trivia");
  ASSERT_TRUE(skill->setup(json::object({{"api_key", "key"}})));
  auto dm = skill->get_datamap_functions();
  ASSERT_TRUE(dm[0]["parameters"]["properties"].contains("category"));
  return true;
}

TEST(skill_trivia_no_webhook_tools) {
  auto skill = sw_skills::SkillRegistry::instance().create("api_ninjas_trivia");
  ASSERT_TRUE(skill->setup(json::object({{"api_key", "key"}})));
  ASSERT_EQ(skill->register_tools().size(), 0u);
  return true;
}
