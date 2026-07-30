// Skill registry tests — enumeration, creation, validation
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>

#include "signalwire/agent/agent_base.hpp"
#include "signalwire/skills/skill_manager.hpp"
#include "signalwire/skills/skill_registry.hpp"

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
  std::vector<std::string> expected = {"datetime",
                                       "math",
                                       "joke",
                                       "weather_api",
                                       "web_search",
                                       "wikipedia_search",
                                       "google_maps",
                                       "spider",
                                       "datasphere",
                                       "datasphere_serverless",
                                       "swml_transfer",
                                       "play_background_file",
                                       "api_ninjas_trivia",
                                       "native_vector_search",
                                       "info_gatherer",
                                       "claude_skills",
                                       "mcp_gateway",
                                       "custom_skills"};
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
  signalwire::agent::AgentBase agent;
  sw_skills::SkillManager mgr(agent);
  bool ok = mgr.load_skill("datetime");
  ASSERT_TRUE(ok);
  ASSERT_TRUE(mgr.is_loaded("datetime"));
  return true;
}

TEST(skill_manager_unload_skill) {
  signalwire::agent::AgentBase agent;
  sw_skills::SkillManager mgr(agent);
  (void)mgr.load_skill("math");
  mgr.unload_skill("math");
  ASSERT_FALSE(mgr.is_loaded("math"));
  return true;
}

TEST(skill_manager_list_loaded_skills) {
  signalwire::agent::AgentBase agent;
  sw_skills::SkillManager mgr(agent);
  (void)mgr.load_skill("datetime");
  (void)mgr.load_skill("math");
  auto loaded = mgr.list_loaded();
  ASSERT_EQ(loaded.size(), 2u);
  return true;
}

TEST(skill_manager_cleanup_all) {
  signalwire::agent::AgentBase agent;
  sw_skills::SkillManager mgr(agent);
  (void)mgr.load_skill("datetime");
  (void)mgr.load_skill("math");
  mgr.cleanup_all();
  ASSERT_FALSE(mgr.is_loaded("datetime"));
  ASSERT_FALSE(mgr.is_loaded("math"));
  return true;
}

TEST(skill_manager_unknown_skill_returns_false) {
  signalwire::agent::AgentBase agent;
  sw_skills::SkillManager mgr(agent);
  ASSERT_FALSE(mgr.load_skill("nonexistent_skill"));
  return true;
}

TEST(skill_manager_duplicate_single_instance_rejected) {
  signalwire::agent::AgentBase agent;
  sw_skills::SkillManager mgr(agent);
  (void)mgr.load_skill("datetime");
  ASSERT_FALSE(mgr.load_skill("datetime"));
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
}  // namespace

TEST(skill_registry_add_skill_directory_valid) {
  auto& reg = sw_skills::SkillRegistry::instance();
  std::string dir = make_temp_dir();
  ASSERT_FALSE(dir.empty());
  reg.add_skill_directory(dir);
  auto paths = reg.external_paths();
  bool found = false;
  for (const auto& p : paths) {
    if (p == dir) {
      found = true;
      break;
    }
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
  for (const auto& p : before)
    if (p == dir) before_count++;
  reg.add_skill_directory(dir);
  reg.add_skill_directory(dir);  // second call, should not duplicate
  auto after = reg.external_paths();
  int after_count = 0;
  for (const auto& p : after)
    if (p == dir) after_count++;
  ASSERT_EQ(after_count, before_count + 1);
  rmdir(dir.c_str());
  return true;
}

// ========================================================================
// Duplicate-registration guard
// ========================================================================
//
// ``register_skill`` used to overwrite silently on a name collision. That is
// what let two different classes both claim ``"spider"`` — one in
// ``src/skills/builtin/spider.cpp``, one in ``src/skills/skill_registry.cpp``
// — from two different translation units. Static-init order ACROSS TUs is
// unspecified in C++, so which implementation actually ran was decided by link
// order, and the surface enumerator (which reads ``builtin/``) was projecting
// the class that was NOT running. Parity can pass against dead code that way.
//
// A second registration of an existing skill name is always a bug, so it must
// FAIL LOUD rather than silently replace the incumbent.

TEST(skill_registry_duplicate_registration_throws) {
  auto& reg = sw_skills::SkillRegistry::instance();
  const std::string name = "dup_guard_probe_skill";
  ASSERT_FALSE(reg.has_skill(name));

  // First registration succeeds.
  reg.register_skill(name, []() -> std::unique_ptr<sw_skills::SkillBase> { return nullptr; });
  ASSERT_TRUE(reg.has_skill(name));

  // Second registration of the SAME name must throw, not overwrite.
  bool threw = false;
  try {
    reg.register_skill(name, []() -> std::unique_ptr<sw_skills::SkillBase> { return nullptr; });
  } catch (const std::invalid_argument& e) {
    threw = true;
    std::string msg = e.what();
    // The message must name the offending skill so the collision is
    // diagnosable from the abort alone.
    ASSERT_TRUE(msg.find("Duplicate skill registration") != std::string::npos);
    ASSERT_TRUE(msg.find(name) != std::string::npos);
  }
  ASSERT_TRUE(threw);
  return true;
}

// Every registered built-in name must resolve to the implementation in
// ``src/skills/builtin/<name>.cpp`` — the same file the surface enumerator
// reads. ``skill_description()`` is the discriminator: the deleted duplicate
// classes in skill_registry.cpp carried DIFFERENT, abbreviated descriptions
// ("Web scraping", "DataSphere RAG", "Gather info", "MCP bridge", …), so if a
// second implementation ever wins the registration again, these strings change
// and this test goes red.
TEST(skill_registry_builtin_impl_is_the_live_one) {
  auto& reg = sw_skills::SkillRegistry::instance();
  const std::vector<std::pair<std::string, std::string>> expected = {
      {"spider", "Fast web scraping and crawling capabilities"},
      {"datasphere", "Search knowledge using SignalWire DataSphere RAG stack"},
      {"info_gatherer", "Gather answers to a configurable list of questions"},
      {"mcp_gateway", "Bridge MCP servers with SWAIG functions"},
      {"swml_transfer", "Transfer calls between agents based on pattern matching"},
      {"play_background_file", "Control background file playback"},
      {"custom_skills", "Register user-defined custom tools"},
      {"google_maps", "Validate addresses and compute driving routes using Google Maps"},
      {"wikipedia_search",
       "Search Wikipedia for information about a topic and get article summaries"},
  };
  for (const auto& [name, desc] : expected) {
    auto skill = reg.create(name);
    ASSERT_TRUE(skill != nullptr);
    ASSERT_EQ(skill->skill_description(), desc);
  }
  return true;
}
