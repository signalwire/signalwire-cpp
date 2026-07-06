// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "signalwire/swaig/function_result.hpp"  // RecordFormat/Codec/... enums + *_value()

namespace signalwire {
namespace swaig {

using json = nlohmann::json;

// ===========================================================================
// ParameterSchema — a typed, fluent builder for SWAIG tool parameters.
//
// `define_tool(name, desc, parameters, handler)` / `swaig::ToolDefinition`
// take `parameters` as a raw `json` blob: a JSON-Schema `object` with a
// `properties` map (one entry per argument) and an optional `required` list,
// hand-written as nested `json::object(...)`:
//
//     json::object({{"type", "object"}, {"properties", json::object({
//         {"query", json::object({{"type","string"},{"description","..."}})}
//     })}, {"required", json::array({"query"})}})
//
// That hand-written form is error-prone (typo'd keys, mismatched braces, a
// `"type"` value that isn't a real JSON-Schema type, an `enum` written as the
// wrong JSON kind). `ParameterSchema` is a TYPED CONVENIENCE OVER THE SAME
// WIRE OUTPUT — it builds the IDENTICAL `json` value, byte-for-byte, via a
// fluent API:
//
//     swaig::ParameterSchema{}
//         .string("service", "The service to book")
//         .string("date",    "YYYY-MM-DD")
//         .enum_of("fmt", swaig::record_format_values(), "Recording format")
//         .required({"service", "date"})
//         .to_json();
//
// `to_json()` returns exactly what the hand-written `json::object(...)` above
// would — same keys, same values, same `required` array. (nlohmann::json
// serialises object keys in sorted order, so the `.dump()` is canonical and
// independent of insertion order; see the byte-identical proof tests.)
//
// This is ADDITIVE: the untyped `json` path keeps working unchanged. The
// builder is a PORT_ADDITION (Python's `define_tool` takes a raw dict; there
// is no typed builder in the reference). Idiom: "let the language shine" —
// type the knowable JSON-Schema property shapes that Python leaves as a bare
// dict (porting-sdk/IDIOM_PASS_JOURNAL.md §4 "Tier 2 flagship").
//
// `[[nodiscard]]` policy (consistent with the earlier Tier-2 pass + DataMap):
// the value-returning terminals (`to_json()`, const accessors) are
// `[[nodiscard]]` — discarding the built schema is a bug. The fluent setters
// that return `ParameterSchema&` for chaining are intentionally NOT nodiscard
// (chaining + a final `to_json()` is the whole point).
// ===========================================================================

/// Helper: the closed-set values for the Tier-1 SWAIG enums, as the exact
/// wire strings, ready to hand to `enum_of(...)`. These reuse the SAME
/// `*_value()` normalization point as the typed `FunctionResult` overloads, so
/// a schema built with `record_format_values()` lists precisely the strings
/// `FunctionResult::record_call` accepts — one source of truth for each set.
[[nodiscard]] inline std::vector<std::string> record_format_values() {
  return {record_format_value(RecordFormat::Wav), record_format_value(RecordFormat::Mp3),
          record_format_value(RecordFormat::Mp4)};
}
[[nodiscard]] inline std::vector<std::string> record_direction_values() {
  return {record_direction_value(RecordDirection::Speak),
          record_direction_value(RecordDirection::Listen),
          record_direction_value(RecordDirection::Both)};
}
[[nodiscard]] inline std::vector<std::string> tap_direction_values() {
  return {tap_direction_value(TapDirection::Speak), tap_direction_value(TapDirection::Hear),
          tap_direction_value(TapDirection::Both)};
}
[[nodiscard]] inline std::vector<std::string> codec_values() {
  return {codec_value(Codec::Pcmu), codec_value(Codec::Pcma)};
}

/// A typed, fluent builder for a SWAIG tool's `parameters` JSON-Schema object.
///
/// Construct, chain property declarations, optionally mark some required, then
/// call `to_json()` to get the wire `json`. The result is byte-identical to
/// the equivalent hand-written `json::object(...)` blob.
class ParameterSchema {
 public:
  ParameterSchema() = default;

  // -- Scalar property kinds ----------------------------------------------
  // Each adds one entry to `properties`: a JSON-Schema object whose `type`
  // is the JSON-Schema scalar type, plus an optional `description`.

  /// Add a `"type":"string"` property.
  ParameterSchema& string(const std::string& name, const std::string& description = "") {
    return add_scalar(name, "string", description);
  }

  /// Add a `"type":"number"` property (JSON number — floating point).
  ParameterSchema& number(const std::string& name, const std::string& description = "") {
    return add_scalar(name, "number", description);
  }

  /// Add a `"type":"integer"` property.
  ParameterSchema& integer(const std::string& name, const std::string& description = "") {
    return add_scalar(name, "integer", description);
  }

  /// Add a `"type":"boolean"` property.
  ParameterSchema& boolean(const std::string& name, const std::string& description = "") {
    return add_scalar(name, "boolean", description);
  }

  // -- Enum (closed set) ---------------------------------------------------

  /// Add a `"type":"string"` property constrained to a closed set of values
  /// (`"enum":[...]`). Pass the values explicitly, or feed one of the
  /// Tier-1 helpers (`record_format_values()`, `codec_values()`, …) so the
  /// schema's accepted set matches the typed `FunctionResult` overloads
  /// exactly — INTEGRATING the RecordFormat/RecordDirection/TapDirection/
  /// Codec enums via their `*_value()` wire strings.
  ParameterSchema& enum_of(const std::string& name, const std::vector<std::string>& values,
                           const std::string& description = "") {
    json prop = json::object();
    prop["type"] = "string";
    if (!description.empty()) {
      prop["description"] = description;
    }
    json arr = json::array();
    for (const auto& v : values) {
      arr.push_back(v);
    }
    prop["enum"] = arr;
    properties_[name] = prop;
    return *this;
  }

  // -- Array ---------------------------------------------------------------

  /// Add a `"type":"array"` property whose `items` are the given scalar JSON
  /// kind (e.g. `array_of("tags", "string", "...")` → `{"type":"array",
  /// "items":{"type":"string"}}`).
  ParameterSchema& array_of(const std::string& name, const std::string& item_type,
                            const std::string& description = "") {
    json prop = json::object();
    prop["type"] = "array";
    if (!description.empty()) {
      prop["description"] = description;
    }
    prop["items"] = json::object({{"type", item_type}});
    properties_[name] = prop;
    return *this;
  }

  /// Add a `"type":"array"` property whose `items` are a full nested schema
  /// (e.g. an array of objects). The nested `ParameterSchema` is rendered to
  /// its `to_json()` and used verbatim as `items`.
  ParameterSchema& array_of(const std::string& name, const ParameterSchema& item_schema,
                            const std::string& description = "") {
    json prop = json::object();
    prop["type"] = "array";
    if (!description.empty()) {
      prop["description"] = description;
    }
    prop["items"] = item_schema.to_json();
    properties_[name] = prop;
    return *this;
  }

  // -- Object (nested) -----------------------------------------------------

  /// Add a nested `"type":"object"` property, whose `properties`/`required`
  /// come from another `ParameterSchema`. The nested schema is rendered via
  /// `to_json()` and merged with this property's `description`, producing the
  /// same `{"type":"object","properties":{...},"required":[...]}` a
  /// hand-written nested blob would.
  ParameterSchema& object_of(const std::string& name, const ParameterSchema& nested,
                             const std::string& description = "") {
    json prop = nested.to_json();  // already {type:object, properties:{...}, [required]}
    if (!description.empty()) {
      prop["description"] = description;
    }
    properties_[name] = prop;
    return *this;
  }

  // -- Escape hatch --------------------------------------------------------

  /// Add a property from a raw, pre-built JSON-Schema fragment, for shapes
  /// the typed kinds above don't cover (custom `format`, `minimum`, oneOf,
  /// …). Keeps the builder a strict superset of the hand-written path: you
  /// can always drop down to raw `json` for one property without abandoning
  /// the builder for the rest.
  ParameterSchema& property(const std::string& name, const json& schema) {
    properties_[name] = schema;
    return *this;
  }

  // -- Required ------------------------------------------------------------

  /// Mark one or more declared properties as required. Replaces any
  /// previously-set required list (call once with the full set, mirroring the
  /// single `"required":[...]` array in the hand-written form).
  ParameterSchema& required(const std::vector<std::string>& names) {
    required_ = names;
    return *this;
  }

  /// Append a single name to the required list (additive variant).
  ParameterSchema& require(const std::string& name) {
    required_.push_back(name);
    return *this;
  }

  // -- Build ---------------------------------------------------------------

  /// Render to the wire `json`: a JSON-Schema `object` with `properties` and
  /// (when any were marked) `required`. BYTE-IDENTICAL to the equivalent
  /// hand-written `json::object({{"type","object"},{"properties",{...}},
  /// {"required",[...]}})`.
  ///
  /// [[nodiscard]]: the rendered schema is the builder's whole output;
  /// discarding it discards the build.
  [[nodiscard]] json to_json() const {
    json out = json::object();
    out["type"] = "object";
    out["properties"] = properties_;
    if (!required_.empty()) {
      json req = json::array();
      for (const auto& r : required_) {
        req.push_back(r);
      }
      out["required"] = req;
    }
    return out;
  }

  /// Implicit conversion so a `ParameterSchema` can be passed straight to
  /// `define_tool(name, desc, schema, handler)` (which takes `const json&`)
  /// without an explicit `.to_json()`. Keeps the typed builder a drop-in for
  /// the raw-json parameter.
  operator json() const { return to_json(); }  // NOLINT(google-explicit-constructor)

  // -- Accessors -----------------------------------------------------------

  /// True if no properties have been declared yet.
  [[nodiscard]] bool empty() const { return properties_.empty(); }

  /// Number of declared properties.
  [[nodiscard]] std::size_t size() const { return properties_.size(); }

 private:
  ParameterSchema& add_scalar(const std::string& name, const char* json_type,
                              const std::string& description) {
    json prop = json::object();
    prop["type"] = json_type;
    if (!description.empty()) {
      prop["description"] = description;
    }
    properties_[name] = prop;
    return *this;
  }

  json properties_ = json::object();
  std::vector<std::string> required_;
};

}  // namespace swaig
}  // namespace signalwire
