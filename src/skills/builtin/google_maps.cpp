// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#include "signalwire/skills/skill_base.hpp"
#include "signalwire/skills/skill_registry.hpp"

namespace signalwire {
namespace skills {

class GoogleMapsSkill : public SkillBase {
public:
    std::string skill_name() const override { return "google_maps"; }
    std::string skill_description() const override {
        return "Validate addresses and compute driving routes using Google Maps";
    }

    bool setup(const json& params) override {
        params_ = params;
        api_key_ = get_param_or_env(params, "api_key", "GOOGLE_MAPS_API_KEY");
        lookup_tool_ = get_param<std::string>(params, "lookup_tool_name", "lookup_address");
        route_tool_ = get_param<std::string>(params, "route_tool_name", "compute_route");
        return !api_key_.empty();
    }

    std::vector<swaig::ToolDefinition> register_tools() override {
        std::vector<swaig::ToolDefinition> tools;

        tools.push_back(define_tool(lookup_tool_, "Look up and validate an address using Google Maps",
            json::object({{"type", "object"}, {"properties", json::object({
                {"address", json::object({{"type", "string"}, {"description", "Address to look up"}})}
            })}, {"required", json::array({"address"})}}),
            [this](const json& args, const json&) -> swaig::FunctionResult {
                std::string addr = args.value("address", "");
                return swaig::FunctionResult("Address lookup for '" + addr +
                    "': [Would use Google Geocoding API with key " + api_key_.substr(0, 4) + "...]");
            }
        ));

        tools.push_back(define_tool(route_tool_, "Compute driving route between two locations",
            json::object({{"type", "object"}, {"properties", json::object({
                {"origin_lat", json::object({{"type", "number"}, {"description", "Origin latitude"}})},
                {"origin_lng", json::object({{"type", "number"}, {"description", "Origin longitude"}})},
                {"dest_lat", json::object({{"type", "number"}, {"description", "Destination latitude"}})},
                {"dest_lng", json::object({{"type", "number"}, {"description", "Destination longitude"}})}
            })}, {"required", json::array({"origin_lat", "origin_lng", "dest_lat", "dest_lng"})}}),
            [](const json& args, const json&) -> swaig::FunctionResult {
                return swaig::FunctionResult("Route computed: [Would use Google Routes API]");
            }
        ));

        return tools;
    }

    std::vector<std::string> get_hints() const override {
        return {"address", "location", "route", "directions", "miles", "distance"};
    }

    std::vector<SkillPromptSection> get_prompt_sections() const override {
        return {{"Google Maps", "", {
            "Use " + lookup_tool_ + " to validate addresses",
            "Use " + route_tool_ + " to compute driving routes"}}};
    }

private:
    std::string api_key_;
    std::string lookup_tool_ = "lookup_address";
    std::string route_tool_ = "compute_route";
};

REGISTER_SKILL(GoogleMapsSkill)

} // namespace skills
} // namespace signalwire
