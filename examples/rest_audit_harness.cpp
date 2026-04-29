// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
//
// rest_audit_harness.cpp
//
// Runtime probe for the REST transport. Driven by porting-sdk's
// `audit_rest_transport.py`. Reads:
//   - REST_OPERATION       dotted name (e.g. `calling.list_calls`)
//   - REST_FIXTURE_URL     `http://127.0.0.1:NNNN`
//   - REST_OPERATION_ARGS  JSON dict of args for the operation
//   - SIGNALWIRE_PROJECT_ID, SIGNALWIRE_API_TOKEN
//
// Constructs a `RestClient` pointed at REST_FIXTURE_URL, invokes the
// named operation, and prints the parsed return value as JSON to stdout.
// Exits non-zero on any error.
//
// Operations supported:
//   - calling.list_calls           GET  /api/laml/2010-04-01/Accounts/{proj}/Calls.json
//   - messaging.send               POST /api/laml/2010-04-01/Accounts/{proj}/Messages.json
//   - phone_numbers.list           GET  /api/relay/rest/phone_numbers
//   - fabric.subscribers.list      GET  /api/fabric/subscribers
//   - compatibility.calls.list     GET  /api/laml/2010-04-01/Accounts/{proj}/Calls.json

#include <signalwire/rest/rest_client.hpp>
#include <signalwire/common.hpp>

#include <cstdlib>
#include <iostream>
#include <map>
#include <sstream>
#include <string>

using namespace signalwire;
using json = nlohmann::json;

namespace {

void die(const std::string& msg) {
    std::cerr << "rest_audit_harness: " << msg << "\n";
    std::exit(1);
}

std::string env_required(const char* name) {
    const char* v = std::getenv(name);
    if (!v || !*v) {
        die(std::string(name) + " env var required");
    }
    return std::string(v);
}

std::string env_or(const char* name, const std::string& fallback = "") {
    const char* v = std::getenv(name);
    return (v && *v) ? std::string(v) : fallback;
}

/// Convert a JSON value to a string (for query-string params).
std::string to_query_string(const json& v) {
    if (v.is_string()) return v.get<std::string>();
    if (v.is_number()) return v.dump();
    if (v.is_boolean()) return v.get<bool>() ? "true" : "false";
    return v.dump();
}

std::map<std::string, std::string> args_to_query_map(const json& args) {
    std::map<std::string, std::string> m;
    if (args.is_object()) {
        for (auto it = args.begin(); it != args.end(); ++it) {
            m[it.key()] = to_query_string(it.value());
        }
    }
    return m;
}

}  // namespace

int main() {
    if (!std::getenv("SIGNALWIRE_LOG_MODE")) {
        ::setenv("SIGNALWIRE_LOG_MODE", "off", 1);
    }

    const std::string operation = env_required("REST_OPERATION");
    const std::string fixture_url = env_required("REST_FIXTURE_URL");
    const std::string args_raw = env_or("REST_OPERATION_ARGS", "{}");
    const std::string project = env_required("SIGNALWIRE_PROJECT_ID");
    const std::string token = env_required("SIGNALWIRE_API_TOKEN");

    json args;
    try {
        args = json::parse(args_raw);
    } catch (const json::parse_error& e) {
        die(std::string("REST_OPERATION_ARGS not JSON: ") + e.what());
    }

    rest::RestClient client = rest::RestClient::with_base_url(fixture_url, project, token);

    json result;
    try {
        if (operation == "calling.list_calls" ||
            operation == "compatibility.calls.list") {
            // Compat / LAML calls listing — Twilio-shape path.
            std::string path = "/api/laml/2010-04-01/Accounts/" + project + "/Calls.json";
            result = client.http_client().get(path, args_to_query_map(args));
        } else if (operation == "messaging.send") {
            std::string path = "/api/laml/2010-04-01/Accounts/" + project + "/Messages.json";
            result = client.http_client().post(path, args);
        } else if (operation == "phone_numbers.list") {
            result = client.phone_numbers().list(args_to_query_map(args));
        } else if (operation == "fabric.subscribers.list") {
            result = client.fabric().subscribers.list(args_to_query_map(args));
        } else {
            die("unsupported REST_OPERATION: " + operation);
        }
    } catch (const std::exception& e) {
        die(std::string(operation) + " failed: " + e.what());
    }

    std::cout << result.dump() << "\n";
    return 0;
}
