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

// load_skill's trailing two parameters are OPTIONAL (reference
// skill_manager.py:26) — these omit BOTH, so the defaults are what is
// exercised, not arguments the caller supplied.
TEST(skill_manager_load) {
    signalwire::agent::AgentBase agent;
    sw_skills::SkillManager mgr(agent);
    bool loaded = mgr.load_skill("datetime");
    ASSERT_TRUE(loaded);
    ASSERT_TRUE(mgr.is_loaded("datetime"));
    return true;
}

TEST(skill_manager_list_loaded) {
    signalwire::agent::AgentBase agent;
    sw_skills::SkillManager mgr(agent);
    (void)mgr.load_skill("datetime");
    (void)mgr.load_skill("math");
    auto loaded = mgr.list_loaded();
    ASSERT_EQ(loaded.size(), 2u);
    return true;
}

TEST(skill_manager_unload) {
    signalwire::agent::AgentBase agent;
    sw_skills::SkillManager mgr(agent);
    (void)mgr.load_skill("datetime");
    mgr.unload_skill("datetime");
    ASSERT_FALSE(mgr.is_loaded("datetime"));
    return true;
}

TEST(skill_manager_unknown_skill) {
    signalwire::agent::AgentBase agent;
    sw_skills::SkillManager mgr(agent);
    bool loaded = mgr.load_skill("nonexistent");
    ASSERT_FALSE(loaded);
    return true;
}

TEST(skill_manager_no_duplicate_single_instance) {
    signalwire::agent::AgentBase agent;
    sw_skills::SkillManager mgr(agent);
    (void)mgr.load_skill("datetime");
    bool second = mgr.load_skill("datetime");
    ASSERT_FALSE(second);
    return true;
}

// The reference's `skill_class` argument short-circuits the registry lookup.
// C++'s spelling is a SkillFactory. Passing one must bypass the name lookup
// entirely — proven by loading under a name the registry does NOT know.
TEST(skill_manager_explicit_skill_class_bypasses_registry) {
    signalwire::agent::AgentBase agent;
    sw_skills::SkillManager mgr(agent);
    ASSERT_FALSE(sw_skills::SkillRegistry::instance().has_skill("not_in_registry"));

    sw_skills::SkillFactory factory = []() -> std::unique_ptr<sw_skills::SkillBase> {
        return sw_skills::SkillRegistry::instance().create("math");
    };
    bool loaded = mgr.load_skill("not_in_registry", factory);
    ASSERT_TRUE(loaded);
    ASSERT_TRUE(mgr.is_loaded("not_in_registry"));
    return true;
}

// params defaults to absent and must normalise to an empty object, not null.
TEST(skill_manager_params_default_is_empty_object) {
    signalwire::agent::AgentBase agent;
    sw_skills::SkillManager mgr(agent);
    ASSERT_TRUE(mgr.load_skill("datetime"));
    sw_skills::SkillBase* skill = mgr.get_skill("datetime");
    ASSERT_TRUE(skill != nullptr);
    ASSERT_TRUE(skill->params().is_object());
    ASSERT_TRUE(skill->params().empty());
    return true;
}

// Reference parity: SkillBase.__init__(agent, params) stores `self.agent` and
// `self.params` as public instance attributes, and SkillManager.__init__(agent)
// stores `self.agent`. The port's skills received params only through
// `setup(params)` and never held the agent at all, so a loaded skill could not
// reach back to the agent that owns it.
TEST(skill_manager_binds_agent_and_params_onto_skill) {
    signalwire::agent::AgentBase agent;
    sw_skills::SkillManager mgr(agent);
    ASSERT_TRUE(mgr.agent() == &agent);

    json params = json::object({{"prefix", "dt"}});
    bool loaded = mgr.load_skill("datetime", std::nullopt, params);
    ASSERT_TRUE(loaded);

    sw_skills::SkillBase* skill = mgr.get_skill("datetime");
    ASSERT_TRUE(skill != nullptr);
    ASSERT_TRUE(skill->agent() == &agent);
    ASSERT_EQ(skill->params()["prefix"], "dt");
    return true;
}

// A default-constructed manager has no bound agent. The reference's manager is
// always agent-bound (SkillManager.__init__(agent)), so load_skill must fail
// LOUD here rather than silently loading into nothing.
TEST(skill_manager_default_has_no_bound_agent) {
    sw_skills::SkillManager mgr;
    ASSERT_TRUE(mgr.agent() == nullptr);
    ASSERT_FALSE(mgr.load_skill("math"));
    ASSERT_FALSE(mgr.is_loaded("math"));
    return true;
}
