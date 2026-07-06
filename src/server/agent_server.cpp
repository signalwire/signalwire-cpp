// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#include "signalwire/server/agent_server.hpp"

#include <algorithm>

#include "httplib.h"
#include "server/tls_server.hpp"
#include "signalwire/agent/agent_base.hpp"
#include "signalwire/common.hpp"
#include "signalwire/logging.hpp"

namespace signalwire {
namespace server {

namespace {
// Forward-declared so the SIP-mapping members defined above the anonymous-
// namespace definition can normalize routes to a leading "/".
std::string normalize_route(const std::string& route);
}  // namespace

AgentServer::AgentServer(const std::string& host, int port) : host_(host), port_(port) {
  std::string env_port = get_env("PORT", "");
  if (!env_port.empty()) {
    try {
      port_ = std::stoi(env_port);
    } catch (const std::exception& e) {
      // Ignore invalid PORT values and keep the constructor-provided port.
      static_cast<void>(e);
    }
  }
}

AgentServer::~AgentServer() { stop(); }

AgentServer& AgentServer::register_agent(std::shared_ptr<agent::AgentBase> agent,
                                         const std::string& route) {
  std::lock_guard<std::mutex> lock(mutex_);
  std::string normalized = route;
  if (!normalized.empty() && normalized.front() != '/') {
    normalized = "/" + normalized;
  }
  agents_[normalized] = std::move(agent);
  get_logger().info("Registered agent at route: " + normalized);
  return *this;
}

AgentServer& AgentServer::unregister_agent(const std::string& route) {
  std::lock_guard<std::mutex> lock(mutex_);
  agents_.erase(route);
  return *this;
}

std::vector<std::string> AgentServer::list_routes() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<std::string> routes;
  routes.reserve(agents_.size());
  for (const auto& [r, _] : agents_) {
    routes.push_back(r);
  }
  return routes;
}

AgentServer& AgentServer::enable_sip_routing(bool enable) {
  sip_routing_ = enable;
  return *this;
}

AgentServer& AgentServer::map_sip_username(const std::string& username, const std::string& route) {
  // Validate SIP username
  static const std::regex valid_sip_re("^[a-zA-Z0-9._-]{1,64}$");
  if (!std::regex_match(username, valid_sip_re)) {
    get_logger().warn("Invalid SIP username: " + username);
    return *this;
  }
  // Store the mapping under the LOWERCASED username, with the route normalized
  // to a leading "/" — Python parity: AgentServer.register_sip_username stores
  // ``self._sip_username_mapping[username.lower()] = route`` and lookups
  // case-fold. Without lowercasing, a "Bob"/"BOB"/"bob" lookup misses.
  std::string key = username;
  std::transform(key.begin(), key.end(), key.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  sip_routes_[key] = normalize_route(route);
  return *this;
}

std::string AgentServer::lookup_sip_route(const std::string& username) const {
  // Case-fold on lookup (Python parity: AgentServer._lookup_sip_route uses
  // ``username.lower()``). Returns "" when no mapping exists.
  std::string key = username;
  std::transform(key.begin(), key.end(), key.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = sip_routes_.find(key);
  return it != sip_routes_.end() ? it->second : std::string{};
}

std::map<std::string, std::string> AgentServer::get_sip_username_mapping() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return sip_routes_;
}

AgentServer& AgentServer::set_static_dir(const std::string& dir) {
  static_dir_ = dir;
  return *this;
}

// ---- Python-parity surface -------------------------------------------------

namespace {
std::string normalize_route(const std::string& route) {
  std::string r = route;
  if (r.empty() || r.front() != '/') {
    r = "/" + r;
  }
  return r;
}
}  // namespace

AgentServer& AgentServer::register_(std::shared_ptr<agent::AgentBase> agent,
                                    const std::string& route) {
  // Python: route=None -> use the agent's own route.
  std::string effective = route.empty() ? agent->route() : route;
  return register_agent(std::move(agent), effective);
}

bool AgentServer::unregister(const std::string& route) {
  std::lock_guard<std::mutex> lock(mutex_);
  return agents_.erase(normalize_route(route)) > 0;
}

std::vector<std::pair<std::string, std::shared_ptr<agent::AgentBase>>> AgentServer::get_agents()
    const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<std::pair<std::string, std::shared_ptr<agent::AgentBase>>> out;
  out.reserve(agents_.size());
  for (const auto& [route, agent] : agents_) {
    out.emplace_back(route, agent);
  }
  return out;
}

std::shared_ptr<agent::AgentBase> AgentServer::get_agent(const std::string& route) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = agents_.find(normalize_route(route));
  return it == agents_.end() ? nullptr : it->second;
}

AgentServer& AgentServer::setup_sip_routing(const std::string& route, bool auto_map) {
  sip_routing_ = true;
  sip_route_ = normalize_route(route);
  sip_auto_map_ = auto_map;
  return *this;
}

AgentServer& AgentServer::register_sip_username(const std::string& username,
                                                const std::string& route) {
  return map_sip_username(username, route);
}

AgentServer& AgentServer::serve_static_files(const std::string& directory,
                                             const std::string& route) {
  static_dir_ = directory;
  static_route_ = normalize_route(route);
  return *this;
}

AgentServer& AgentServer::register_global_routing_callback(GlobalRoutingCallback callback_fn,
                                                           const std::string& path) {
  std::lock_guard<std::mutex> lock(mutex_);
  std::string normalized = normalize_route(path);
  // Strip a trailing slash (except the bare root), mirroring Python's rstrip.
  if (normalized.size() > 1 && normalized.back() == '/') {
    normalized.pop_back();
  }
  routing_callbacks_.emplace_back(normalized, std::move(callback_fn));
  get_logger().info("Registered global routing callback at " + normalized);
  return *this;
}

void AgentServer::setup_routes(httplib::Server& server) {
  // Health (no auth)
  server.Get("/health", [](const httplib::Request&, httplib::Response& res) {
    res.set_content("{\"status\":\"healthy\"}", "application/json");
  });

  // Ready (no auth)
  server.Get("/ready", [](const httplib::Request&, httplib::Response& res) {
    res.set_content("{\"status\":\"ready\"}", "application/json");
  });

  // Agent listing
  server.Get("/agents", [this](const httplib::Request&, httplib::Response& res) {
    json agents_json = json::array();
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& [route, agent] : agents_) {
      agents_json.push_back(json::object({{"route", route}, {"name", agent->name()}}));
    }
    res.set_content(agents_json.dump(), "application/json");
  });

  // For each registered agent, set up their routes
  for (auto& [route, agent] : agents_) {
    agent->init_auth();

    std::string base = route;
    if (base.empty()) {
      base = "/";
    }

    // SWML endpoint
    auto swml_handler = [&ag = *agent](const httplib::Request& req, httplib::Response& res) {
      ag.handle_swml_request(req, res);
    };
    server.Get(base, swml_handler);
    server.Post(base, swml_handler);

    // SWAIG endpoint
    std::string swaig_path = base + (base.back() == '/' ? "" : "/") + "swaig";
    server.Post(swaig_path, [&ag = *agent](const httplib::Request& req, httplib::Response& res) {
      ag.handle_swaig_request(req, res);
    });

    // Post-prompt endpoint
    std::string pp_path = base + (base.back() == '/' ? "" : "/") + "post_prompt";
    server.Post(pp_path, [&ag = *agent](const httplib::Request& req, httplib::Response& res) {
      ag.handle_post_prompt_request(req, res);
    });
  }

  // Static file serving
  if (!static_dir_.empty()) {
    server.set_mount_point("/static", static_dir_);
  }

  // SIP routing endpoint
  if (sip_routing_) {
    server.Post("/sip", [this](const httplib::Request& req, httplib::Response& res) {
      json body;
      try {
        body = json::parse(req.body);
      } catch (...) {
        res.status = 400;
        res.set_content("{\"error\":\"invalid JSON\"}", "application/json");
        return;
      }

      std::string username = body.value("username", "");
      if (username.empty()) {
        res.status = 400;
        res.set_content("{\"error\":\"missing username\"}", "application/json");
        return;
      }

      std::string sip_key = username;
      std::transform(sip_key.begin(), sip_key.end(), sip_key.begin(),
                     [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
      auto it = sip_routes_.find(sip_key);
      if (it != sip_routes_.end()) {
        res.set_content(json::object({{"route", it->second}}).dump(), "application/json");
      } else {
        res.status = 404;
        res.set_content("{\"error\":\"no route for SIP username\"}", "application/json");
      }
    });
  }
}

void AgentServer::run() {
  // TLS termination in-process when SWML_SSL_ENABLED + cert/key paths are
  // set (mirrors Python's SecurityConfig). make_http_server returns an
  // httplib::SSLServer upcast to Server* in that case; otherwise plain HTTP.
  auto tls = server::resolve_tls_config_from_env();
  server_ = server::make_http_server(tls);
  if (tls.usable() && !server_->is_valid()) {
    get_logger().error("SSL enabled but cert/key failed to load (cert=" + tls.cert_path +
                       " key=" + tls.key_path + ")");
    return;
  }
  server_->set_payload_max_length(static_cast<size_t>(1024) * 1024);  // 1MB limit

  setup_routes(*server_);

  get_logger().info("Starting AgentServer on " +
                    std::string(tls.usable() ? "https://" : "http://") + host_ + ":" +
                    std::to_string(port_));
  get_logger().info("Registered " + std::to_string(agents_.size()) + " agent(s)");

  if (!server_->listen(host_, port_)) {
    get_logger().error("Failed to start server on " + host_ + ":" + std::to_string(port_) +
                       " -- is the port already in use?");
  }
}

void AgentServer::stop() {
  if (server_) {
    server_->stop();
    server_.reset();
  }
}

}  // namespace server
}  // namespace signalwire
