#pragma once

#include <string>
#include <iostream>
#include <cstdlib>
#include <mutex>
#include <sstream>

namespace signalwire {

enum class LogLevel {
    Debug = 0,
    Info = 1,
    Warn = 2,
    Error = 3,
    Off = 4
};

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

    LogLevel level() const {
        return level_;
    }

    void suppress() {
        std::lock_guard<std::mutex> lock(mutex_);
        suppressed_ = true;
    }

    void unsuppress() {
        std::lock_guard<std::mutex> lock(mutex_);
        suppressed_ = false;
    }

    bool is_suppressed() const {
        return suppressed_;
    }

    void log(LogLevel level, const std::string& message) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (suppressed_ || level < level_) return;

        const char* prefix = "";
        switch (level) {
            case LogLevel::Debug: prefix = "[DEBUG] "; break;
            case LogLevel::Info:  prefix = "[INFO]  "; break;
            case LogLevel::Warn:  prefix = "[WARN]  "; break;
            case LogLevel::Error: prefix = "[ERROR] "; break;
            default: break;
        }

        if (level >= LogLevel::Warn) {
            std::cerr << prefix << message << "\n";
        } else {
            std::cout << prefix << message << "\n";
        }
    }

    void debug(const std::string& msg) { log(LogLevel::Debug, msg); }
    void info(const std::string& msg)  { log(LogLevel::Info, msg); }
    void warn(const std::string& msg)  { log(LogLevel::Warn, msg); }
    void error(const std::string& msg) { log(LogLevel::Error, msg); }

private:
    Logger() {
        const char* env_level = std::getenv("SIGNALWIRE_LOG_LEVEL");
        if (env_level) {
            std::string lvl(env_level);
            if (lvl == "debug") level_ = LogLevel::Debug;
            else if (lvl == "info") level_ = LogLevel::Info;
            else if (lvl == "warn") level_ = LogLevel::Warn;
            else if (lvl == "error") level_ = LogLevel::Error;
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

inline Logger& get_logger() {
    return Logger::instance();
}

} // namespace signalwire
