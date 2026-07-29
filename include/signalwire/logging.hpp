#pragma once

#include <cstdlib>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>

// For the control-char scrub applied on the emission path below. logging_config
// includes signalwire/logging/logger.hpp (a DIFFERENT header from this one), so
// this does not close an include cycle.
#include "signalwire/core/logging_config.hpp"

namespace signalwire {

enum class LogLevel { Debug = 0, Info = 1, Warn = 2, Error = 3, Off = 4 };

/// The SDK's process-wide logging singleton.
///
/// One instance per process, reached through ``Logger::instance()`` (or the
/// ``get_logger()`` free function); non-copyable. Every operation is guarded by
/// an internal mutex, so concurrent logging from the agent's HTTP threads and
/// the RELAY reader thread is safe. Records at or above ``level()`` are
/// emitted, with ``Warn`` and ``Error`` going to ``stderr`` and the rest to
/// ``stdout``; ``suppress()`` silences output entirely without disturbing the
/// configured level (``unsuppress()`` restores it).
///
/// Initial state comes from the environment at first use:
/// ``SIGNALWIRE_LOG_LEVEL`` (``debug``/``info``/``warn``/``error``, default
/// ``Info``) and ``SIGNALWIRE_LOG_MODE=off``, which starts the logger
/// suppressed.
///
/// Every message is scrubbed of control characters ON THE EMISSION PATH before
/// it is written. This is log-injection defence, and matches the reference
/// registering ``strip_control_chars`` in both of its structlog processor
/// chains: without it a caller-supplied ``\x00`` or ``\x1b[`` escape reaches
/// the terminal verbatim and can forge log lines.
///
/// Distinct from ``signalwire::logging::Logger`` (``signalwire/logging/
/// logger.hpp``), which is a per-component NAMED logger created by value.
class Logger {
 public:
  static Logger& instance() {
    static Logger logger;
    return logger;
  }

  void set_level(LogLevel level) {
    std::lock_guard<std::mutex> lock(mutex_);
    level_ = level;
  }

  [[nodiscard]] LogLevel level() const { return level_; }

  void suppress() {
    std::lock_guard<std::mutex> lock(mutex_);
    suppressed_ = true;
  }

  void unsuppress() {
    std::lock_guard<std::mutex> lock(mutex_);
    suppressed_ = false;
  }

  [[nodiscard]] bool is_suppressed() const { return suppressed_; }

  // `message` is read-only here (streamed to cout/cerr, never stored), so
  // it takes a std::string_view; the convenience debug/info/warn/error
  // wrappers below forward their view through unchanged.
  void log(LogLevel level, std::string_view message) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (suppressed_ || level < level_) {
      return;
    }

    const char* prefix = "";
    switch (level) {
      case LogLevel::Debug:
        prefix = "[DEBUG] ";
        break;
      case LogLevel::Info:
        prefix = "[INFO]  ";
        break;
      case LogLevel::Warn:
        prefix = "[WARN]  ";
        break;
      case LogLevel::Error:
        prefix = "[ERROR] ";
        break;
      default:
        break;
    }

    // Scrub control characters BEFORE emitting — log-injection defence, and the
    // reason the reference registers strip_control_chars in both of its structlog
    // processor chains. A port that merely EXPOSES the scrub without putting it on
    // the emission path offers no protection at all: a caller-supplied `\x00` or a
    // `\x1b[` escape reaches the terminal verbatim and can forge log lines.
    const std::string safe =
        ::signalwire::core::logging_config::strip_control_chars_str(std::string(message));

    if (level >= LogLevel::Warn) {
      std::cerr << prefix << safe << "\n";
    } else {
      std::cout << prefix << safe << "\n";
    }
  }

  void debug(std::string_view msg) { log(LogLevel::Debug, msg); }
  void info(std::string_view msg) { log(LogLevel::Info, msg); }
  void warn(std::string_view msg) { log(LogLevel::Warn, msg); }
  void error(std::string_view msg) { log(LogLevel::Error, msg); }

 private:
  Logger() {
    const char* env_level = std::getenv("SIGNALWIRE_LOG_LEVEL");
    if (env_level) {
      std::string lvl(env_level);
      if (lvl == "debug") {
        level_ = LogLevel::Debug;
      } else if (lvl == "info") {
        level_ = LogLevel::Info;
      } else if (lvl == "warn") {
        level_ = LogLevel::Warn;
      } else if (lvl == "error") {
        level_ = LogLevel::Error;
      }
    }

    const char* env_mode = std::getenv("SIGNALWIRE_LOG_MODE");
    if (env_mode && std::string(env_mode) == "off") {
      suppressed_ = true;
    }
  }

  Logger(const Logger&) = delete;
  Logger& operator=(const Logger&) = delete;

  LogLevel level_ = LogLevel::Info;
  bool suppressed_ = false;
  std::mutex mutex_;
};

inline Logger& get_logger() { return Logger::instance(); }

}  // namespace signalwire
