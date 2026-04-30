// Copyright (c) 2025 SignalWire
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#pragma once

#include <string>

namespace signalwire {
namespace core {
namespace logging_config {

/**
 * Cross-language SDK contract for serverless / deployment-mode detection.
 *
 * Mirrors `signalwire.core.logging_config.get_execution_mode` in the
 * Python reference. Order of precedence (FIRST match wins):
 *
 *   1. GATEWAY_INTERFACE                                       -> "cgi"
 *   2. AWS_LAMBDA_FUNCTION_NAME or LAMBDA_TASK_ROOT            -> "lambda"
 *   3. FUNCTION_TARGET, K_SERVICE, or GOOGLE_CLOUD_PROJECT     -> "google_cloud_function"
 *   4. AZURE_FUNCTIONS_ENVIRONMENT, FUNCTIONS_WORKER_RUNTIME, or
 *      AzureWebJobsStorage                                     -> "azure_function"
 *   5. otherwise                                               -> "server"
 *
 * @return The detected mode as a canonical lower-case string. One of
 *   "cgi", "lambda", "google_cloud_function", "azure_function", or
 *   "server".
 */
std::string get_execution_mode();

}  // namespace logging_config
}  // namespace core
}  // namespace signalwire
