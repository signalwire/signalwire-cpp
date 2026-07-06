// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT

#include "signalwire/swaig/type_inference.hpp"

namespace signalwire {
namespace swaig {
namespace type_inference {

InferredSchema infer_schema(const ParameterSchema& params,
                            const std::optional<std::string>& description) {
  // The built schema is the single source of truth: to_json() yields
  // {"type":"object","properties":{...},["required":[...]]}.
  const json schema = params.to_json();

  json parameters = json::object();
  if (schema.contains("properties") && schema["properties"].is_object()) {
    parameters = schema["properties"];
  }

  // has_raw_data: whether the builder declared a raw_data property. It is the
  // SWAIG raw-payload channel, not a real tool argument, so it is filtered out
  // of the emitted parameters/required (mirroring the Python reference).
  const bool has_raw_data = parameters.contains("raw_data");
  if (has_raw_data) {
    parameters.erase("raw_data");
  }

  std::vector<std::string> required;
  if (schema.contains("required") && schema["required"].is_array()) {
    for (const auto& r : schema["required"]) {
      if (r.is_string()) {
        std::string name = r.get<std::string>();
        if (name != "raw_data") {
          required.push_back(name);
        }
      }
    }
  }

  // is_typed: the typed builder declared properties (the C++ analog of "the
  // handler uses typed parameters"). An empty builder yields an untyped
  // (old-style) tool, exactly like the reference's empty-schema fall-through.
  const bool is_typed = !parameters.empty() || has_raw_data;

  return InferredSchema{std::move(parameters), std::move(required), description, is_typed,
                        has_raw_data};
}

ToolHandler create_typed_handler_wrapper(ToolHandler func, bool has_raw_data) {
  // Wrap the handler to the standard (args, raw_data) SWAIG calling convention.
  // The wrapper normalizes args to a JSON object and forwards the raw payload
  // only when the handler declared it — otherwise the handler sees an empty
  // object for the raw channel, matching the Python wrapper's contract.
  return
      [func = std::move(func), has_raw_data](const json& args, const json& raw) -> FunctionResult {
        const json normalized_args = args.is_object() ? args : json::object();
        const json forwarded_raw = has_raw_data ? raw : json::object();
        return func(normalized_args, forwarded_raw);
      };
}

}  // namespace type_inference
}  // namespace swaig
}  // namespace signalwire
