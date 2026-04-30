// Skill registry tests — enumeration, creation, validation
#include "signalwire/skills/skill_registry.hpp"
#include "signalwire/skills/skill_manager.hpp"
#include "signalwire/agent/agent_base.hpp"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>

namespace sw_skills = signalwire::skills;
using json = nlohmann::json;

TEST(skill_registry_singleton) {
    auto& r1 = sw_skills::SkillRegistry::instance();
    auto& r2 = sw_skills::SkillRegistry::instance();
    ASSERT_EQ(&r1, &r2);
    return true;
}

TEST(skill_registry_all_18_skills_present) {
    auto& reg = sw_skills::SkillRegistry::instance();
    auto all = reg.list_skills();
    ASSERT_TRUE(all.size() >= 18u);

    // Check each expected skill
    std::vector<std::string> expected = {
        "datetime", "math", "joke", "weather_api", "web_search",
        "wikipedia_search", "google_maps", "spider", "datasphere",
        "datasphere_serverless", "swml_transfer", "play_background_file",
        "api_ninjas_trivia", "native_vector_search", "info_gatherer",
        "claude_skills", "mcp_gateway", "custom_skills"
    };
    for (const auto& name : expected) {
        ASSERT_TRUE(reg.has_skill(name));
    }
    return true;
}

TEST(skill_registry_create_returns_null_for_unknown) {
    auto& reg = sw_skills::SkillRegistry::instance();
    auto skill = reg.create("totally_fake_skill");
    ASSERT_TRUE(skill == nullptr);
    return true;
}

TEST(skill_registry_create_returns_unique_instances) {
    auto& reg = sw_skills::SkillRegistry::instance();
    auto s1 = reg.create("datetime");
    auto s2 = reg.create("datetime");
    ASSERT_TRUE(s1 != nullptr);
    ASSERT_TRUE(s2 != nullptr);
    ASSERT_TRUE(s1.get() != s2.get());
    return true;
}

// ========================================================================
// SkillManager tests
// ========================================================================

TEST(skill_manager_load_and_check) {
    sw_skills::SkillManager mgr;
    signalwire::agent::AgentBase agent;
    bool ok = mgr.load_skill("datetime", json::object(), agent);
    ASSERT_TRUE(ok);
    ASSERT_TRUE(mgr.is_loaded("datetime"));
    return true;
}

TEST(skill_manager_unload_skill) {
    sw_skills::SkillManager mgr;
    signalwire::agent::AgentBase agent;
    mgr.load_skill("math", json::object(), agent);
    mgr.unload_skill("math");
    ASSERT_FALSE(mgr.is_loaded("math"));
    return true;
}

TEST(skill_manager_list_loaded_skills) {
    sw_skills::SkillManager mgr;
    signalwire::agent::AgentBase agent;
    mgr.load_skill("datetime", json::object(), agent);
    mgr.load_skill("math", json::object(), agent);
    auto loaded = mgr.list_loaded();
    ASSERT_EQ(loaded.size(), 2u);
    return true;
}

TEST(skill_manager_cleanup_all) {
    sw_skills::SkillManager mgr;
    signalwire::agent::AgentBase agent;
    mgr.load_skill("datetime", json::object(), agent);
    mgr.load_skill("math", json::object(), agent);
    mgr.cleanup_all();
    ASSERT_FALSE(mgr.is_loaded("datetime"));
    ASSERT_FALSE(mgr.is_loaded("math"));
    return true;
}

TEST(skill_manager_unknown_skill_returns_false) {
    sw_skills::SkillManager mgr;
    signalwire::agent::AgentBase agent;
    ASSERT_FALSE(mgr.load_skill("nonexistent_skill", json::object(), agent));
    return true;
}

TEST(skill_manager_duplicate_single_instance_rejected) {
    sw_skills::SkillManager mgr;
    signalwire::agent::AgentBase agent;
    mgr.load_skill("datetime", json::object(), agent);
    ASSERT_FALSE(mgr.load_skill("datetime", json::object(), agent));
    return true;
}

// ========================================================================
// add_skill_directory — parity with Python's
// signalwire.skills.registry.SkillRegistry.add_skill_directory
// ========================================================================

namespace {
// tmp dir helper for parity tests; mkdtemp returns a unique directory.
std::string make_temp_dir() {
    char tmpl[] = "/tmp/swcpp_skill_dir_XXXXXX";
    char* dir = mkdtemp(tmpl);
    if (!dir) return std::string();
    return std::string(dir);
}
} // namespace

TEST(skill_registry_add_skill_directory_valid) {
    auto& reg = sw_skills::SkillRegistry::instance();
    std::string dir = make_temp_dir();
    ASSERT_FALSE(dir.empty());
    reg.add_skill_directory(dir);
    auto paths = reg.external_paths();
    bool found = false;
    for (const auto& p : paths) {
        if (p == dir) { found = true; break; }
    }
    ASSERT_TRUE(found);
    // cleanup: remove directory (don't leak between tests)
    rmdir(dir.c_str());
    return true;
}

TEST(skill_registry_add_skill_directory_not_exists) {
    auto& reg = sw_skills::SkillRegistry::instance();
    bool threw = false;
    try {
        reg.add_skill_directory("/no/such/path/swcpp_abc123_does_not_exist");
    } catch (const std::invalid_argument& e) {
        threw = true;
        std::string msg = e.what();
        ASSERT_TRUE(msg.find("does not exist") != std::string::npos);
    }
    ASSERT_TRUE(threw);
    return true;
}

TEST(skill_registry_add_skill_directory_not_a_directory) {
    auto& reg = sw_skills::SkillRegistry::instance();
    // Create a regular file
    char file_tmpl[] = "/tmp/swcpp_skill_file_XXXXXX";
    int fd = mkstemp(file_tmpl);
    ASSERT_TRUE(fd >= 0);
    close(fd);
    bool threw = false;
    try {
        reg.add_skill_directory(file_tmpl);
    } catch (const std::invalid_argument& e) {
        threw = true;
        std::string msg = e.what();
        ASSERT_TRUE(msg.find("not a directory") != std::string::npos);
    }
    unlink(file_tmpl);
    ASSERT_TRUE(threw);
    return true;
}

TEST(skill_registry_add_skill_directory_dedup) {
    auto& reg = sw_skills::SkillRegistry::instance();
    std::string dir = make_temp_dir();
    ASSERT_FALSE(dir.empty());
    auto before = reg.external_paths();
    int before_count = 0;
    for (const auto& p : before) if (p == dir) before_count++;
    reg.add_skill_directory(dir);
    reg.add_skill_directory(dir); // second call, should not duplicate
    auto after = reg.external_paths();
    int after_count = 0;
    for (const auto& p : after) if (p == dir) after_count++;
    ASSERT_EQ(after_count, before_count + 1);
    rmdir(dir.c_str());
    return true;
}
