// Copyright (c) 2026 SignalWire
// SPDX-License-Identifier: MIT
//
// Reading what a client says it can do.
//
// A browser client -- the SignalWire address widget, or anything speaking the
// same convention -- declares its rendering capabilities in the user variables
// it sends at dial time:
//
//   {"vars": {"userVariables": {"capabilities": {"display_content": true, ...},
//                               "metadata": {...}}}}
//
// These are declarations of what the client can RENDER, not grants of
// authority: treat them as hints for deciding what to offer, never as
// permission to do anything privileged. A caller controls its own user
// variables. Absence means no: every function here resolves errors and missing
// data to "not declared".
#pragma once

#include <nlohmann/json.hpp>
#include <set>
#include <string>

namespace signalwire {
namespace core {
namespace capabilities {

using json = nlohmann::json;

/// Return the user variables from a SWML request body (``vars.userVariables``),
/// or an empty object when any level is missing or not an object.
[[nodiscard]] json user_variables(const json& body_params);

/// Return the capability names the client declared as truthy. Accepts either a
/// full SWML request body or an already-extracted user variables object. Empty
/// when nothing was declared, the payload was malformed, or the client is not a
/// browser at all.
[[nodiscard]] std::set<std::string> declared_capabilities(const json& body_params);

/// Whether the client declared ``name`` (true only when explicitly truthy).
[[nodiscard]] bool has_capability(const json& body_params, const std::string& name);

}  // namespace capabilities
}  // namespace core
}  // namespace signalwire
