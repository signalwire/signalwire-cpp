// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#include "signalwire/skills/skill_base.hpp"
#include "signalwire/skills/skill_registry.hpp"

namespace signalwire {
namespace skills {

class McpGatewaySkill : public SkillBase {
public:
    std::string skill_name() const override { return "mcp_gateway"; }
    std::string skill_description() const override { return "Bridge MCP servers with SWAIG functions"; }

    bool setup(const json& params) override {
        params_ = params;
        gateway_url_ = get_param<std::string>(params, "gateway_url", "");
        tool_prefix_ = get_param<std::string>(params, "tool_prefix", "mcp_");
        return !gateway_url_.empty();
    }

    std::vector<swaig::ToolDefinition> register_tools() override {
        // In full implementation, this would connect to the MCP gateway
        // and dynamically create tools based on discovered services
        std::vector<swaig::ToolDefinition> tools;

        if (params_.contains("services") && params_["services"].is_array()) {
            for (const auto& svc : params_["services"]) {
                std::string svc_name = svc.value("name", "service");
                tools.push_back(define_tool(
                    tool_prefix_ + svc_name + "_query",
                    "[" + svc_name + "] Query the MCP service",
                    json::object({{"type", "object"}, {"properties", json::object({
                        {"query", json::object({{"type", "string"}, {"description", "Query"}})}
                    })}}),
                    [this, svc_name](const json& args, const json&) -> swaig::FunctionResult {
                        return swaig::FunctionResult(
                            "MCP gateway query to " + svc_name + " via " + gateway_url_);
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
        return json::object({
            {"mcp_gateway_url", gateway_url_},
            {"mcp_session_id", nullptr},
            {"mcp_services", services}
        });
    }

    std::vector<SkillPromptSection> get_prompt_sections() const override {
        std::vector<std::string> bullets;
        if (params_.contains("services") && params_["services"].is_array()) {
            for (const auto& svc : params_["services"]) {
                bullets.push_back("Connected to: " + svc.value("name", ""));
            }
        }
        return {{"MCP Gateway Integration",
                 "You have access to MCP (Model Context Protocol) services.",
                 bullets}};
    }

private:
    std::string gateway_url_;
    std::string tool_prefix_ = "mcp_";
};

REGISTER_SKILL(McpGatewaySkill)

} // namespace skills
} // namespace signalwire
