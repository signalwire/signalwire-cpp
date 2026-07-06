// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#pragma once

#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include "signalwire/swaig/parameter_schema.hpp"
#include "signalwire/swaig/tool_definition.hpp"  // ToolHandler

namespace signalwire {
namespace swaig {
namespace type_inference {

using json = nlohmann::json;

// ===========================================================================
// Schema inference for SWAIG tool functions.
//
// Mirrors the Python reference module-level functions
// ``signalwire.core.agent.tools.type_inference.infer_schema`` /
// ``create_typed_handler_wrapper``. Python reflects a callable's type hints
// (``inspect.signature`` + ``typing`` introspection) to derive a JSON-Schema
// for the tool's parameters. C++ has no runtime parameter-name/type-hint
// reflection over an arbitrary lambda, so — like the .NET / Ruby ports, which
// infer from a delegate's parameter list rather than Python-style hint
// reflection — the C++ port infers the schema from the port's TYPED
// params-builder (``ParameterSchema``), which is the direct analog of "a typed
// handler's parameter list". The output tuple is identical; only the input is
// idiomatic (a built ``ParameterSchema`` instead of a raw callable).
//
// The orchestrator projects these ``signalwire::swaig::type_inference`` free
// functions onto the Python module-level functions in the enumerators.
// ===========================================================================

/// The inferred-schema 5-tuple (Python's
/// ``(parameters, required, description, is_typed, has_raw_data)``):
///   - parameters:   the JSON-Schema ``properties`` object (name -> property).
///   - required:     the list of required parameter names.
///   - description:  the tool description (nullopt when none supplied).
///   - is_typed:     true when the typed builder declared any properties.
///   - has_raw_data: true when a ``raw_data`` property was declared.
using InferredSchema =
    std::tuple<json, std::vector<std::string>, std::optional<std::string>, bool, bool>;

/// Infer a JSON-Schema for a SWAIG tool's parameters from the port's typed
/// ``ParameterSchema`` params-builder (Python:
/// ``infer_schema(func) -> (parameters, required, description, is_typed,
/// has_raw_data)``). The ``raw_data`` property is the SWAIG raw-payload channel
/// and is excluded from the emitted ``parameters``/``required`` — its presence
/// is reported via ``has_raw_data`` instead, matching the reference.
///
/// @param params      the typed schema built via ``ParameterSchema``.
/// @param description  optional tool description (Python derives this from the
///                     handler's docstring; C++ lambdas carry none, so the
///                     caller supplies it — default nullopt).
[[nodiscard]] InferredSchema infer_schema(
    const ParameterSchema& params, const std::optional<std::string>& description = std::nullopt);

/// Wrap a typed handler so it can be invoked with the standard SWAIG calling
/// convention ``(args, raw_data)`` (Python:
/// ``create_typed_handler_wrapper(func, has_raw_data) -> wrapper``). The
/// wrapper normalizes the ``args`` object to a JSON object and forwards it;
/// when ``has_raw_data`` is set it also forwards the raw payload, otherwise it
/// passes an empty object so the wrapped handler never sees the raw channel it
/// did not ask for. Same output shape as the input handler (a ``ToolHandler``).
[[nodiscard]] ToolHandler create_typed_handler_wrapper(ToolHandler func, bool has_raw_data);

}  // namespace type_inference
}  // namespace swaig
}  // namespace signalwire
