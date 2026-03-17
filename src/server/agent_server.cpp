// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#include "signalwire/server/agent_server.hpp"
#include "signalwire/agent/agent_base.hpp"
#include "signalwire/logging.hpp"
#include "signalwire/common.hpp"
#include "httplib.h"

namespace signalwire {
namespace server {

AgentServer::AgentServer(const std::string& host, int port)
    : host_(host), port_(port) {
    std::string env_port = get_env("PORT", "");
    if (!env_port.empty()) {
        try { port_ = std::stoi(env_port); } catch (...) {}
    }
}

AgentServer::~AgentServer() {
    stop();
}

AgentServer& AgentServer::register_agent(std::shared_ptr<agent::AgentBase> agent,
                                          const std::string& route) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string normalized = route;
    if (!normalized.empty() && normalized.front() != '/') normalized = "/" + normalized;
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
    sip_routes_[username] = route;
    return *this;
}

AgentServer& AgentServer::set_static_dir(const std::string& dir) {
    static_dir_ = dir;
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
            agents_json.push_back(json::object({
                {"route", route},
                {"name", agent->name()}
            }));
        }
        res.set_content(agents_json.dump(), "application/json");
    });

    // For each registered agent, set up their routes
    for (auto& [route, agent] : agents_) {
        agent->init_auth();

        std::string base = route;
        if (base.empty()) base = "/";

        // SWML endpoint
        auto swml_handler = [&ag = *agent](const httplib::Request& req, httplib::Response& res) {
            ag.handle_swml_request(req, res);
        };
        server.Get(base.c_str(), swml_handler);
        server.Post(base.c_str(), swml_handler);

        // SWAIG endpoint
        std::string swaig_path = base + (base.back() == '/' ? "" : "/") + "swaig";
        server.Post(swaig_path.c_str(), [&ag = *agent](const httplib::Request& req, httplib::Response& res) {
            ag.handle_swaig_request(req, res);
        });

        // Post-prompt endpoint
        std::string pp_path = base + (base.back() == '/' ? "" : "/") + "post_prompt";
        server.Post(pp_path.c_str(), [&ag = *agent](const httplib::Request& req, httplib::Response& res) {
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

            auto it = sip_routes_.find(username);
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
    server_ = std::make_unique<httplib::Server>();
    server_->set_payload_max_length(1024 * 1024); // 1MB limit

    setup_routes(*server_);

    get_logger().info("Starting AgentServer on " + host_ + ":" + std::to_string(port_));
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

} // namespace server
} // namespace signalwire
