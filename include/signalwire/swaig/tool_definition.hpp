#pragma once

#include <string>
#include <functional>
#include <nlohmann/json.hpp>
#include "signalwire/swaig/function_result.hpp"

namespace signalwire {
namespace swaig {

using json = nlohmann::json;
using ToolHandler = std::function<FunctionResult(const json& args, const json& raw_data)>;

/// Definition of a SWAIG tool (function)
struct ToolDefinition {
    std::string name;
    std::string description;
    json parameters;  // JSON schema for parameters
    ToolHandler handler;
    bool secure = false;

    /// Render to the SWAIG function JSON format (for inclusion in SWML)
    json to_swaig_json(const std::string& web_hook_url = "") const {
        json func;
        func["function"] = name;
        func["description"] = description;

        if (!parameters.is_null() && !parameters.empty()) {
            func["parameters"] = parameters;
        } else {
            func["parameters"] = json::object({{"type", "object"}, {"properties", json::object()}});
        }

        if (!web_hook_url.empty()) {
            func["web_hook_url"] = web_hook_url;
        }

        if (secure) {
            func["secure"] = true;
        }

        return func;
    }
};

} // namespace swaig
} // namespace signalwire
