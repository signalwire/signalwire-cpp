// Skills system tests

#include "signalwire/skills/skill_base.hpp"
#include "signalwire/skills/skill_registry.hpp"
#include "signalwire/skills/skill_manager.hpp"

namespace sw_skills = signalwire::skills;
using json = nlohmann::json;

// Force skill registration linkage
static bool _skills_init = (sw_skills::ensure_builtin_skills_registered(), true);

// ========================================================================
// Registry tests
// ========================================================================

TEST(skill_registry_has_datetime) {
    auto& reg = sw_skills::SkillRegistry::instance();
    ASSERT_TRUE(reg.has_skill("datetime"));
    return true;
}

TEST(skill_registry_has_math) {
    auto& reg = sw_skills::SkillRegistry::instance();
    ASSERT_TRUE(reg.has_skill("math"));
    return true;
}

TEST(skill_registry_has_joke) {
    auto& reg = sw_skills::SkillRegistry::instance();
    ASSERT_TRUE(reg.has_skill("joke"));
    return true;
}

TEST(skill_registry_has_weather_api) {
    auto& reg = sw_skills::SkillRegistry::instance();
    ASSERT_TRUE(reg.has_skill("weather_api"));
    return true;
}

TEST(skill_registry_has_web_search) {
    auto& reg = sw_skills::SkillRegistry::instance();
    ASSERT_TRUE(reg.has_skill("web_search"));
    return true;
}

TEST(skill_registry_has_wikipedia_search) {
    auto& reg = sw_skills::SkillRegistry::instance();
    ASSERT_TRUE(reg.has_skill("wikipedia_search"));
    return true;
}

TEST(skill_registry_has_google_maps) {
    auto& reg = sw_skills::SkillRegistry::instance();
    ASSERT_TRUE(reg.has_skill("google_maps"));
    return true;
}

TEST(skill_registry_has_spider) {
    auto& reg = sw_skills::SkillRegistry::instance();
    ASSERT_TRUE(reg.has_skill("spider"));
    return true;
}

TEST(skill_registry_has_datasphere) {
    auto& reg = sw_skills::SkillRegistry::instance();
    ASSERT_TRUE(reg.has_skill("datasphere"));
    return true;
}

TEST(skill_registry_has_datasphere_serverless) {
    auto& reg = sw_skills::SkillRegistry::instance();
    ASSERT_TRUE(reg.has_skill("datasphere_serverless"));
    return true;
}

TEST(skill_registry_has_swml_transfer) {
    auto& reg = sw_skills::SkillRegistry::instance();
    ASSERT_TRUE(reg.has_skill("swml_transfer"));
    return true;
}

TEST(skill_registry_has_play_background_file) {
    auto& reg = sw_skills::SkillRegistry::instance();
    ASSERT_TRUE(reg.has_skill("play_background_file"));
    return true;
}

TEST(skill_registry_has_api_ninjas_trivia) {
    auto& reg = sw_skills::SkillRegistry::instance();
    ASSERT_TRUE(reg.has_skill("api_ninjas_trivia"));
    return true;
}

TEST(skill_registry_has_native_vector_search) {
    auto& reg = sw_skills::SkillRegistry::instance();
    ASSERT_TRUE(reg.has_skill("native_vector_search"));
    return true;
}

TEST(skill_registry_has_info_gatherer) {
    auto& reg = sw_skills::SkillRegistry::instance();
    ASSERT_TRUE(reg.has_skill("info_gatherer"));
    return true;
}

TEST(skill_registry_has_claude_skills) {
    auto& reg = sw_skills::SkillRegistry::instance();
    ASSERT_TRUE(reg.has_skill("claude_skills"));
    return true;
}

TEST(skill_registry_has_mcp_gateway) {
    auto& reg = sw_skills::SkillRegistry::instance();
    ASSERT_TRUE(reg.has_skill("mcp_gateway"));
    return true;
}

TEST(skill_registry_has_custom_skills) {
    auto& reg = sw_skills::SkillRegistry::instance();
    ASSERT_TRUE(reg.has_skill("custom_skills"));
    return true;
}

TEST(skill_registry_all_18_skills) {
    auto& reg = sw_skills::SkillRegistry::instance();
    auto skills = reg.list_skills();
    ASSERT_TRUE(skills.size() >= 18u);
    return true;
}

TEST(skill_registry_no_nonexistent) {
    auto& reg = sw_skills::SkillRegistry::instance();
    ASSERT_FALSE(reg.has_skill("nonexistent_skill"));
    return true;
}

// ========================================================================
// Skill creation and setup
// ========================================================================

TEST(skill_create_datetime) {
    auto& reg = sw_skills::SkillRegistry::instance();
    auto skill = reg.create("datetime");
    ASSERT_TRUE(skill != nullptr);
    ASSERT_EQ(skill->skill_name(), "datetime");
    ASSERT_FALSE(skill->supports_multiple_instances());
    ASSERT_TRUE(skill->setup(json::object()));
    return true;
}

TEST(skill_create_math) {
    auto& reg = sw_skills::SkillRegistry::instance();
    auto skill = reg.create("math");
    ASSERT_TRUE(skill != nullptr);
    ASSERT_TRUE(skill->setup(json::object()));
    auto tools = skill->register_tools();
    ASSERT_EQ(tools.size(), 1u);
    ASSERT_EQ(tools[0].name, "calculate");
    return true;
}

TEST(skill_math_calculate) {
    auto& reg = sw_skills::SkillRegistry::instance();
    auto skill = reg.create("math");
    skill->setup(json::object());
    auto tools = skill->register_tools();
    ASSERT_EQ(tools.size(), 1u);

    auto result = tools[0].handler(json::object({{"expression", "2 + 3"}}), json::object());
    auto j = result.to_json();
    ASSERT_TRUE(j["response"].get<std::string>().find("5") != std::string::npos);
    return true;
}

TEST(skill_datetime_tools) {
    auto& reg = sw_skills::SkillRegistry::instance();
    auto skill = reg.create("datetime");
    skill->setup(json::object());
    auto tools = skill->register_tools();
    ASSERT_EQ(tools.size(), 2u);
    ASSERT_EQ(tools[0].name, "get_current_time");
    ASSERT_EQ(tools[1].name, "get_current_date");
    return true;
}

TEST(skill_datetime_prompt_sections) {
    auto& reg = sw_skills::SkillRegistry::instance();
    auto skill = reg.create("datetime");
    skill->setup(json::object());
    auto sections = skill->get_prompt_sections();
    ASSERT_EQ(sections.size(), 1u);
    ASSERT_EQ(sections[0].title, "Date and Time Information");
    return true;
}

TEST(skill_web_search_multi_instance) {
    auto& reg = sw_skills::SkillRegistry::instance();
    auto skill = reg.create("web_search");
    ASSERT_TRUE(skill->supports_multiple_instances());
    return true;
}

TEST(skill_custom_skills_with_tools) {
    auto& reg = sw_skills::SkillRegistry::instance();
    auto skill = reg.create("custom_skills");
    json params = json::object({
        {"tools", json::array({
            json::object({
                {"name", "my_tool"},
                {"description", "My custom tool"},
                {"response", "Custom response"}
            })
        })}
    });
    ASSERT_TRUE(skill->setup(params));
    auto tools = skill->register_tools();
    ASSERT_EQ(tools.size(), 1u);
    ASSERT_EQ(tools[0].name, "my_tool");
    return true;
}

TEST(skill_info_gatherer_with_questions) {
    auto& reg = sw_skills::SkillRegistry::instance();
    auto skill = reg.create("info_gatherer");
    json params = json::object({
        {"questions", json::array({
            json::object({{"key_name", "name"}, {"question_text", "What is your name?"}}),
            json::object({{"key_name", "email"}, {"question_text", "What is your email?"}})
        })}
    });
    ASSERT_TRUE(skill->setup(params));
    auto tools = skill->register_tools();
    ASSERT_EQ(tools.size(), 2u); // start_questions + submit_answer
    return true;
}

// ========================================================================
// SkillManager
// ========================================================================

TEST(skill_manager_load) {
    sw_skills::SkillManager mgr;
    signalwire::agent::AgentBase agent;
    bool loaded = mgr.load_skill("datetime", json::object(), agent);
    ASSERT_TRUE(loaded);
    ASSERT_TRUE(mgr.is_loaded("datetime"));
    return true;
}

TEST(skill_manager_list_loaded) {
    sw_skills::SkillManager mgr;
    signalwire::agent::AgentBase agent;
    (void)mgr.load_skill("datetime", json::object(), agent);
    (void)mgr.load_skill("math", json::object(), agent);
    auto loaded = mgr.list_loaded();
    ASSERT_EQ(loaded.size(), 2u);
    return true;
}

TEST(skill_manager_unload) {
    sw_skills::SkillManager mgr;
    signalwire::agent::AgentBase agent;
    (void)mgr.load_skill("datetime", json::object(), agent);
    mgr.unload_skill("datetime");
    ASSERT_FALSE(mgr.is_loaded("datetime"));
    return true;
}

TEST(skill_manager_unknown_skill) {
    sw_skills::SkillManager mgr;
    signalwire::agent::AgentBase agent;
    bool loaded = mgr.load_skill("nonexistent", json::object(), agent);
    ASSERT_FALSE(loaded);
    return true;
}

TEST(skill_manager_no_duplicate_single_instance) {
    sw_skills::SkillManager mgr;
    signalwire::agent::AgentBase agent;
    (void)mgr.load_skill("datetime", json::object(), agent);
    bool second = mgr.load_skill("datetime", json::object(), agent);
    ASSERT_FALSE(second);
    return true;
}
