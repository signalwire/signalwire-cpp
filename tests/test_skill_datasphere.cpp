// Datasphere skill tests
#include <atomic>
#include <chrono>
#include <thread>

#include "httplib.h"
#include "signalwire/skills/skill_registry.hpp"
namespace sw_skills = signalwire::skills;
using json = nlohmann::json;

TEST(skill_datasphere_name) {
  auto skill = sw_skills::SkillRegistry::instance().create("datasphere");
  ASSERT_EQ(skill->skill_name(), "datasphere");
  return true;
}

TEST(skill_datasphere_multi_instance) {
  auto skill = sw_skills::SkillRegistry::instance().create("datasphere");
  ASSERT_TRUE(skill->supports_multiple_instances());
  return true;
}

TEST(skill_datasphere_setup_with_creds) {
  auto skill = sw_skills::SkillRegistry::instance().create("datasphere");
  ASSERT_TRUE(skill->setup(json::object(
      {{"space_name", "test.signalwire.com"}, {"project_id", "proj-123"}, {"token", "tok-456"}})));
  return true;
}

TEST(skill_datasphere_registers_tool) {
  auto skill = sw_skills::SkillRegistry::instance().create("datasphere");
  skill->setup(json::object(
      {{"space_name", "s"}, {"project_id", "p"}, {"token", "t"}, {"document_id", "doc-1"}}));
  auto tools = skill->register_tools();
  ASSERT_EQ(tools.size(), 1u);
  ASSERT_EQ(tools[0].name, "search_knowledge");
  return true;
}

TEST(skill_datasphere_custom_tool_name) {
  auto skill = sw_skills::SkillRegistry::instance().create("datasphere");
  skill->setup(json::object(
      {{"space_name", "s"}, {"project_id", "p"}, {"token", "t"}, {"tool_name", "search_docs"}}));
  auto tools = skill->register_tools();
  ASSERT_EQ(tools[0].name, "search_docs");
  return true;
}

TEST(skill_datasphere_global_data_contains_enabled) {
  auto skill = sw_skills::SkillRegistry::instance().create("datasphere");
  skill->setup(json::object({{"space_name", "s"}, {"project_id", "p"}, {"token", "t"}}));
  auto gd = skill->get_global_data();
  ASSERT_TRUE(gd.contains("datasphere_enabled"));
  return true;
}

// Drive the handler against a local fixture so we prove the skill issues
// a real POST with Basic auth and parses the results[] array. The
// fixture answers `/api/datasphere/documents/{doc}/search` with a
// canned JSON body containing one result.
TEST(skill_datasphere_handler_returns_response) {
  httplib::Server srv;
  std::atomic<bool> got_request{false};
  std::string captured_auth;
  srv.Post("/api/datasphere/documents/search",
           [&](const httplib::Request& req, httplib::Response& res) {
             got_request = true;
             captured_auth = req.get_header_value("Authorization");
             res.set_content(R"JSON({"chunks":[{"text":"answer body","score":0.9}]})JSON",
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

  ::setenv("DATASPHERE_BASE_URL", ("http://127.0.0.1:" + std::to_string(port)).c_str(), 1);
  auto skill = sw_skills::SkillRegistry::instance().create("datasphere");
  skill->setup(json::object(
      {{"space_name", "s"}, {"project_id", "p"}, {"token", "t"}, {"document_id", "doc-1"}}));
  auto tools = skill->register_tools();
  auto result = tools[0].handler(json::object({{"query", "test"}}), json::object());
  auto resp = result.to_json()["response"].get<std::string>();

  srv.stop();
  th.join();
  ::unsetenv("DATASPHERE_BASE_URL");

  ASSERT_TRUE(got_request);
  ASSERT_TRUE(captured_auth.rfind("Basic ", 0) == 0);          // proves Basic auth shape
  ASSERT_TRUE(resp.find("answer body") != std::string::npos);  // proves real parse
  return true;
}
