// Spider skill tests
#include <atomic>
#include <chrono>
#include <thread>

#include "httplib.h"
#include "signalwire/skills/skill_registry.hpp"
namespace sw_skills = signalwire::skills;
using json = nlohmann::json;

TEST(skill_spider_name) {
  auto skill = sw_skills::SkillRegistry::instance().create("spider");
  ASSERT_EQ(skill->skill_name(), "spider");
  return true;
}

TEST(skill_spider_multi_instance) {
  auto skill = sw_skills::SkillRegistry::instance().create("spider");
  ASSERT_TRUE(skill->supports_multiple_instances());
  return true;
}

TEST(skill_spider_setup) {
  auto skill = sw_skills::SkillRegistry::instance().create("spider");
  ASSERT_TRUE(skill->setup(json::object()));
  return true;
}

TEST(skill_spider_registers_tools) {
  auto skill = sw_skills::SkillRegistry::instance().create("spider");
  skill->setup(json::object());
  auto tools = skill->register_tools();
  ASSERT_TRUE(tools.size() >= 1u);
  // First tool should contain "scrape"
  ASSERT_TRUE(tools[0].name.find("scrape") != std::string::npos);
  return true;
}

// Drive the handler against a local fixture so we prove the skill issues
// a real GET to the URL the LLM passes (with SPIDER_BASE_URL rewriting
// the host to point at the loopback). The fixture serves an HTML page;
// the skill must strip the tags and return readable text.
TEST(skill_spider_handler_works) {
  httplib::Server srv;
  std::atomic<bool> got_request{false};
  srv.Get("/page", [&](const httplib::Request&, httplib::Response& res) {
    got_request = true;
    res.set_content("<html><body><h1>Hello</h1><p>real content</p></body></html>", "text/html");
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

  ::setenv("SPIDER_BASE_URL", ("http://127.0.0.1:" + std::to_string(port)).c_str(), 1);
  auto skill = sw_skills::SkillRegistry::instance().create("spider");
  skill->setup(json::object());
  auto tools = skill->register_tools();
  ASSERT_TRUE(tools.size() >= 1u);
  auto result =
      tools[0].handler(json::object({{"url", "https://example.com/page"}}), json::object());
  auto resp = result.to_json()["response"].get<std::string>();

  srv.stop();
  th.join();
  ::unsetenv("SPIDER_BASE_URL");

  ASSERT_TRUE(got_request);
  ASSERT_TRUE(resp.find("Hello") != std::string::npos);  // proves HTML strip + real fetch
  ASSERT_TRUE(resp.find("real content") != std::string::npos);
  return true;
}

TEST(skill_spider_has_hints) {
  auto skill = sw_skills::SkillRegistry::instance().create("spider");
  skill->setup(json::object());
  auto hints = skill->get_hints();
  ASSERT_TRUE(hints.size() >= 1u);
  return true;
}

// ``remove_xpaths`` — the reference PREFILLS this list in __init__ and drops each
// matched element (tag AND its body) before extracting text. Prove the observable
// effect: script source, CSS, and nav/header/footer/aside/noscript chrome must NOT
// reach the scraped text, while the real page body must. Before this landed the naive
// tag-strip turned `<script>` bodies into "scraped content".
TEST(skill_spider_remove_xpaths_drops_script_style_and_chrome) {
  httplib::Server srv;
  srv.Get("/page", [&](const httplib::Request&, httplib::Response& res) {
    res.set_content(
        "<html><head>"
        "<style>.secret_css_token{color:red}</style>"
        "<script>var secret_js_token = 1;</script>"
        "</head><body>"
        "<nav>secret_nav_token</nav>"
        "<header>secret_header_token</header>"
        "<aside>secret_aside_token</aside>"
        "<noscript>secret_noscript_token</noscript>"
        "<p>keeper body text</p>"
        "<footer>secret_footer_token</footer>"
        "</body></html>",
        "text/html");
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

  ::setenv("SPIDER_BASE_URL", ("http://127.0.0.1:" + std::to_string(port)).c_str(), 1);
  auto skill = sw_skills::SkillRegistry::instance().create("spider");
  skill->setup(json::object());
  auto tools = skill->register_tools();
  ASSERT_TRUE(tools.size() >= 1u);
  auto result =
      tools[0].handler(json::object({{"url", "https://example.com/page"}}), json::object());
  auto resp = result.to_json()["response"].get<std::string>();

  srv.stop();
  th.join();
  ::unsetenv("SPIDER_BASE_URL");

  // Every default remove_xpaths entry: //script //style //nav //header //footer
  // //aside //noscript — content dropped, not merely untagged.
  ASSERT_TRUE(resp.find("secret_js_token") == std::string::npos);
  ASSERT_TRUE(resp.find("secret_css_token") == std::string::npos);
  ASSERT_TRUE(resp.find("secret_nav_token") == std::string::npos);
  ASSERT_TRUE(resp.find("secret_header_token") == std::string::npos);
  ASSERT_TRUE(resp.find("secret_aside_token") == std::string::npos);
  ASSERT_TRUE(resp.find("secret_noscript_token") == std::string::npos);
  ASSERT_TRUE(resp.find("secret_footer_token") == std::string::npos);
  // …and the real body survives, so the fold is not just "drop everything".
  ASSERT_TRUE(resp.find("keeper body text") != std::string::npos);
  return true;
}

// The surviving (and now ONLY) spider implementation is the one in
// ``src/skills/builtin/spider.cpp`` — the same file the surface enumerator
// reads. Before the duplicate in ``skill_registry.cpp`` was deleted, a
// ``remove_xpaths`` fix could land in builtin/ and have no effect at runtime,
// because the registry's ``SpiderSkillR`` was the class actually registered.
// Pin the identity here so a re-introduced duplicate is caught, and re-prove
// the strip behaviour end to end against the live registration.
TEST(skill_spider_live_impl_is_the_builtin_and_strips_script_and_nav) {
  auto skill = sw_skills::SkillRegistry::instance().create("spider");
  ASSERT_TRUE(skill != nullptr);
  // The deleted duplicate said "Web scraping"; the builtin (and the Python
  // reference's SKILL_DESCRIPTION) says this.
  ASSERT_EQ(skill->skill_description(), "Fast web scraping and crawling capabilities");

  httplib::Server srv;
  srv.Get("/page", [&](const httplib::Request&, httplib::Response& res) {
    res.set_content(
        "<html><body>"
        "<script>alert(1)</script>"
        "<nav>NAVTEXT</nav>"
        "<p>VISIBLE BODY</p>"
        "</body></html>",
        "text/html");
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

  ::setenv("SPIDER_BASE_URL", ("http://127.0.0.1:" + std::to_string(port)).c_str(), 1);
  skill->setup(json::object());
  auto tools = skill->register_tools();
  ASSERT_TRUE(tools.size() >= 1u);
  auto result =
      tools[0].handler(json::object({{"url", "https://example.com/page"}}), json::object());
  auto resp = result.to_json()["response"].get<std::string>();

  srv.stop();
  th.join();
  ::unsetenv("SPIDER_BASE_URL");

  ASSERT_TRUE(resp.find("alert(1)") == std::string::npos);  // //script dropped
  ASSERT_TRUE(resp.find("NAVTEXT") == std::string::npos);   // //nav dropped
  ASSERT_TRUE(resp.find("VISIBLE BODY") != std::string::npos);
  return true;
}
