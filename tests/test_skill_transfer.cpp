// SWML transfer skill tests
#include "signalwire/skills/skill_registry.hpp"
namespace sw_skills = signalwire::skills;
using json = nlohmann::json;

TEST(skill_transfer_name) {
  auto skill = sw_skills::SkillRegistry::instance().create("swml_transfer");
  ASSERT_EQ(skill->skill_name(), "swml_transfer");
  return true;
}

TEST(skill_transfer_multi_instance) {
  auto skill = sw_skills::SkillRegistry::instance().create("swml_transfer");
  ASSERT_TRUE(skill->supports_multiple_instances());
  return true;
}

TEST(skill_transfer_setup_requires_transfers) {
  auto skill = sw_skills::SkillRegistry::instance().create("swml_transfer");
  ASSERT_FALSE(skill->setup(json::object()));
  return true;
}

TEST(skill_transfer_setup_with_transfers) {
  auto skill = sw_skills::SkillRegistry::instance().create("swml_transfer");
  ASSERT_TRUE(skill->setup(json::object(
      {{"transfers",
        json::object({{"sales", json::object({{"url", "https://example.com/sales"},
                                              {"message", "Transferring to sales"}})},
                      {"support", json::object({{"url", "https://example.com/support"}})}})}})));
  return true;
}

TEST(skill_transfer_returns_datamap) {
  auto skill = sw_skills::SkillRegistry::instance().create("swml_transfer");
  ASSERT_TRUE(skill->setup(json::object(
      {{"transfers",
        json::object({{"sales", json::object({{"url", "https://example.com/sales"}})}})}})));
  auto dm = skill->get_datamap_functions();
  ASSERT_EQ(dm.size(), 1u);
  ASSERT_EQ(dm[0]["function"].get<std::string>(), "transfer_call");
  return true;
}

TEST(skill_transfer_custom_tool_name) {
  auto skill = sw_skills::SkillRegistry::instance().create("swml_transfer");
  ASSERT_TRUE(skill->setup(
      json::object({{"tool_name", "route_call"},
                    {"transfers", json::object({{"dept", json::object({{"url", "x"}})}})}})));
  auto dm = skill->get_datamap_functions();
  ASSERT_EQ(dm[0]["function"].get<std::string>(), "route_call");
  return true;
}

TEST(skill_transfer_has_hints) {
  auto skill = sw_skills::SkillRegistry::instance().create("swml_transfer");
  ASSERT_TRUE(skill->setup(
      json::object({{"transfers", json::object({{"sales-team", json::object({{"url", "x"}})}})}})));
  auto hints = skill->get_hints();
  ASSERT_TRUE(!hints.empty());
  // Should contain at least "transfer" or "connect"
  bool has_any = false;
  for (const auto& h : hints) {
    if (h == "transfer" || h == "connect") {
      has_any = true;
    }
  }
  ASSERT_TRUE(has_any);
  return true;
}

TEST(skill_transfer_no_webhook_tools) {
  auto skill = sw_skills::SkillRegistry::instance().create("swml_transfer");
  ASSERT_TRUE(skill->setup(
      json::object({{"transfers", json::object({{"sales", json::object({{"url", "x"}})}})}})));
  ASSERT_EQ(skill->register_tools().size(), 0u);
  return true;
}
