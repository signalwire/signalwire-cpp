// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
//
// SERVED-PATH routing test (#61). Proves that a request hitting the ACTUAL
// endpoint registered by serve()/setup_routes() flows through the decomposed
// handle_request() core: a routing callback that returns a redirect yields a
// real 307 with a Location header over the wire (NOT a 200), bad auth yields a
// 401 with WWW-Authenticate, and the happy path yields 200 SWML.
//
// Before the #61 fix the served SWML handler re-implemented auth+render and
// SKIPPED the routing-callback branch, so this 307 assertion returned 200 and
// the test failed. After the fix the served path delegates to handle_request().

#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

#include "httplib.h"
#include "signalwire/agent/agent_base.hpp"
#include "signalwire/common.hpp"

using json = nlohmann::json;

namespace {

// Bind an ephemeral TCP port, then release it so serve() can claim it.
int served_pick_free_port() {
  int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    std::cerr << "pick_free_port: socket() failed: " << std::strerror(errno) << "\n";
    return -1;
  }
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = 0;
  if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    std::cerr << "pick_free_port: bind() failed: " << std::strerror(errno) << "\n";
    ::close(fd);
    return -1;
  }
  socklen_t len = sizeof(addr);
  if (::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len) != 0) {
    std::cerr << "pick_free_port: getsockname() failed: " << std::strerror(errno) << "\n";
    ::close(fd);
    return -1;
  }
  int port = ntohs(addr.sin_port);
  ::close(fd);
  if (port <= 0) {
    std::cerr << "pick_free_port: kernel assigned no port\n";
    return -1;
  }
  return port;
}

std::string served_basic_auth(const std::string& user, const std::string& pass) {
  return "Basic " + signalwire::base64_encode(user + ":" + pass);
}

}  // namespace

// Regression guard for the ephemeral-port picker. Before this was fixed the
// picker ignored the return values of socket()/bind()/getsockname(), so any
// failure fell through to `ntohs(addr.sin_port)` on an unmodified sockaddr and
// returned 0 -- and the caller then bound a test server to port 0 or dialled
// nothing, failing later and somewhere else with an unrelated-looking error.
// The contract now is: a usable port is strictly positive, and any syscall
// failure returns -1 after reporting errno, so callers can assert on it.
TEST(pick_free_port_returns_a_usable_port_never_the_silent_zero) {
  int port = served_pick_free_port();
  // 0 is exactly what the unchecked version returned on failure.
  ASSERT_NE(port, 0);
  ASSERT_TRUE(port > 0);
  // A kernel-assigned ephemeral port is above the well-known range, in 16 bits.
  ASSERT_TRUE(port > 1024 && port <= 65535);

  // Successive picks must each really consult the kernel rather than replay a
  // stale sockaddr -- the failure shape the unchecked version could produce.
  int second = served_pick_free_port();
  ASSERT_TRUE(second > 0);
  ASSERT_TRUE(second > 1024 && second <= 65535);
  return true;
}

TEST(served_path_routing_307_401_200_through_handle_request) {
  ::setenv("SWML_BASIC_AUTH_USER", "su", 1);
  ::setenv("SWML_BASIC_AUTH_PASSWORD", "sp", 1);
  ::unsetenv("PORT");  // don't let an ambient PORT override our chosen port

  int port = served_pick_free_port();
  ASSERT_TRUE(port > 0);

  signalwire::agent::AgentBase agent("served-agent", "/");
  agent.set_host("127.0.0.1").set_port(port);

  // Routing callback on the served route: redirect when the body asks for it.
  // Registered at "/" because the agent's endpoint (and thus the callback path
  // derived from the request URL) is "/".
  agent.register_routing_callback(
      [](const json& body, const std::map<std::string, std::string>&) -> std::string {
        return body.value("dest", std::string()) == "sales" ? "/sales" : "";
      },
      "/");

  // serve() blocks on listen(); run it in a thread and stop it on the way out.
  std::thread server_thread([&agent]() { agent.serve(); });
  struct Guard {
    signalwire::agent::AgentBase& a;
    std::thread& t;
    ~Guard() {
      a.stop();
      if (t.joinable()) t.join();
      ::unsetenv("SWML_BASIC_AUTH_USER");
      ::unsetenv("SWML_BASIC_AUTH_PASSWORD");
    }
  } guard{agent, server_thread};

  const std::string base = "http://127.0.0.1:" + std::to_string(port);

  // Poll /health (no auth) until the listener is up.
  bool up = false;
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (std::chrono::steady_clock::now() < deadline) {
    httplib::Client cli(base);
    cli.set_connection_timeout(1, 0);
    auto res = cli.Get("/health");
    if (res && res->status == 200) {
      up = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  ASSERT_TRUE(up);

  httplib::Client cli(base);
  cli.set_connection_timeout(2, 0);
  cli.set_read_timeout(2, 0);
  // Do NOT auto-follow the redirect: we want to observe the raw 307.
  cli.set_follow_location(false);

  // (1) Routing callback returns a route -> the SERVED path must produce a real
  // 307 with the Location header (this is the assertion that failed pre-fix).
  {
    httplib::Headers h = {{"Authorization", served_basic_auth("su", "sp")},
                          {"Content-Type", "application/json"}};
    auto res = cli.Post("/", h, json({{"dest", "sales"}}).dump(), "application/json");
    ASSERT_TRUE(static_cast<bool>(res));
    ASSERT_EQ(res->status, 307);
    ASSERT_TRUE(res->has_header("Location"));
    ASSERT_EQ(res->get_header_value("Location"), std::string("/sales"));
  }

  // (2) Bad auth through the served path -> 401 + WWW-Authenticate.
  {
    httplib::Headers h = {{"Authorization", served_basic_auth("su", "wrong")},
                          {"Content-Type", "application/json"}};
    auto res = cli.Post("/", h, json({{"dest", "sales"}}).dump(), "application/json");
    ASSERT_TRUE(static_cast<bool>(res));
    ASSERT_EQ(res->status, 401);
    ASSERT_TRUE(res->has_header("WWW-Authenticate"));
  }

  // (3) Happy path (callback returns empty route) -> 200 rendered SWML.
  {
    httplib::Headers h = {{"Authorization", served_basic_auth("su", "sp")},
                          {"Content-Type", "application/json"}};
    auto res = cli.Post("/", h, json({{"dest", "other"}}).dump(), "application/json");
    ASSERT_TRUE(static_cast<bool>(res));
    ASSERT_EQ(res->status, 200);
    json parsed = json::parse(res->body);
    ASSERT_TRUE(parsed.is_object());
  }

  return true;
}
