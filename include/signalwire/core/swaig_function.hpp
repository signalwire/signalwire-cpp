// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
//
// SWAIGFunction — a registered SWAIG function (a tool the AI model can call).
//
// It holds a name/description/parameters/handler and renders into the SWAIG
// JSON descriptor sent to the model.
//
// DISTINCT from the lightweight swaig::ToolDefinition struct used by
// Service/AgentBase — that one is a plain wire-descriptor holder; SWAIGFunction
// adds execute(), validate_args(), and direct invocation via call()/operator().

#pragma once

#include <functional>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace signalwire {
namespace core {

using json = nlohmann::json;

/// Result of validate_args: (is_valid, errors).
struct ArgsValidationResult {
  bool valid = false;
  std::vector<std::string> errors;
};

/// Handler signature: (args, raw_data) -> result. The result JSON may already
/// be a FunctionResult dict (containing "response"), any other object, or a
/// scalar, which execute() coerces via to-string.
using SwaigFunctionHandler = std::function<json(const json& args, const json& raw_data)>;

/// Represents a SWAIG function — a tool the AI model can call.
class SWAIGFunction {
 public:
  /// Construct a SWAIG function.
  ///
  /// The required trio (name, handler, description) are leading params; every
  /// remaining param is defaulted. Arbitrary additional SWAIG descriptor fields
  /// go in the trailing `extra_swaig_fields` JSON object.
  SWAIGFunction(std::string name, SwaigFunctionHandler handler, std::string description,
                json parameters = json::object(), bool secure = false,
                std::optional<json> fillers = std::nullopt,
                std::optional<std::string> wait_file = std::nullopt,
                std::optional<int> wait_file_loops = std::nullopt,
                std::optional<std::string> webhook_url = std::nullopt,
                std::vector<std::string> required = {}, bool is_typed_handler = false,
                json extra_swaig_fields = json::object());

  // ---- Accessors ----
  [[nodiscard]] const std::string& name() const { return name_; }
  [[nodiscard]] const std::string& description() const { return description_; }
  [[nodiscard]] const json& parameters() const { return parameters_; }
  [[nodiscard]] bool secure() const { return secure_; }
  [[nodiscard]] const std::optional<json>& fillers() const { return fillers_; }
  [[nodiscard]] const std::optional<std::string>& wait_file() const { return wait_file_; }
  [[nodiscard]] const std::optional<int>& wait_file_loops() const { return wait_file_loops_; }
  [[nodiscard]] const std::optional<std::string>& webhook_url() const { return webhook_url_; }
  [[nodiscard]] const std::vector<std::string>& required() const { return required_; }
  [[nodiscard]] bool is_typed_handler() const { return is_typed_handler_; }
  [[nodiscard]] const json& extra_swaig_fields() const { return extra_swaig_fields_; }
  [[nodiscard]] bool is_external() const { return is_external_; }
  [[nodiscard]] const SwaigFunctionHandler& handler() const { return handler_; }

  /// Call the underlying handler. Returns the handler's raw (uncoerced)
  /// return value. Exposed both as a named `call` and as `operator()`.
  json call(const json& args, const json& raw_data = json::object()) const;
  json operator()(const json& args, const json& raw_data = json::object()) const;

  /// Execute the function: invoke the handler and coerce its return value into
  /// a FunctionResult dict. On any exception, logs and returns a generic
  /// non-leaking error message rather than propagating the throw.
  [[nodiscard]] json execute(const json& args,
                             const std::optional<json>& raw_data = std::nullopt) const;

  /// Validate the arguments against the parameter schema.
  ///
  /// There is no bundled JSON-Schema validator, so this performs a built-in
  /// check rather than full schema validation: the schema's `required` list
  /// plus each declared property's `type`. Passes when no properties are
  /// declared.
  [[nodiscard]] ArgsValidationResult validate_args(const json& args) const;

  /// Convert this function to a SWAIG-compatible JSON descriptor for SWML.
  [[nodiscard]] json to_swaig(const std::string& base_url,
                              const std::optional<std::string>& token = std::nullopt,
                              const std::optional<std::string>& call_id = std::nullopt,
                              bool include_auth = true) const;

  /// Generic non-leaking message returned when a handler throws.
  static const char* kExecuteErrorResponse;

 private:
  /// Wrap loose property maps in the {type, properties[, required]} envelope.
  [[nodiscard]] json ensure_parameter_structure() const;

  std::string name_;
  SwaigFunctionHandler handler_;
  std::string description_;
  json parameters_;
  bool secure_;
  std::optional<json> fillers_;
  std::optional<std::string> wait_file_;
  std::optional<int> wait_file_loops_;
  std::optional<std::string> webhook_url_;
  std::vector<std::string> required_;
  bool is_typed_handler_;
  json extra_swaig_fields_;
  bool is_external_;
};

}  // namespace core
}  // namespace signalwire
