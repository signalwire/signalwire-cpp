// Prefab InfoGatherer tests
#include "signalwire/prefabs/prefabs.hpp"
using namespace signalwire::prefabs;
using json = nlohmann::json;

TEST(prefab_ig_default_construction) {
  InfoGathererAgent agent;
  ASSERT_EQ(agent.name(), "info_gatherer");
  return true;
}

TEST(prefab_ig_named_construction) {
  InfoGathererAgent agent("my_ig", "/my_ig");
  ASSERT_EQ(agent.name(), "my_ig");
  ASSERT_EQ(agent.route(), "/my_ig");
  return true;
}

TEST(prefab_ig_has_personality_section) {
  InfoGathererAgent agent;
  ASSERT_TRUE(agent.prompt_has_section("Personality"));
  return true;
}

TEST(prefab_ig_has_instructions_section) {
  InfoGathererAgent agent;
  ASSERT_TRUE(agent.prompt_has_section("Instructions"));
  return true;
}

TEST(prefab_ig_set_questions) {
  InfoGathererAgent agent;
  agent.set_questions({json::object({{"key_name", "name"}, {"question_text", "Your name?"}}),
                       json::object({{"key_name", "email"}, {"question_text", "Your email?"}})});
  ASSERT_TRUE(agent.has_skill("info_gatherer"));
  return true;
}

TEST(prefab_ig_set_completion_message) {
  InfoGathererAgent agent;
  agent.set_completion_message("All done!");
  // No crash
  return true;
}

TEST(prefab_ig_set_prefix) {
  InfoGathererAgent agent;
  agent.set_prefix("contact");
  // No crash
  return true;
}

TEST(prefab_ig_renders_swml) {
  InfoGathererAgent agent;
  agent.set_questions({json::object({{"key_name", "name"}, {"question_text", "Name?"}})});
  json swml = agent.render_swml();
  ASSERT_EQ(swml["version"].get<std::string>(), "1.0.0");
  ASSERT_TRUE(swml["sections"]["main"].size() >= 2u);
  return true;
}
