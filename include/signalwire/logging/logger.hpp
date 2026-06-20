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
  std::string level = "";
  const char* env = std::getenv("SIGNALWIRE_LOG_LEVEL");
  if (env) level = env;
  const char* mode = std::getenv("SIGNALWIRE_LOG_MODE");
  if (mode && std::string(mode) == "off") return LogLevel::OFF;
  if (level == "debug") return LogLevel::DEBUG;
  if (level == "warn") return LogLevel::WARN;
  if (level == "error") return LogLevel::ERROR;
  return LogLevel::INFO;
}

class Logger {
 public:
  explicit Logger(const std::string& name) : name_(name) {}

  // `msg` is consumed read-only (streamed to cerr, never stored), so it
  // takes a std::string_view — callers can pass a std::string, a literal,
  // or a substring view with no allocation. (The constructor's `name`
  // stays std::string: it is retained in name_.)
  void debug(std::string_view msg) const {
    if (get_log_level() <= LogLevel::DEBUG)
      std::cerr << "[DEBUG][" << name_ << "] " << msg << std::endl;
  }
  void info(std::string_view msg) const {
    if (get_log_level() <= LogLevel::INFO)
      std::cerr << "[INFO][" << name_ << "] " << msg << std::endl;
  }
  void warn(std::string_view msg) const {
    if (get_log_level() <= LogLevel::WARN)
      std::cerr << "[WARN][" << name_ << "] " << msg << std::endl;
  }
  void error(std::string_view msg) const {
    if (get_log_level() <= LogLevel::ERROR)
      std::cerr << "[ERROR][" << name_ << "] " << msg << std::endl;
  }

 private:
  std::string name_;
};

[[nodiscard]] inline Logger get_logger(const std::string& name) { return Logger(name); }

}  // namespace logging
}  // namespace signalwire
