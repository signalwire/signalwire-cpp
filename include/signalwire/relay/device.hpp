// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#pragma once

#include <string>
#include <nlohmann/json.hpp>

namespace signalwire {
namespace relay {

using json = nlohmann::json;

// ===========================================================================
// Device — the {type, params} object passed as a raw json map across the RELAY
// calling methods that target an endpoint: connect / refer / dial / tap.
//
// Grounded in `porting-sdk/relay-protocol/calling.{dial,connect,refer}.params.json`
// (extracted from switchblade `PublicCall*Params.cs`): each device is an object
// with a REQUIRED string `type` discriminant (e.g. "phone", "sip", "webrtc")
// and an open `params` object whose keys depend on `type`. The schema is
// `additionalProperties:true` and does NOT enumerate the `type` values, so we
// type the SHAPE only: `type` stays a `std::string` (open discriminant), and
// `params` stays a free `json` map. `to_json()` yields the IDENTICAL wire shape
// the hand-written `{{"type",...},{"params",...}}` map produces.
//
// Additive idiom (PORT_ADDITIONS.md): the raw-`json` connect/dial/refer/tap
// overloads stay canonical (parity with Python's nested dict/list). `Device`
// is a typed convenience for assembling that map with a named field instead of
// stringly keys — `Device{"phone", {{"to_number", to}}}` reads better than the
// brace-soup and can't typo the two top-level keys.
// ===========================================================================
struct Device {
    /// REQUIRED endpoint-type discriminant. Open set (not schema-enumerated) →
    /// kept a std::string. Common values: "phone", "sip", "webrtc".
    std::string type;
    /// Type-specific parameters (e.g. {"to_number","from_number"} for "phone").
    /// Free json map — the wire schema is additionalProperties:true.
    json params = json::object();

    Device() = default;
    Device(std::string type_, json params_ = json::object())
        : type(std::move(type_)), params(std::move(params_)) {}

    /// Serialize to the exact RELAY device wire shape: {"type":..,"params":..}.
    /// Byte-identical to the hand-written map the raw-json call sites build.
    [[nodiscard]] json to_json() const {
        json j;
        j["type"] = type;
        j["params"] = params;
        return j;
    }
};

} // namespace relay
} // namespace signalwire
