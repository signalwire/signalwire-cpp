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
  /// @param log_level Logging level (debug, info, warning, error, critical).
  ///   Stored lowercased and applied to the process logger.
  explicit AgentServer(const std::string& host = "0.0.0.0", int port = 3000,
                       const std::string& log_level = "info");
  ~AgentServer();

  // Construction parameters, readable back; `run()` reads these to bind.
  /// The bind host.
  [[nodiscard]] const std::string& host() const { return host_; }
  /// The bind port. A set ``PORT`` env var wins, so this reports the port that
  /// will actually be bound.
  [[nodiscard]] int port() const { return port_; }
  /// The lowercased level string.
  [[nodiscard]] const std::string& log_level() const { return log_level_; }

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

  // ---- Public surface --------
  // The canonical spellings for the operations above; the *_agent /
  // *_sip_username / etc. forms are aliases retained for existing callers.

  /// Register an agent at a route. When ``route`` is empty the agent's own
  /// route is used.
  AgentServer& register_(std::shared_ptr<agent::AgentBase> agent, const std::string& route = "");

  /// Unregister an agent by route; returns whether one was removed.
  bool unregister(const std::string& route);

  /// All registered agents as (route, agent) pairs.
  std::vector<std::pair<std::string, std::shared_ptr<agent::AgentBase>>> get_agents() const;

  /// Look up an agent by route; nullptr when absent. The route is normalized
  /// (leading ``/`` added) before lookup.
  std::shared_ptr<agent::AgentBase> get_agent(const std::string& route) const;

  /// Enable SIP routing at ``route``. ``auto_map`` requests auto-mapping of
  /// each agent's own SIP usernames.
  AgentServer& setup_sip_routing(const std::string& route = "/sip", bool auto_map = true);

  /// Map a SIP username to a route.
  AgentServer& register_sip_username(const std::string& username, const std::string& route);

  /// Look up the route registered for a SIP username, case-insensitively;
  /// returns an empty string when none is registered.
  [[nodiscard]] std::string lookup_sip_route(const std::string& username) const;

  /// The username -> route mapping (keys are lowercased).
  [[nodiscard]] std::map<std::string, std::string> get_sip_username_mapping() const;

  /// Serve static files from a directory at ``route``.
  AgentServer& serve_static_files(const std::string& directory, const std::string& route = "/");

  /// A routing callback: given a request path + query params, return the route
  /// to dispatch to (empty = no override).
  using GlobalRoutingCallback =
      std::function<std::string(const std::string& path, const json& params)>;

  /// Register a routing callback across all agents at ``path``.
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
  std::string log_level_;
  std::map<std::string, std::shared_ptr<agent::AgentBase>> agents_;
  std::map<std::string, std::string> sip_routes_;  // username -> route
  std::string static_dir_;
  std::string static_route_ = "/";
  std::string sip_route_ = "/sip";
  bool sip_auto_map_ = true;
  std::vector<std::pair<std::string, GlobalRoutingCallback>> routing_callbacks_;  // (path, cb)
  bool sip_routing_ = false;
  /// ``shared_ptr``, not ``unique_ptr``: ``serve()`` holds its own strong
  /// reference across the blocking ``listen()``, so a concurrent ``stop()``
  /// that drops the member cannot destroy the server while a thread is still
  /// executing inside it. Guarded by ``mutex_`` (write in ``serve()``, drop in
  /// ``stop()`` — two different threads).
  std::shared_ptr<httplib::Server> server_;
  mutable std::mutex mutex_;
};

}  // namespace server
}  // namespace signalwire
