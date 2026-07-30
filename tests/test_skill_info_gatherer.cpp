// Info gatherer skill tests
#include "signalwire/skills/skill_registry.hpp"
namespace sw_skills = signalwire::skills;
using json = nlohmann::json;

TEST(skill_infogatherer_name) {
  auto skill = sw_skills::SkillRegistry::instance().create("info_gatherer");
  ASSERT_EQ(skill->skill_name(), "info_gatherer");
  return true;
}

TEST(skill_infogatherer_multi_instance) {
  auto skill = sw_skills::SkillRegistry::instance().create("info_gatherer");
  ASSERT_TRUE(skill->supports_multiple_instances());
  return true;
}

TEST(skill_infogatherer_setup_requires_questions) {
  auto skill = sw_skills::SkillRegistry::instance().create("info_gatherer");
  ASSERT_FALSE(skill->setup(json::object()));
  return true;
}

TEST(skill_infogatherer_setup_with_questions) {
  auto skill = sw_skills::SkillRegistry::instance().create("info_gatherer");
  ASSERT_TRUE(skill->setup(json::object(
      {{"questions",
        json::array(
            {json::object({{"key_name", "name"}, {"question_text", "What is your name?"}}),
             json::object({{"key_name", "email"}, {"question_text", "What is your email?"}})})}})));
  return true;
}

TEST(skill_infogatherer_two_tools) {
  auto skill = sw_skills::SkillRegistry::instance().create("info_gatherer");
  skill->setup(json::object(
      {{"questions", json::array({json::object(
                         {{"key_name", "name"}, {"question_text", "What is your name?"}})})}}));
  auto tools = skill->register_tools();
  ASSERT_EQ(tools.size(), 2u);
  ASSERT_EQ(tools[0].name, "start_questions");
  ASSERT_EQ(tools[1].name, "submit_answer");
  return true;
}

TEST(skill_infogatherer_prefixed_tools) {
  auto skill = sw_skills::SkillRegistry::instance().create("info_gatherer");
  skill->setup(json::object(
      {{"prefix", "contact"},
       {"questions", json::array({json::object({{"key_name", "n"}, {"question_text", "Q?"}})})}}));
  auto tools = skill->register_tools();
  ASSERT_EQ(tools[0].name, "contact_start_questions");
  ASSERT_EQ(tools[1].name, "contact_submit_answer");
  return true;
}

TEST(skill_infogatherer_start_handler_returns_response) {
  auto skill = sw_skills::SkillRegistry::instance().create("info_gatherer");
  skill->setup(json::object(
      {{"questions", json::array({json::object(
                         {{"key_name", "name"}, {"question_text", "What is your name?"}})})}}));
  auto tools = skill->register_tools();
  auto result = tools[0].handler(json::object(), json::object());
  auto resp = result.to_json()["response"].get<std::string>();
  ASSERT_FALSE(resp.empty());
  return true;
}

TEST(skill_infogatherer_instance_key_default) {
  auto skill = sw_skills::SkillRegistry::instance().create("info_gatherer");
  skill->setup(json::object(
      {{"questions", json::array({json::object({{"key_name", "n"}, {"question_text", "Q?"}})})}}));
  ASSERT_EQ(skill->get_instance_key(), "info_gatherer");
  return true;
}

TEST(skill_infogatherer_instance_key_with_prefix) {
  auto skill = sw_skills::SkillRegistry::instance().create("info_gatherer");
  skill->setup(json::object(
      {{"prefix", "contact"},
       {"questions", json::array({json::object({{"key_name", "n"}, {"question_text", "Q?"}})})}}));
  ASSERT_EQ(skill->get_instance_key(), "info_gatherer_contact");
  return true;
}

TEST(skill_infogatherer_submit_handler) {
  auto skill = sw_skills::SkillRegistry::instance().create("info_gatherer");
  skill->setup(json::object(
      {{"questions",
        json::array({json::object({{"key_name", "name"}, {"question_text", "Name?"}})})}}));
  auto tools = skill->register_tools();
  // Call submit_answer handler
  auto result = tools[1].handler(json::object({{"answer", "John"}, {"confirmed_by_user", true}}),
                                 json::object());
  auto resp = result.to_json()["response"].get<std::string>();
  ASSERT_FALSE(resp.empty());
  return true;
}
