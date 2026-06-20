// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace httplib {
class Server;
class Request;
class Response;
}  // namespace httplib

namespace signalwire {
namespace agent {
class AgentBase;
}

namespace server {

using json = nlohmann::json;

/// Multi-agent hosting server
class AgentServer {
 public:
  explicit AgentServer(const std::string& host = "0.0.0.0", int port = 3000);
  ~AgentServer();

  /// Register an agent at a specific route
  AgentServer& register_agent(std::shared_ptr<agent::AgentBase> agent, const std::string& route);

  /// Unregister an agent by route
  AgentServer& unregister_agent(const std::string& route);

  /// List registered routes
  std::vector<std::string> list_routes() const;

  /// Enable SIP routing
  AgentServer& enable_sip_routing(bool enable = true);

  /// Map a SIP username to an agent route
  AgentServer& map_sip_username(const std::string& username, const std::string& route);

  /// Enable static file serving from a directory
  AgentServer& set_static_dir(const std::string& dir);

  /// Start the server (blocking)
  void run();

  /// Stop the server
  void stop();

 private:
  void setup_routes(httplib::Server& server);

  std::string host_;
  int port_;
  std::map<std::string, std::shared_ptr<agent::AgentBase>> agents_;
  std::map<std::string, std::string> sip_routes_;  // username -> route
  std::string static_dir_;
  bool sip_routing_ = false;
  std::unique_ptr<httplib::Server> server_;
  mutable std::mutex mutex_;
};

}  // namespace server
}  // namespace signalwire
