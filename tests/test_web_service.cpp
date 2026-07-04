// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
//
// Behavioral tests for web::WebService (static-file serving).
// Python parity: signalwire.web.web_service.WebService.

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "httplib.h"
#include "signalwire/common.hpp"
#include "signalwire/web/web_service.hpp"

using namespace signalwire::web;
namespace fs = std::filesystem;

namespace {

// Create a unique temp directory with a couple of files, returns its path.
std::string make_temp_docroot() {
  fs::path base =
      fs::temp_directory_path() / ("sw_webservice_test_" + std::to_string(::getpid()) + "_" +
                                   std::to_string(reinterpret_cast<uintptr_t>(&base)));
  fs::create_directories(base);
  { std::ofstream(base / "hello.txt") << "hello world"; }
  { std::ofstream(base / "index.html") << "<h1>index</h1>"; }
  { std::ofstream(base / ".env") << "SECRET=1"; }
  return base.string();
}

}  // namespace

// ---- file_allowed (no server needed) ----

TEST(web_service_file_allowed_blocks_dotenv) {
  std::string root = make_temp_docroot();
  WebService ws;
  ASSERT_TRUE(ws.file_allowed(root + "/hello.txt"));
  ASSERT_FALSE(ws.file_allowed(root + "/.env"));         // blocked by default
  ASSERT_FALSE(ws.file_allowed(root + "/missing.txt"));  // does not exist
  fs::remove_all(root);
  return true;
}

TEST(web_service_file_allowed_respects_allowlist) {
  std::string root = make_temp_docroot();
  WebService ws(8002, std::nullopt, std::nullopt, std::nullopt, false,
                std::optional<std::vector<std::string>>(std::vector<std::string>{".html"}));
  ASSERT_TRUE(ws.file_allowed(root + "/index.html"));
  ASSERT_FALSE(ws.file_allowed(root + "/hello.txt"));  // not in allow-list
  fs::remove_all(root);
  return true;
}

TEST(web_service_file_allowed_max_size) {
  std::string root = make_temp_docroot();
  WebService ws(8002, std::nullopt, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt,
                /*max_file_size=*/3);
  // hello.txt is 11 bytes > 3
  ASSERT_FALSE(ws.file_allowed(root + "/hello.txt"));
  fs::remove_all(root);
  return true;
}

// ---- add/remove_directory ----

TEST(web_service_add_directory_validates) {
  WebService ws;
  ASSERT_THROWS(ws.add_directory("/x", "/no/such/dir/here"));
  return true;
}

TEST(web_service_add_and_remove_directory) {
  std::string root = make_temp_docroot();
  WebService ws;
  ws.add_directory("docs", root);  // route normalized to /docs
  ASSERT_TRUE(ws.directories().count("/docs") == 1);
  ws.remove_directory("/docs");
  ASSERT_TRUE(ws.directories().count("/docs") == 0);
  fs::remove_all(root);
  return true;
}

// ---- start / serve / stop over HTTP ----

TEST(web_service_serves_file) {
  std::string root = make_temp_docroot();
  WebService ws;
  ws.add_directory("/static", root);
  int port = ws.start("127.0.0.1", 0);
  ASSERT_TRUE(port > 0);

  httplib::Client cli("127.0.0.1", port);
  cli.set_connection_timeout(2, 0);
  auto res = cli.Get("/static/hello.txt");
  ws.stop();
  fs::remove_all(root);

  ASSERT_TRUE(static_cast<bool>(res));
  ASSERT_EQ(res->status, 200);
  ASSERT_EQ(res->body, std::string("hello world"));
  return true;
}

TEST(web_service_blocks_dotfile_over_http) {
  std::string root = make_temp_docroot();
  WebService ws;
  ws.add_directory("/static", root);
  int port = ws.start("127.0.0.1", 0);

  httplib::Client cli("127.0.0.1", port);
  cli.set_connection_timeout(2, 0);
  auto res = cli.Get("/static/.env");
  ws.stop();
  fs::remove_all(root);

  ASSERT_TRUE(static_cast<bool>(res));
  ASSERT_EQ(res->status, 403);  // File type not allowed
  return true;
}

TEST(web_service_path_traversal_denied) {
  std::string root = make_temp_docroot();
  WebService ws;
  ws.add_directory("/static", root);
  int port = ws.start("127.0.0.1", 0);

  httplib::Client cli("127.0.0.1", port);
  cli.set_connection_timeout(2, 0);
  // %2e%2e = ".." — attempt to escape the docroot.
  auto res = cli.Get("/static/../../etc/hosts");
  ws.stop();
  fs::remove_all(root);

  ASSERT_TRUE(static_cast<bool>(res));
  ASSERT_TRUE(res->status == 403 || res->status == 404);
  return true;
}

TEST(web_service_basic_auth_required) {
  std::string root = make_temp_docroot();
  WebService ws(8002, std::nullopt,
                std::optional<std::pair<std::string, std::string>>(
                    std::make_pair(std::string("admin"), std::string("secret"))));
  ws.add_directory("/static", root);
  int port = ws.start("127.0.0.1", 0);

  httplib::Client cli("127.0.0.1", port);
  cli.set_connection_timeout(2, 0);
  auto unauth = cli.Get("/static/hello.txt");

  httplib::Client cli2("127.0.0.1", port);
  cli2.set_connection_timeout(2, 0);
  cli2.set_basic_auth("admin", "secret");
  auto ok = cli2.Get("/static/hello.txt");

  ws.stop();
  fs::remove_all(root);

  ASSERT_TRUE(static_cast<bool>(unauth));
  ASSERT_EQ(unauth->status, 401);
  ASSERT_TRUE(static_cast<bool>(ok));
  ASSERT_EQ(ok->status, 200);
  return true;
}

TEST(web_service_directory_browsing_lists) {
  std::string root = make_temp_docroot();
  // Remove index.html so the listing path is exercised.
  fs::remove(fs::path(root) / "index.html");
  WebService ws(8002, std::nullopt, std::nullopt, std::nullopt,
                /*enable_directory_browsing=*/true);
  ws.add_directory("/static", root);
  int port = ws.start("127.0.0.1", 0);

  httplib::Client cli("127.0.0.1", port);
  cli.set_connection_timeout(2, 0);
  auto res = cli.Get("/static/");
  ws.stop();
  fs::remove_all(root);

  ASSERT_TRUE(static_cast<bool>(res));
  ASSERT_EQ(res->status, 200);
  ASSERT_TRUE(res->body.find("hello.txt") != std::string::npos);  // listed
  ASSERT_TRUE(res->body.find(".env") == std::string::npos);       // hidden skipped
  return true;
}

TEST(web_service_directory_index_html) {
  std::string root = make_temp_docroot();
  WebService ws;
  ws.add_directory("/static", root);
  int port = ws.start("127.0.0.1", 0);

  httplib::Client cli("127.0.0.1", port);
  cli.set_connection_timeout(2, 0);
  // Request the directory itself -> serves index.html (browsing disabled).
  auto res = cli.Get("/static/");
  ws.stop();
  fs::remove_all(root);

  ASSERT_TRUE(static_cast<bool>(res));
  ASSERT_EQ(res->status, 200);
  ASSERT_TRUE(res->body.find("index") != std::string::npos);
  return true;
}
