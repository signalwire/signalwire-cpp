#pragma once

#include <functional>
#include <nlohmann/json.hpp>
#include <string>

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
  /// Whether this tool requires SWAIG token validation. Defaults to TRUE — a
  /// tool defined without an explicit ``secure`` is SECURE, so its rendered webhook
  /// carries the per-tool ``__token`` and its dispatch validates it. Defaulting
  /// this to false would silently ship every tool unauthenticated.
  bool secure = true;

  /// Render to the SWAIG function JSON format (for inclusion in SWML)
  /// [[nodiscard]]: the rendered JSON is the output; discarding it is a bug.
  [[nodiscard]] json to_swaig_json(const std::string& web_hook_url = "") const {
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

    // NOTE: ``secure`` is NOT emitted as a SWAIG function property. It is not a
    // property of the SWML ``UserSWAIGFunction`` schema and the reference never
    // renders it — the WIRE manifestation of ``secure`` is the per-tool
    // ``__token`` appended to ``web_hook_url`` when the document is rendered
    // with a call_id (see AgentBase::build_swaig_functions).
    return func;
  }
};

}  // namespace swaig
}  // namespace signalwire
