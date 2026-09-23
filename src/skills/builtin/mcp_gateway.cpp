// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#include "httplib.h"
#include "signalwire/skills/skill_base.hpp"
#include "signalwire/skills/skill_registry.hpp"

namespace signalwire {
namespace skills {

class McpGatewaySkill : public SkillBase {
 public:
  std::string skill_name() const override { return "mcp_gateway"; }
  std::string skill_description() const override {
    return "Bridge MCP servers with SWAIG functions";
  }

  // Advertise the configurable parameters (mirrors the Python skill's
  // MCPGatewaySkill.get_parameter_schema). ``verify_ssl`` defaults to TRUE so a
  // gateway call verifies the server certificate unless the operator explicitly
  // opts out.
  json get_parameter_schema() const override {
    return json::object(
        {{"gateway_url", json::object({{"type", "string"},
                                       {"description", "URL of the MCP Gateway service"},
                                       {"required", true}})},
         {"tool_prefix",
          json::object({{"type", "string"},
                        {"description", "Prefix for registered SWAIG function names"},
                        {"default", "mcp_"},
                        {"required", false}})},
         {"request_timeout", json::object({{"type", "integer"},
                                           {"description", "Request timeout in seconds"},
                                           {"default", 30},
                                           {"required", false}})},
         {"verify_ssl", json::object({{"type", "boolean"},
                                      {"description", "Verify SSL certificates"},
                                      {"default", true},
                                      {"required", false}})}});
  }

  bool setup(const json& params) override {
    params_ = params;
    gateway_url_ = get_param<std::string>(params, "gateway_url", "");
    tool_prefix_ = get_param<std::string>(params, "tool_prefix", "mcp_");
    request_timeout_ = get_param<int>(params, "request_timeout", 30);
    // Secure default: verify SSL certificates unless the operator opts out.
    verify_ssl_ = get_param<bool>(params, "verify_ssl", true);
    return !gateway_url_.empty();
  }

  std::vector<swaig::ToolDefinition> register_tools() override {
    std::vector<swaig::ToolDefinition> tools;

    if (params_.contains("services") && params_["services"].is_array()) {
      for (const auto& svc : params_["services"]) {
        std::string svc_name = svc.value("name", "service");
        // Capture the config the handler needs to reach the gateway, INCLUDING
        // verify_ssl, so each call verifies the cert per the skill's setting.
        std::string gateway_url = gateway_url_;
        bool verify_ssl = verify_ssl_;
        int timeout = request_timeout_;
        tools.push_back(define_tool(
            tool_prefix_ + svc_name + "_query", "[" + svc_name + "] Query the MCP service",
            json::object({{"type", "object"},
                          {"properties",
                           json::object({{"query", json::object({{"type", "string"},
                                                                 {"description", "Query"}})}})}}),
            [gateway_url, svc_name, verify_ssl, timeout](const json& args,
                                                         const json&) -> swaig::FunctionResult {
              json body = json::object({{"tool", svc_name}, {"arguments", args}});
              std::string result = call_gateway(gateway_url, "/services/" + svc_name + "/call",
                                                body, verify_ssl, timeout);
              return swaig::FunctionResult(result);
            }));
      }
    }

    return tools;
  }

  std::vector<std::string> get_hints() const override {
    std::vector<std::string> h = {"MCP", "gateway"};
    if (params_.contains("services") && params_["services"].is_array()) {
      for (const auto& svc : params_["services"]) {
        h.push_back(svc.value("name", ""));
      }
    }
    return h;
  }

  json get_global_data() const override {
    json services = json::array();
    if (params_.contains("services") && params_["services"].is_array()) {
      for (const auto& svc : params_["services"]) {
        services.push_back(svc.value("name", ""));
      }
    }
    return json::object({{"mcp_gateway_url", gateway_url_},
                         {"mcp_session_id", nullptr},
                         {"mcp_services", services}});
  }

  std::vector<SkillPromptSection> get_prompt_sections() const override {
    std::vector<std::string> bullets;
    if (params_.contains("services") && params_["services"].is_array()) {
      for (const auto& svc : params_["services"]) {
        bullets.push_back("Connected to: " + svc.value("name", ""));
      }
    }
    return {{"MCP Gateway Integration", "You have access to MCP (Model Context Protocol) services.",
             bullets}};
  }

  // Expose the cert-verification setting for tests / callers. This is the value
  // wired into ``enable_server_certificate_verification`` on every gateway call.
  bool verify_ssl() const { return verify_ssl_; }

 private:
  // POST a JSON body to the gateway. ``verify_ssl`` controls TLS server-cert
  // verification: when true (the secure default) the https client verifies the
  // server certificate; when the operator set verify_ssl=false it is turned off.
  // This is the real wiring — the flag drives httplib's certificate check, not a
  // stored no-op.
  static std::string call_gateway(const std::string& gateway_url, const std::string& path,
                                  const json& body, bool verify_ssl, int timeout_seconds) {
    // Split scheme+host from the base url; httplib::Client takes the origin and
    // the request path separately.
    std::string origin = gateway_url;
    // Strip any trailing slash so path concatenation is clean.
    while (!origin.empty() && origin.back() == '/') {
      origin.pop_back();
    }
    httplib::Client cli(origin);
    // WIRED: verify_ssl (default true) drives httplib server-cert verification.
    cli.enable_server_certificate_verification(verify_ssl);
    auto micros = std::chrono::microseconds(static_cast<long long>(timeout_seconds) * 1000000LL);
    cli.set_connection_timeout(micros);
    cli.set_read_timeout(micros);
    cli.set_write_timeout(micros);
    // Honor an explicit CA bundle if the fleet var is set (same trust bundle the
    // REST client reads), keeping verification enabled per verify_ssl above.
    if (const char* rest_ca = std::getenv("SIGNALWIRE_REST_CA_FILE")) {
      if (rest_ca && *rest_ca) {
        cli.set_ca_cert_path(rest_ca);
      }
    }
    auto res = cli.Post(path, body.dump(), "application/json");
    if (res && res->status == 200) {
      try {
        json parsed = json::parse(res->body);
        return parsed.value("result", std::string("No response"));
      } catch (const std::exception&) {
        return res->body;
      }
    }
    if (res) {
      return "MCP gateway error: HTTP " + std::to_string(res->status);
    }
    return "MCP gateway connection error";
  }

  std::string gateway_url_;
  std::string tool_prefix_ = "mcp_";
  int request_timeout_ = 30;
  bool verify_ssl_ = true;
};

REGISTER_SKILL(McpGatewaySkill)

}  // namespace skills
}  // namespace signalwire
