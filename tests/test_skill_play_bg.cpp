// Play background file skill tests
#include "signalwire/skills/skill_registry.hpp"
namespace sw_skills = signalwire::skills;
using json = nlohmann::json;

TEST(skill_playbg_name) {
  auto skill = sw_skills::SkillRegistry::instance().create("play_background_file");
  ASSERT_EQ(skill->skill_name(), "play_background_file");
  return true;
}

TEST(skill_playbg_multi_instance) {
  auto skill = sw_skills::SkillRegistry::instance().create("play_background_file");
  ASSERT_TRUE(skill->supports_multiple_instances());
  return true;
}

TEST(skill_playbg_setup_requires_files) {
  auto skill = sw_skills::SkillRegistry::instance().create("play_background_file");
  ASSERT_FALSE(skill->setup(json::object()));
  return true;
}

TEST(skill_playbg_setup_with_files) {
  auto skill = sw_skills::SkillRegistry::instance().create("play_background_file");
  ASSERT_TRUE(skill->setup(json::object(
      {{"files",
        json::array(
            {json::object({{"key", "music"}, {"url", "https://example.com/music.mp3"}}),
             json::object({{"key", "hold"}, {"url", "https://example.com/hold.mp3"}})})}})));
  return true;
}

TEST(skill_playbg_returns_datamap) {
  auto skill = sw_skills::SkillRegistry::instance().create("play_background_file");
  skill->setup(json::object(
      {{"files", json::array({json::object(
                     {{"key", "music"}, {"url", "https://example.com/music.mp3"}})})}}));
  auto dm = skill->get_datamap_functions();
  ASSERT_EQ(dm.size(), 1u);
  ASSERT_EQ(dm[0]["function"].get<std::string>(), "play_background_file");
  return true;
}

TEST(skill_playbg_custom_tool_name) {
  auto skill = sw_skills::SkillRegistry::instance().create("play_background_file");
  skill->setup(
      json::object({{"tool_name", "bg_music"},
                    {"files", json::array({json::object({{"key", "music"}, {"url", "x"}})})}}));
  auto dm = skill->get_datamap_functions();
  ASSERT_EQ(dm[0]["function"].get<std::string>(), "bg_music");
  return true;
}

TEST(skill_playbg_no_webhook_tools) {
  auto skill = sw_skills::SkillRegistry::instance().create("play_background_file");
  skill->setup(
      json::object({{"files", json::array({json::object({{"key", "m"}, {"url", "x"}})})}}));
  ASSERT_EQ(skill->register_tools().size(), 0u);
  return true;
}
