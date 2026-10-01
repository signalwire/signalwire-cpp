// Copyright (c) 2026 SignalWire
// SPDX-License-Identifier: MIT
#include "signalwire/core/capabilities.hpp"

namespace signalwire {
namespace core {
namespace capabilities {

namespace {

// Python truthiness of a JSON value: false / 0 / "" / null / [] / {} are falsey.
bool truthy(const json& v) {
  if (v.is_null()) {
    return false;
  }
  if (v.is_boolean()) {
    return v.get<bool>();
  }
  if (v.is_number_integer() || v.is_number_unsigned()) {
    return v.get<long long>() != 0;
  }
  if (v.is_number_float()) {
    return v.get<double>() != 0.0;
  }
  if (v.is_string()) {
    return !v.get_ref<const std::string&>().empty();
  }
  return !v.empty();  // array / object
}

}  // namespace

json user_variables(const json& body_params) {
  if (!body_params.is_object()) {
    return json::object();
  }
  auto vit = body_params.find("vars");
  if (vit == body_params.end() || !vit->is_object()) {
    return json::object();
  }
  auto uit = vit->find("userVariables");
  if (uit == vit->end() || !uit->is_object()) {
    return json::object();
  }
  return *uit;
}

std::set<std::string> declared_capabilities(const json& body_params) {
  json variables = user_variables(body_params);
  if (variables.empty() && body_params.is_object()) {
    // Already-extracted user variables were passed directly.
    variables = body_params;
  }
  std::set<std::string> out;
  auto cit = variables.find("capabilities");
  if (cit == variables.end() || !cit->is_object()) {
    return out;
  }
  for (auto it = cit->begin(); it != cit->end(); ++it) {
    if (truthy(it.value())) {
      out.insert(it.key());
    }
  }
  return out;
}

bool has_capability(const json& body_params, const std::string& name) {
  return declared_capabilities(body_params).count(name) > 0;
}

}  // namespace capabilities
}  // namespace core
}  // namespace signalwire
