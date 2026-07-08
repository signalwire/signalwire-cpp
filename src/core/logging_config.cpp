// Copyright (c) 2025 SignalWire
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#include "signalwire/core/logging_config.hpp"

#include <atomic>
#include <cstdlib>
#include <cstring>

#include "signalwire/logging.hpp"

namespace signalwire {
namespace core {
namespace logging_config {

namespace {

bool is_set(const char* name) {
  const char* v = std::getenv(name);
  return v != nullptr && v[0] != '\0';
}

// One-shot "logging configured" flag, mirroring Python's module-level
// ``_logging_configured``. Guards configure_logging() so it only applies once
// until reset_logging_configuration() clears it.
std::atomic<bool> g_configured{false};

}  // namespace

std::string get_execution_mode() {
  if (is_set("GATEWAY_INTERFACE")) {
    return "cgi";
  }
  if (is_set("AWS_LAMBDA_FUNCTION_NAME") || is_set("LAMBDA_TASK_ROOT")) {
    return "lambda";
  }
  if (is_set("FUNCTION_TARGET") || is_set("K_SERVICE") || is_set("GOOGLE_CLOUD_PROJECT")) {
    return "google_cloud_function";
  }
  if (is_set("AZURE_FUNCTIONS_ENVIRONMENT") || is_set("FUNCTIONS_WORKER_RUNTIME") ||
      is_set("AzureWebJobsStorage")) {
    return "azure_function";
  }
  return "server";
}

void configure_logging() {
  // Idempotent: only the first call takes effect until reset.
  bool expected = false;
  if (!g_configured.compare_exchange_strong(expected, true)) {
    return;
  }
  // Honor an explicit "off" mode by suppressing the logger; otherwise the
  // Logger self-configures its level from SIGNALWIRE_LOG_LEVEL on first use.
  auto& logger = signalwire::get_logger();
  const char* mode = std::getenv("SIGNALWIRE_LOG_MODE");
  if (mode != nullptr && std::strcmp(mode, "off") == 0) {
    logger.suppress();
  } else {
    logger.unsuppress();
  }
}

void reset_logging_configuration() { g_configured.store(false); }

bool get_logger(const std::string& /*name*/) {
  // Single entry point: ensure the process logger is configured before use.
  if (!g_configured.load()) {
    configure_logging();
  }
  return g_configured.load();
}

std::string strip_control_chars(const std::string& value) {
  std::string out;
  out.reserve(value.size());
  for (char c : value) {
    auto uc = static_cast<unsigned char>(c);
    // Drop ASCII control chars except tab, newline, carriage-return, and the
    // DEL char; preserve everything printable / UTF-8 continuation bytes.
    if ((uc < 0x20 && c != '\t' && c != '\n' && c != '\r') || uc == 0x7f) {
      continue;
    }
    out.push_back(c);
  }
  return out;
}

}  // namespace logging_config
}  // namespace core
}  // namespace signalwire
