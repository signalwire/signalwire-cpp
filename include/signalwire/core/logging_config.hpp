// Copyright (c) 2025 SignalWire
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#pragma once

#include <string>

// `get_logger` below returns a NAMED logger by value, so the type must be
// complete here (not merely forward-declared).
#include "signalwire/logging/logger.hpp"

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
 * Obtain the SDK logger, configuring it on first access. This is the single
 * entry point every SDK module should use, mirroring Python's
 * ``signalwire.core.logging_config.get_logger``.
 *
 * Returns a NAMED logger so a CALLER CAN ACTUALLY LOG, and so ``name`` means
 * something. It previously returned ``bool`` (the internal configured-once flag)
 * and discarded ``name`` entirely, which left the canonical entry point unable
 * to hand back a logger at all — a caller had to already know to reach into a
 * different header. Every other port returns a logger object here (ts
 * ``Logger``, go ``*logging.Logger``, java / php / rust / dotnet ``Logger``,
 * ruby ``Logging::Logger``), so the ``bool`` form was a functional gap, not an
 * idiom. It went unnoticed because the reference records this function's return
 * as ``any``, and the signature differ treats ``any`` as matching anything on
 * either side.
 *
 * Delegates to ``signalwire::logging::get_logger(name)``, which already built
 * the named-logger form — this entry point simply guarantees configuration has
 * happened first, which is exactly the reference's single-entry-point contract.
 * (Note ``signalwire::get_logger()``, no argument, is a THIRD overload returning
 * the process singleton by reference; it is unrelated to this contract.)
 *
 * @param name Logical logger name, as in the reference's per-module
 *   ``get_logger(__name__)``.
 */
::signalwire::logging::Logger get_logger(const std::string& name);

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
