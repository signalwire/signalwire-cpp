// Copyright (c) 2025 SignalWire
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#pragma once

#include <nlohmann/json.hpp>
#include <string>

// `get_logger` below returns a NAMED logger by value, so the type must be
// complete here (not merely forward-declared).
#include "signalwire/logging/logger.hpp"

namespace signalwire {
namespace core {
namespace logging_config {

/**
 * Detect the serverless / deployment mode from the environment.
 *
 * Order of precedence (FIRST match wins):
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
 * variables (idempotent). Reads
 * ``SIGNALWIRE_LOG_MODE`` (off/stderr/default/auto) and
 * ``SIGNALWIRE_LOG_LEVEL`` and applies them to the process logger. Safe to
 * call repeatedly; only the first call takes effect until
 * ``reset_logging_configuration`` is invoked.
 */
void configure_logging();

/**
 * Reset the one-shot logging-configured flag so a subsequent
 * ``configure_logging`` call re-reads the environment. Useful when env vars
 * change at runtime.
 */
void reset_logging_configuration();

/**
 * Obtain the SDK logger, configuring it on first access. This is the single
 * entry point every SDK module should use.
 *
 * Returns a NAMED logger BY VALUE, so the caller can log directly and ``name``
 * is honoured. Delegates to ``signalwire::logging::get_logger(name)``; the only
 * thing this entry point adds is the guarantee that ``configure_logging`` has
 * run first.
 *
 * (Note ``signalwire::get_logger()``, taking no argument, is a DIFFERENT
 * overload returning the process singleton by reference; it is unrelated to
 * this contract.)
 *
 * @param name Logical logger name, conventionally the calling module's name.
 */
::signalwire::logging::Logger get_logger(const std::string& name);

/**
 * Strip control characters from a single string.
 *
 * Removes ASCII control chars except ``\t``, ``\n`` and ``\r``.
 *
 * INTERNAL helper: the public entry point is the event-map form
 * (``strip_control_chars`` below); this is the per-value scrub that form is
 * built out of.
 */
std::string strip_control_chars_str(const std::string& value);

/**
 * Strip control characters from log event values to prevent log injection.
 *
 * Takes the log event map, scrubs every STRING value, and returns the map.
 * Non-string values pass through untouched.
 *
 * This is called from ``signalwire::logging::Logger::log``, so the scrub sits
 * on the real emission path rather than merely being available to callers.
 */
nlohmann::json strip_control_chars(const nlohmann::json& event_dict);

}  // namespace logging_config
}  // namespace core
}  // namespace signalwire
