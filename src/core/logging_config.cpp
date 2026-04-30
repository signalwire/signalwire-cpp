// Copyright (c) 2025 SignalWire
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#include "signalwire/core/logging_config.hpp"

#include <cstdlib>
#include <cstring>

namespace signalwire {
namespace core {
namespace logging_config {

namespace {

bool is_set(const char* name) {
    const char* v = std::getenv(name);
    return v != nullptr && v[0] != '\0';
}

}  // namespace

std::string get_execution_mode() {
    if (is_set("GATEWAY_INTERFACE")) {
        return "cgi";
    }
    if (is_set("AWS_LAMBDA_FUNCTION_NAME") || is_set("LAMBDA_TASK_ROOT")) {
        return "lambda";
    }
    if (is_set("FUNCTION_TARGET")
            || is_set("K_SERVICE")
            || is_set("GOOGLE_CLOUD_PROJECT")) {
        return "google_cloud_function";
    }
    if (is_set("AZURE_FUNCTIONS_ENVIRONMENT")
            || is_set("FUNCTIONS_WORKER_RUNTIME")
            || is_set("AzureWebJobsStorage")) {
        return "azure_function";
    }
    return "server";
}

}  // namespace logging_config
}  // namespace core
}  // namespace signalwire
