// Math skill tests
#include "signalwire/skills/skill_registry.hpp"
namespace sw_skills = signalwire::skills;
using json = nlohmann::json;

TEST(skill_math_name_and_setup) {
  auto skill = sw_skills::SkillRegistry::instance().create("math");
  ASSERT_EQ(skill->skill_name(), "math");
  ASSERT_TRUE(skill->setup(json::object()));
  return true;
}

TEST(skill_math_single_tool) {
  auto skill = sw_skills::SkillRegistry::instance().create("math");
  ASSERT_TRUE(skill->setup(json::object()));
  auto tools = skill->register_tools();
  ASSERT_EQ(tools.size(), 1u);
  ASSERT_EQ(tools[0].name, "calculate");
  return true;
}

TEST(skill_math_calculate_addition) {
  auto skill = sw_skills::SkillRegistry::instance().create("math");
  ASSERT_TRUE(skill->setup(json::object()));
  auto tools = skill->register_tools();
  auto result = tools[0].handler(json::object({{"expression", "2 + 3"}}), json::object());
  ASSERT_TRUE(result.to_json()["response"].get<std::string>().find("5") != std::string::npos);
  return true;
}

TEST(skill_math_calculate_multiplication) {
  auto skill = sw_skills::SkillRegistry::instance().create("math");
  ASSERT_TRUE(skill->setup(json::object()));
  auto tools = skill->register_tools();
  auto result = tools[0].handler(json::object({{"expression", "7 * 8"}}), json::object());
  ASSERT_TRUE(result.to_json()["response"].get<std::string>().find("56") != std::string::npos);
  return true;
}

TEST(skill_math_calculate_empty_expression) {
  auto skill = sw_skills::SkillRegistry::instance().create("math");
  ASSERT_TRUE(skill->setup(json::object()));
  auto tools = skill->register_tools();
  auto result = tools[0].handler(json::object({{"expression", ""}}), json::object());
  // Should return some error/response without crashing
  ASSERT_FALSE(result.to_json()["response"].get<std::string>().empty());
  return true;
}

TEST(skill_math_no_multi_instance) {
  auto skill = sw_skills::SkillRegistry::instance().create("math");
  ASSERT_FALSE(skill->supports_multiple_instances());
  return true;
}
