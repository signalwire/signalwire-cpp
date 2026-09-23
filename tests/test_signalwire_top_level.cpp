// Tests for the top-level convenience entry points exposed in
// ``signalwire`` namespace — RestClient, register_skill,
// add_skill_directory, list_skills_with_params. These mirror Python's
// package-level signalwire/__init__.py factory + skill registry helpers.

#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>

#include "signalwire/signalwire.hpp"

namespace {

// Helper: make a unique temp directory.
std::string make_tmp_dir() {
  char tmpl[] = "/tmp/sw-cpp-skill-dir-XXXXXX";
  if (!mkdtemp(tmpl)) {
    return "";
  }
  return std::string(tmpl);
}

// Trivial SkillBase subclass for the register_skill test.
class TopLevelDummySkill : public signalwire::skills::SkillBase {
 public:
  std::string skill_name() const override { return "top_level_dummy_skill_cpp"; }
  std::string skill_description() const override { return "Dummy skill for top-level parity test"; }
  bool setup(const signalwire::skills::json&) override { return true; }
  std::vector<signalwire::swaig::ToolDefinition> register_tools() override { return {}; }
};

}  // namespace

TEST(signalwire_top_level_rest_client_keyword_credentials) {
  std::map<std::string, std::string> kwargs = {
      {"project", "p-123"},
      {"token", "t-456"},
      {"space", "demo.signalwire.com"},
  };
  auto client = signalwire::RestClient({}, kwargs);
  ASSERT_EQ(client.project_id(), std::string("p-123"));
  return true;
}

TEST(signalwire_top_level_rest_client_positional_credentials) {
  auto client = signalwire::RestClient({"proj", "tok", "pos.signalwire.com"}, {});
  ASSERT_EQ(client.project_id(), std::string("proj"));
  return true;
}

TEST(signalwire_top_level_rest_client_throws_on_missing_credentials) {
  // Clear env vars so the function actually fails.
  unsetenv("SIGNALWIRE_PROJECT_ID");
  unsetenv("SIGNALWIRE_API_TOKEN");
  unsetenv("SIGNALWIRE_SPACE");
  bool threw = false;
  try {
    auto client = signalwire::RestClient({}, {});
    (void)client;
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  ASSERT_TRUE(threw);
  return true;
}

TEST(signalwire_top_level_add_skill_directory_records_path) {
  std::string tmp = make_tmp_dir();
  ASSERT_FALSE(tmp.empty());
  signalwire::add_skill_directory(tmp);
  auto& reg = signalwire::skills::SkillRegistry::instance();
  auto paths = reg.external_paths();
  bool found = false;
  for (const auto& p : paths) {
    if (p == tmp) {
      found = true;
      break;
    }
  }
  rmdir(tmp.c_str());
  ASSERT_TRUE(found);
  return true;
}

TEST(signalwire_top_level_add_skill_directory_throws_on_missing) {
  bool threw = false;
  try {
    signalwire::add_skill_directory("/no/such/path/zzz_cpp_top_level");
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  ASSERT_TRUE(threw);
  return true;
}

TEST(signalwire_top_level_register_skill_records_class) {
  signalwire::register_skill([]() -> std::unique_ptr<signalwire::skills::SkillBase> {
    return std::make_unique<TopLevelDummySkill>();
  });
  auto& reg = signalwire::skills::SkillRegistry::instance();
  ASSERT_TRUE(reg.has_skill("top_level_dummy_skill_cpp"));
  return true;
}

TEST(signalwire_top_level_list_skills_with_params_returns_schema) {
  auto schema = signalwire::list_skills_with_params();
  ASSERT_TRUE(!schema.empty());
  for (const auto& [name, entry] : schema) {
    auto it = entry.find("name");
    ASSERT_TRUE(it != entry.end());
    ASSERT_EQ(it->second, name);
  }
  return true;
}
