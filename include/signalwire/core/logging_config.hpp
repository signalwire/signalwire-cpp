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

/**
 * Configure the SDK logging system once, globally, from environment
 * variables (idempotent). Mirrors Python's
 * ``signalwire.core.logging_config.configure_logging``. Reads
 * ``SIGNALWIRE_LOG_MODE`` (off/stderr/default/auto) and
 * ``SIGNALWIRE_LOG_LEVEL`` and applies them to the process logger. Safe to
 * call repeatedly; only the first call takes effect until
 * ``reset_logging_configuration`` is invoked.
 */
void configure_logging();

/**
 * Reset the one-shot logging-configured flag so a subsequent
 * ``configure_logging`` call re-reads the environment. Mirrors Python's
 * ``reset_logging_configuration`` (useful when env vars change at runtime).
 */
void reset_logging_configuration();

/**
 * Return whether ``configure_logging`` has already run (the internal flag).
 * Ensures the logger is configured on first access, mirroring Python's
 * ``get_logger`` single-entry-point behavior. The C++ logger is a process
 * singleton (see ``signalwire::get_logger``); this helper guarantees it has
 * been configured before use and returns the configured state.
 *
 * @param name Logical logger name (recorded for parity; the C++ Logger is a
 *   process singleton so the name is advisory).
 */
bool get_logger(const std::string& name);

/**
 * Strip control characters (to prevent log injection) from ``value``.
 * Mirrors Python's ``strip_control_chars`` structlog processor, reduced to
 * the value-sanitizing core: removes ASCII control chars except ``\t``,
 * ``\n`` and ``\r``.
 */
std::string strip_control_chars(const std::string& value);

}  // namespace logging_config
}  // namespace core
}  // namespace signalwire
