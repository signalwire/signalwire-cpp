// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#pragma once

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>
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

  // ---- Python-parity surface (signalwire.agent_server.AgentServer) --------
  // Python names for the operations above, so the cross-language surface lines
  // up. These are the canonical spellings; the *_agent / *_sip_username / etc.
  // forms are the C++-idiomatic aliases retained for existing callers.

  /// Register an agent at a route (Python: ``register``). When ``route`` is
  /// empty the agent's own route is used, matching Python's ``route=None``.
  AgentServer& register_(std::shared_ptr<agent::AgentBase> agent, const std::string& route = "");

  /// Unregister an agent by route; returns whether one was removed
  /// (Python: ``unregister`` -> bool).
  bool unregister(const std::string& route);

  /// All registered agents as (route, agent) pairs (Python: ``get_agents``).
  std::vector<std::pair<std::string, std::shared_ptr<agent::AgentBase>>> get_agents() const;

  /// Look up an agent by route; nullptr when absent (Python: ``get_agent``).
  /// The route is normalized (leading ``/`` added) before lookup.
  std::shared_ptr<agent::AgentBase> get_agent(const std::string& route) const;

  /// Enable SIP routing (Python: ``setup_sip_routing``). ``auto_map`` mirrors
  /// Python's auto-mapping of each agent's SIP usernames.
  AgentServer& setup_sip_routing(const std::string& route = "/sip", bool auto_map = true);

  /// Map a SIP username to a route (Python: ``register_sip_username``).
  AgentServer& register_sip_username(const std::string& username, const std::string& route);

  /// Serve static files from a directory at ``route`` (Python:
  /// ``serve_static_files``).
  AgentServer& serve_static_files(const std::string& directory, const std::string& route = "/");

  /// A routing callback: given a request path + query params, return the route
  /// to dispatch to (empty = no override). Mirrors Python's
  /// ``Callable[[Request, dict], str | None]`` contract.
  using GlobalRoutingCallback =
      std::function<std::string(const std::string& path, const json& params)>;

  /// Register a routing callback across all agents at ``path`` (Python:
  /// ``register_global_routing_callback``).
  AgentServer& register_global_routing_callback(GlobalRoutingCallback callback_fn,
                                                const std::string& path);

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
  std::string static_route_ = "/";
  std::string sip_route_ = "/sip";
  bool sip_auto_map_ = true;
  std::vector<std::pair<std::string, GlobalRoutingCallback>> routing_callbacks_;  // (path, cb)
  bool sip_routing_ = false;
  std::unique_ptr<httplib::Server> server_;
  mutable std::mutex mutex_;
};

}  // namespace server
}  // namespace signalwire
