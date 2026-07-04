// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT

#include "signalwire/core/swaig_function.hpp"

#include <exception>

#include "signalwire/logging.hpp"
#include "signalwire/swaig/function_result.hpp"

namespace signalwire {
namespace core {

const char* SWAIGFunction::kExecuteErrorResponse =
    "Sorry, I couldn't complete that action. Please try again or contact support if the issue "
    "persists.";

SWAIGFunction::SWAIGFunction(std::string name, SwaigFunctionHandler handler,
                             std::string description, json parameters, bool secure,
                             std::optional<json> fillers, std::optional<std::string> wait_file,
                             std::optional<int> wait_file_loops,
                             std::optional<std::string> webhook_url,
                             std::vector<std::string> required, bool is_typed_handler,
                             json extra_swaig_fields)
    : name_(std::move(name)),
      handler_(std::move(handler)),
      description_(std::move(description)),
      parameters_(parameters.is_null() ? json::object() : std::move(parameters)),
      secure_(secure),
      fillers_(std::move(fillers)),
      wait_file_(std::move(wait_file)),
      wait_file_loops_(wait_file_loops),
      webhook_url_(std::move(webhook_url)),
      required_(std::move(required)),
      is_typed_handler_(is_typed_handler),
      extra_swaig_fields_(extra_swaig_fields.is_null() ? json::object()
                                                       : std::move(extra_swaig_fields)),
      // Mark as external if webhook_url is provided (Python parity).
      is_external_(webhook_url_.has_value()) {}

json SWAIGFunction::call(const json& args, const json& raw_data) const {
  return handler_ ? handler_(args, raw_data) : json(nullptr);
}

json SWAIGFunction::operator()(const json& args, const json& raw_data) const {
  return call(args, raw_data);
}

json SWAIGFunction::execute(const json& args, const std::optional<json>& raw_data) const {
  try {
    // Raw data is mandatory but tolerate null for robustness (Python parity).
    const json raw = raw_data.has_value() && !raw_data->is_null() ? *raw_data : json::object();

    json result = handler_ ? handler_(args, raw) : json(nullptr);

    // Everything must end up as a FunctionResult dict.
    if (result.is_object() && result.contains("response")) {
      // Already in the correct format — use as is.
      return result;
    }
    if (result.is_object()) {
      // Object without "response" — create a generic success FunctionResult.
      return swaig::FunctionResult("Function completed successfully").to_json();
    }
    if (result.is_string()) {
      return swaig::FunctionResult(result.get<std::string>()).to_json();
    }
    if (result.is_null()) {
      return swaig::FunctionResult("null").to_json();
    }
    // Other scalar type — stringify.
    return swaig::FunctionResult(result.dump()).to_json();
  } catch (const std::exception& e) {
    // Log for debugging but don't expose details to the AI.
    get_logger().error("Error executing SWAIG function " + name_ + ": " + e.what());
    return swaig::FunctionResult(kExecuteErrorResponse).to_json();
  }
}

json SWAIGFunction::ensure_parameter_structure() const {
  if (parameters_.is_null() || parameters_.empty()) {
    return json::object({{"type", "object"}, {"properties", json::object()}});
  }
  // Already the correct structure?
  if (parameters_.contains("type") && parameters_.contains("properties")) {
    return parameters_;
  }
  // Wrap loose properties in the expected envelope.
  json result = json::object({{"type", "object"}, {"properties", parameters_}});
  if (!required_.empty()) {
    result["required"] = required_;
  }
  return result;
}

ArgsValidationResult SWAIGFunction::validate_args(const json& args) const {
  ArgsValidationResult out;
  json schema = ensure_parameter_structure();

  auto props_it = schema.find("properties");
  if (props_it == schema.end() || !props_it->is_object() || props_it->empty()) {
    out.valid = true;
    return out;
  }

  const json a = args.is_object() ? args : json::object();

  // Enforce the schema's `required` list.
  if (schema.contains("required") && schema.at("required").is_array()) {
    for (const auto& req : schema.at("required")) {
      if (req.is_string()) {
        const std::string prop = req.get<std::string>();
        if (!a.contains(prop)) {
          out.errors.push_back("missing required property '" + prop + "'");
        }
      }
    }
  }

  // Enforce each declared property's JSON `type`.
  auto type_matches = [](const std::string& type, const json& value) -> bool {
    if (type == "string") {
      return value.is_string();
    }
    if (type == "integer") {
      return value.is_number_integer();
    }
    if (type == "number") {
      return value.is_number();
    }
    if (type == "boolean") {
      return value.is_boolean();
    }
    if (type == "array") {
      return value.is_array();
    }
    if (type == "object") {
      return value.is_object();
    }
    return true;  // unknown type: nothing to enforce
  };

  for (auto it = props_it->begin(); it != props_it->end(); ++it) {
    const std::string& prop = it.key();
    if (!it.value().is_object() || !a.contains(prop)) {
      continue;
    }
    auto type_it = it.value().find("type");
    if (type_it != it.value().end() && type_it->is_string()) {
      const std::string type = type_it->get<std::string>();
      if (!type_matches(type, a.at(prop))) {
        std::string msg = "property '";
        msg += prop;
        msg += "' must be of type ";
        msg += type;
        out.errors.push_back(std::move(msg));
      }
    }
  }

  out.valid = out.errors.empty();
  return out;
}

json SWAIGFunction::to_swaig(const std::string& base_url, const std::optional<std::string>& token,
                             const std::optional<std::string>& call_id, bool include_auth) const {
  static_cast<void>(include_auth);  // parity placeholder (see reference include_auth)

  // All functions use a single /swaig endpoint.
  std::string url = base_url + "/swaig";
  if (token.has_value() && call_id.has_value()) {
    url = url + "?token=" + *token + "&call_id=" + *call_id;
  }

  json function_def = json::object();
  function_def["function"] = name_;
  function_def["description"] = description_;
  function_def["parameters"] = ensure_parameter_structure();
  if (!url.empty()) {
    function_def["web_hook_url"] = url;
  }
  if (fillers_.has_value() && fillers_->is_object() && !fillers_->empty()) {
    function_def["fillers"] = *fillers_;
  }
  // Merge any extra SWAIG fields.
  if (extra_swaig_fields_.is_object()) {
    for (auto it = extra_swaig_fields_.begin(); it != extra_swaig_fields_.end(); ++it) {
      function_def[it.key()] = it.value();
    }
  }
  return function_def;
}

}  // namespace core
}  // namespace signalwire
