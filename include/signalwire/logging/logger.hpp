// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#pragma once

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

namespace signalwire {
namespace logging {

enum class LogLevel { DEBUG, INFO, WARN, ERROR, OFF };

[[nodiscard]] inline LogLevel get_log_level() {
  std::string level;
  const char* env = std::getenv("SIGNALWIRE_LOG_LEVEL");
  if (env) {
    level = env;
  }
  const char* mode = std::getenv("SIGNALWIRE_LOG_MODE");
  if (mode && std::string(mode) == "off") {
    return LogLevel::OFF;
  }
  if (level == "debug") {
    return LogLevel::DEBUG;
  }
  if (level == "warn") {
    return LogLevel::WARN;
  }
  if (level == "error") {
    return LogLevel::ERROR;
  }
  return LogLevel::INFO;
}

/// A named, per-component logger created by value.
///
/// Construct one per subsystem (or via ``get_logger("name")``) and the name is
/// stamped into every line: ``[LEVEL][name] message``. Cheap to copy and hold
/// as a member — it carries only its name; there is no shared state, no mutex,
/// and no registry.
///
/// The threshold is NOT stored on the instance: each call re-reads
/// ``get_log_level()``, which derives the level from ``SIGNALWIRE_LOG_LEVEL``
/// (``debug``/``warn``/``error``, defaulting to ``INFO``) and from
/// ``SIGNALWIRE_LOG_MODE=off``, which turns everything off. So a change to the
/// environment takes effect on the next call rather than at construction. All
/// levels write to ``stderr``, and the logging methods are ``const``.
///
/// Distinct from ``signalwire::Logger`` (``signalwire/logging.hpp``), which is
/// the mutex-guarded, suppressible process singleton that scrubs control
/// characters on emission. This one does neither — it is the lightweight
/// component-tagged logger.
class Logger {
 public:
  explicit Logger(const std::string& name) : name_(name) {}

  // `msg` is consumed read-only (streamed to cerr, never stored), so it
  // takes a std::string_view — callers can pass a std::string, a literal,
  // or a substring view with no allocation. (The constructor's `name`
  // stays std::string: it is retained in name_.)
  void debug(std::string_view msg) const {
    if (get_log_level() <= LogLevel::DEBUG) {
      std::cerr << "[DEBUG][" << name_ << "] " << msg << "\n";
    }
  }
  void info(std::string_view msg) const {
    if (get_log_level() <= LogLevel::INFO) {
      std::cerr << "[INFO][" << name_ << "] " << msg << "\n";
    }
  }
  void warn(std::string_view msg) const {
    if (get_log_level() <= LogLevel::WARN) {
      std::cerr << "[WARN][" << name_ << "] " << msg << "\n";
    }
  }
  void error(std::string_view msg) const {
    if (get_log_level() <= LogLevel::ERROR) {
      std::cerr << "[ERROR][" << name_ << "] " << msg << "\n";
    }
  }

 private:
  std::string name_;
};

[[nodiscard]] inline Logger get_logger(const std::string& name) { return Logger(name); }

}  // namespace logging
}  // namespace signalwire
