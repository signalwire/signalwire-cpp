// Wikipedia search skill tests
#include <atomic>
#include <chrono>
#include <thread>

#include "httplib.h"
#include "signalwire/skills/skill_registry.hpp"
namespace sw_skills = signalwire::skills;
using json = nlohmann::json;

TEST(skill_wikipedia_name) {
  auto skill = sw_skills::SkillRegistry::instance().create("wikipedia_search");
  ASSERT_EQ(skill->skill_name(), "wikipedia_search");
  return true;
}

TEST(skill_wikipedia_setup_no_params) {
  auto skill = sw_skills::SkillRegistry::instance().create("wikipedia_search");
  ASSERT_TRUE(skill->setup(json::object()));
  return true;
}

TEST(skill_wikipedia_registers_tool) {
  auto skill = sw_skills::SkillRegistry::instance().create("wikipedia_search");
  skill->setup(json::object());
  auto tools = skill->register_tools();
  ASSERT_EQ(tools.size(), 1u);
  ASSERT_EQ(tools[0].name, "search_wiki");
  return true;
}

// Drive the handler against a local fixture so we prove the skill issues
// real HTTP and parses the canned response, not just returns canned text.
TEST(skill_wikipedia_handler_with_query) {
  httplib::Server srv;
  std::atomic<bool> got_request{false};
  srv.Get("/w/api.php", [&](const httplib::Request&, httplib::Response& res) {
    got_request = true;
    res.set_content(
        R"JSON({"query":{"search":[{"title":"Albert Einstein","snippet":"physicist (1879-1955)"}]}})JSON",
        "application/json");
  });

  int port = 0;
  std::thread th([&] {
    port = srv.bind_to_any_port("127.0.0.1");
    srv.listen_after_bind();
  });
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  while (port == 0 && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  ASSERT_TRUE(port > 0);

  ::setenv("WIKIPEDIA_BASE_URL", ("http://127.0.0.1:" + std::to_string(port)).c_str(), 1);
  auto skill = sw_skills::SkillRegistry::instance().create("wikipedia_search");
  skill->setup(json::object());
  auto tools = skill->register_tools();
  auto result = tools[0].handler(json::object({{"query", "Albert Einstein"}}), json::object());
  auto resp = result.to_json()["response"].get<std::string>();

  srv.stop();
  th.join();
  ::unsetenv("WIKIPEDIA_BASE_URL");

  ASSERT_TRUE(got_request);
  ASSERT_TRUE(resp.find("Albert Einstein") != std::string::npos);
  ASSERT_TRUE(resp.find("physicist") != std::string::npos);  // proves parse
  return true;
}

// Empty query short-circuits before any HTTP — confirm the no-results
// message reaches the caller. The skill must NOT contact the upstream
// for an empty query (no fixture needed).
TEST(skill_wikipedia_empty_query_returns_response) {
  auto skill = sw_skills::SkillRegistry::instance().create("wikipedia_search");
  skill->setup(json::object());
  auto tools = skill->register_tools();
  auto result = tools[0].handler(json::object({{"query", ""}}), json::object());
  auto resp = result.to_json()["response"].get<std::string>();
  ASSERT_TRUE(resp.find("No Wikipedia") != std::string::npos);
  return true;
}

TEST(skill_wikipedia_prompt_sections) {
  auto skill = sw_skills::SkillRegistry::instance().create("wikipedia_search");
  skill->setup(json::object());
  auto sections = skill->get_prompt_sections();
  ASSERT_TRUE(sections.size() >= 1u);
  return true;
}
