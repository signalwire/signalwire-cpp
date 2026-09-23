// Custom skills skill tests
#include "signalwire/skills/skill_registry.hpp"
namespace sw_skills = signalwire::skills;
using json = nlohmann::json;

TEST(skill_custom_name) {
  auto skill = sw_skills::SkillRegistry::instance().create("custom_skills");
  ASSERT_EQ(skill->skill_name(), "custom_skills");
  return true;
}

TEST(skill_custom_multi_instance) {
  auto skill = sw_skills::SkillRegistry::instance().create("custom_skills");
  ASSERT_TRUE(skill->supports_multiple_instances());
  return true;
}

TEST(skill_custom_setup_requires_tools) {
  auto skill = sw_skills::SkillRegistry::instance().create("custom_skills");
  ASSERT_FALSE(skill->setup(json::object()));
  return true;
}

TEST(skill_custom_setup_with_tools) {
  auto skill = sw_skills::SkillRegistry::instance().create("custom_skills");
  ASSERT_TRUE(skill->setup(json::object(
      {{"tools",
        json::array({json::object(
            {{"name", "tool1"}, {"description", "Tool 1"}, {"response", "Response 1"}})})}})));
  return true;
}

TEST(skill_custom_registers_defined_tools) {
  auto skill = sw_skills::SkillRegistry::instance().create("custom_skills");
  ASSERT_TRUE(skill->setup(json::object(
      {{"tools",
        json::array(
            {json::object({{"name", "greet"}, {"description", "Greet"}, {"response", "Hello!"}}),
             json::object(
                 {{"name", "bye"}, {"description", "Bye"}, {"response", "Goodbye!"}})})}})));
  auto tools = skill->register_tools();
  ASSERT_EQ(tools.size(), 2u);
  ASSERT_EQ(tools[0].name, "greet");
  ASSERT_EQ(tools[1].name, "bye");
  return true;
}

TEST(skill_custom_handler_returns_response) {
  auto skill = sw_skills::SkillRegistry::instance().create("custom_skills");
  ASSERT_TRUE(skill->setup(json::object(
      {{"tools",
        json::array({json::object(
            {{"name", "greet"}, {"description", "Greet"}, {"response", "Hello world!"}})})}})));
  auto tools = skill->register_tools();
  auto result = tools[0].handler(json::object(), json::object());
  ASSERT_EQ(result.to_json()["response"].get<std::string>(), "Hello world!");
  return true;
}

TEST(skill_custom_minimal_tool_has_name) {
  auto skill = sw_skills::SkillRegistry::instance().create("custom_skills");
  ASSERT_TRUE(skill->setup(json::object({
      {"tools", json::array({json::object()})}  // Minimal tool def
  })));
  auto tools = skill->register_tools();
  ASSERT_EQ(tools.size(), 1u);
  // Whatever default name the implementation uses
  ASSERT_FALSE(tools[0].name.empty());
  return true;
}

TEST(skill_custom_tool_with_parameters) {
  auto skill = sw_skills::SkillRegistry::instance().create("custom_skills");
  ASSERT_TRUE(skill->setup(json::object(
      {{"tools",
        json::array({json::object(
            {{"name", "lookup"},
             {"description", "Lookup item"},
             {"parameters",
              json::object(
                  {{"type", "object"},
                   {"properties", json::object({{"query", json::object({{"type", "string"}})}})}})},
             {"response", "Found it"}})})}})));
  auto tools = skill->register_tools();
  ASSERT_EQ(tools[0].name, "lookup");
  ASSERT_TRUE(tools[0].parameters.contains("properties"));
  return true;
}
