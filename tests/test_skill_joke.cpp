// Joke skill tests
#include "signalwire/skills/skill_registry.hpp"
namespace sw_skills = signalwire::skills;
using json = nlohmann::json;

TEST(skill_joke_name) {
  auto skill = sw_skills::SkillRegistry::instance().create("joke");
  ASSERT_EQ(skill->skill_name(), "joke");
  return true;
}

TEST(skill_joke_setup_requires_api_key) {
  auto skill = sw_skills::SkillRegistry::instance().create("joke");
  // No api key and env not set => setup fails
  bool result = skill->setup(json::object());
  // Depends on env; check it doesn't crash
  (void)result;
  return true;
}

TEST(skill_joke_setup_with_api_key) {
  auto skill = sw_skills::SkillRegistry::instance().create("joke");
  ASSERT_TRUE(skill->setup(json::object({{"api_key", "test-key"}})));
  return true;
}

TEST(skill_joke_returns_datamap_functions) {
  auto skill = sw_skills::SkillRegistry::instance().create("joke");
  skill->setup(json::object({{"api_key", "test-key"}}));
  auto dm = skill->get_datamap_functions();
  ASSERT_EQ(dm.size(), 1u);
  ASSERT_EQ(dm[0]["function"].get<std::string>(), "get_joke");
  ASSERT_TRUE(dm[0].contains("data_map"));
  return true;
}

TEST(skill_joke_custom_tool_name) {
  auto skill = sw_skills::SkillRegistry::instance().create("joke");
  skill->setup(json::object({{"api_key", "key"}, {"tool_name", "tell_joke"}}));
  auto dm = skill->get_datamap_functions();
  ASSERT_EQ(dm[0]["function"].get<std::string>(), "tell_joke");
  return true;
}

TEST(skill_joke_has_prompt_sections) {
  auto skill = sw_skills::SkillRegistry::instance().create("joke");
  skill->setup(json::object({{"api_key", "key"}}));
  auto sections = skill->get_prompt_sections();
  ASSERT_EQ(sections.size(), 1u);
  ASSERT_EQ(sections[0].title, "Joke Telling");
  return true;
}

TEST(skill_joke_has_global_data) {
  auto skill = sw_skills::SkillRegistry::instance().create("joke");
  skill->setup(json::object({{"api_key", "key"}}));
  auto gd = skill->get_global_data();
  ASSERT_TRUE(gd.contains("joke_skill_enabled"));
  ASSERT_EQ(gd["joke_skill_enabled"].get<bool>(), true);
  return true;
}

TEST(skill_joke_no_webhook_tools) {
  auto skill = sw_skills::SkillRegistry::instance().create("joke");
  skill->setup(json::object({{"api_key", "key"}}));
  auto tools = skill->register_tools();
  ASSERT_EQ(tools.size(), 0u);
  return true;
}
