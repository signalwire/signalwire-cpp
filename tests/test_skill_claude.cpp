// Claude skills skill tests
#include "signalwire/skills/skill_registry.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>

namespace sw_skills = signalwire::skills;
namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

// Create a unique, repo-local scratch dir (NOT a shared /tmp) for a test's
// sample skills tree, and clean it up via RAII.
struct TempSkillsDir {
    fs::path root;
    explicit TempSkillsDir(const std::string& tag) {
        auto ns = std::chrono::steady_clock::now().time_since_epoch().count();
        root = fs::path(".sw-tmp") / ("claude_skills_" + tag + "_" + std::to_string(ns));
        fs::create_directories(root);
    }
    ~TempSkillsDir() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
    // Write a SKILL.md under root/<dir_name>/SKILL.md with the given contents.
    fs::path add_skill(const std::string& dir_name, const std::string& skill_md) const {
        fs::path d = root / dir_name;
        fs::create_directories(d);
        std::ofstream out(d / "SKILL.md");
        out << skill_md;
        out.close();
        return d;
    }
};

}  // namespace

TEST(skill_claude_name) {
    auto skill = sw_skills::SkillRegistry::instance().create("claude_skills");
    ASSERT_EQ(skill->skill_name(), "claude_skills");
    return true;
}

TEST(skill_claude_multi_instance) {
    auto skill = sw_skills::SkillRegistry::instance().create("claude_skills");
    ASSERT_TRUE(skill->supports_multiple_instances());
    return true;
}

TEST(skill_claude_setup_requires_path) {
    auto skill = sw_skills::SkillRegistry::instance().create("claude_skills");
    ASSERT_FALSE(skill->setup(json::object()));
    return true;
}

TEST(skill_claude_setup_with_path) {
    TempSkillsDir tmp("setup");
    auto skill = sw_skills::SkillRegistry::instance().create("claude_skills");
    ASSERT_TRUE(skill->setup(json::object({{"skills_path", tmp.root.string()}})));
    return true;
}

// An empty (but existing) skills dir discovers no skills -> no tools declared.
TEST(skill_claude_empty_dir_no_tools) {
    TempSkillsDir tmp("empty");
    auto skill = sw_skills::SkillRegistry::instance().create("claude_skills");
    skill->setup(json::object({{"skills_path", tmp.root.string()}}));
    auto tools = skill->register_tools();
    ASSERT_EQ(tools.size(), 0u);
    return true;
}

// ── SKILL.md discovery (the real implementation) ───────────────────
//
// A directory containing a SKILL.md with YAML frontmatter is discovered and
// declared as one SWAIG tool: name = {tool_prefix}{sanitized-name}, the
// description comes from the frontmatter, and the handler returns the SKILL.md
// body. Native execution of skill code is impossible (AOT) — the port only
// discovers + declares the tool and serves its instructions.

TEST(skill_claude_discovers_skill_md) {
    TempSkillsDir tmp("discover");
    tmp.add_skill("weather-lookup",
                  "---\n"
                  "name: weather-lookup\n"
                  "description: Look up the weather for a city\n"
                  "---\n\n"
                  "Ask the user for a city, then report the forecast.\n");

    auto skill = sw_skills::SkillRegistry::instance().create("claude_skills");
    ASSERT_TRUE(skill->setup(json::object({{"skills_path", tmp.root.string()}})));
    auto tools = skill->register_tools();

    ASSERT_EQ(tools.size(), 1u);
    // Default prefix "claude_" + sanitized name.
    ASSERT_EQ(tools[0].name, "claude_weather_lookup");
    // Description came from the frontmatter, not a generic fallback.
    ASSERT_EQ(tools[0].description, "Look up the weather for a city");
    // Parameters declare a required "arguments" string.
    ASSERT_TRUE(tools[0].parameters["properties"].contains("arguments"));
    ASSERT_EQ(tools[0].parameters["required"][0].get<std::string>(), "arguments");

    // Handler returns the SKILL.md body (+ the caller's arguments).
    auto result =
        tools[0].handler(json::object({{"arguments", "in Boston"}}), json::object());
    auto resp = result.to_json()["response"].get<std::string>();
    ASSERT_TRUE(resp.find("Ask the user for a city") != std::string::npos);
    ASSERT_TRUE(resp.find("in Boston") != std::string::npos);
    return true;
}

TEST(skill_claude_custom_prefix_and_dir_fallback_name) {
    TempSkillsDir tmp("prefix");
    // No "name" in frontmatter -> the directory name is the fallback.
    tmp.add_skill("triage",
                  "---\n"
                  "description: Triage an incoming ticket\n"
                  "---\n\n"
                  "Steps to triage.\n");

    auto skill = sw_skills::SkillRegistry::instance().create("claude_skills");
    skill->setup(json::object({{"skills_path", tmp.root.string()}, {"tool_prefix", "cc_"}}));
    auto tools = skill->register_tools();
    ASSERT_EQ(tools.size(), 1u);
    ASSERT_EQ(tools[0].name, "cc_triage");
    ASSERT_EQ(tools[0].description, "Triage an incoming ticket");
    return true;
}

TEST(skill_claude_discovers_multiple_skills) {
    TempSkillsDir tmp("multi");
    tmp.add_skill("alpha", "---\nname: alpha\ndescription: A\n---\nBody A\n");
    tmp.add_skill("beta", "---\nname: beta\ndescription: B\n---\nBody B\n");

    auto skill = sw_skills::SkillRegistry::instance().create("claude_skills");
    skill->setup(json::object({{"skills_path", tmp.root.string()}}));
    auto tools = skill->register_tools();
    ASSERT_EQ(tools.size(), 2u);
    return true;
}
