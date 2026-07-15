// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#include <sys/stat.h>

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>

#include "signalwire/skills/skill_base.hpp"
#include "signalwire/skills/skill_registry.hpp"

namespace signalwire {
namespace skills {

namespace {

// A garbage TZ silently falls back to UTC in both glibc and the BSD/macOS libc
// (an invalid zone resolves to a UTC-offset-0 "UTC" zone), so applying it and
// trusting the result would return a UTC answer LABELLED as the requested zone
// — the exact bug this fix removes. Detect an unknown zone up front by checking
// the platform zoneinfo database, which is what `tzset` itself reads. UTC needs
// no lookup (it is always valid). Path-escape inputs are rejected.
bool timezone_exists(const std::string& name) {
  if (name.empty()) {
    return false;
  }
  if (name.front() == '/' || name.find("..") != std::string::npos) {
    return false;
  }
  static const char* const kZoneInfoDirs[] = {"/usr/share/zoneinfo/", "/etc/zoneinfo/",
                                              "/usr/lib/zoneinfo/", nullptr};
  for (int i = 0; kZoneInfoDirs[i] != nullptr; ++i) {
    std::string path = std::string(kZoneInfoDirs[i]) + name;
    struct stat st;
    if (stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
      return true;
    }
  }
  return false;
}

// Resolve `epoch` to broken-down local time in zone `tz_name`, formatting with
// `fmt`. Returns true and fills `out` on success; false for an unknown zone.
//
// POSIX time-zone resolution is process-global: it goes through the TZ env var
// + tzset(). This skill runs inside a multithreaded server, so the mutation is
// guarded by a mutex (no two threads race on TZ) AND the prior TZ is saved and
// restored, so the skill never leaks a mutated global TZ back to the process.
bool format_time_in_zone(std::time_t epoch, const std::string& tz_name, const char* fmt,
                         std::string& out) {
  const bool is_utc = tz_name.empty() || tz_name == "UTC" || tz_name == "utc";
  if (!is_utc && !timezone_exists(tz_name)) {
    return false;
  }

  static std::mutex tz_mutex;
  std::lock_guard<std::mutex> lock(tz_mutex);

  const char* prev = std::getenv("TZ");
  const std::string saved = (prev != nullptr) ? std::string(prev) : std::string();
  const bool had_tz = (prev != nullptr);

  setenv("TZ", is_utc ? "UTC" : tz_name.c_str(), 1);
  tzset();

  std::tm tm_buf{};
  localtime_r(&epoch, &tm_buf);
  char buf[96];
  std::strftime(buf, sizeof(buf), fmt, &tm_buf);
  out.assign(buf);

  if (had_tz) {
    setenv("TZ", saved.c_str(), 1);
  } else {
    unsetenv("TZ");
  }
  tzset();
  return true;
}

}  // namespace

class DateTimeSkill : public SkillBase {
 public:
  std::string skill_name() const override { return "datetime"; }
  std::string skill_description() const override {
    return "Get current date, time, and timezone information";
  }
  bool supports_multiple_instances() const override { return false; }

  bool setup(const json& params) override {
    params_ = params;
    return true;
  }

  std::vector<swaig::ToolDefinition> register_tools() override {
    std::vector<swaig::ToolDefinition> tools;

    tools.push_back(define_tool(
        "get_current_time", "Get the current time, optionally in a specific timezone",
        json::object(
            {{"type", "object"},
             {"properties",
              json::object({{"timezone", json::object({{"type", "string"},
                                                       {"description",
                                                        "Timezone (e.g., UTC, US/Eastern)"}})}})}}),
        [](const json& args, const json&) -> swaig::FunctionResult {
          std::string tz = "UTC";
          if (args.contains("timezone") && args["timezone"].is_string()) {
            tz = args["timezone"].get<std::string>();
          }
          auto now = std::chrono::system_clock::now();
          auto now_t = std::chrono::system_clock::to_time_t(now);
          std::string buf;
          // "%Z" emits the zone abbreviation resolved for `tz`, so the answer is
          // the real local time in the requested zone, not UTC.
          if (!format_time_in_zone(now_t, tz, "%H:%M:%S %Z", buf)) {
            return swaig::FunctionResult(std::string("Error getting time: unknown timezone '") +
                                         tz + "'");
          }
          return swaig::FunctionResult(std::string("Current time (") + tz + "): " + buf);
        }));

    tools.push_back(define_tool(
        "get_current_date", "Get the current date",
        json::object({{"type", "object"},
                      {"properties",
                       json::object({{"timezone", json::object({{"type", "string"},
                                                                {"description", "Timezone"}})}})}}),
        [](const json& args, const json&) -> swaig::FunctionResult {
          std::string tz = "UTC";
          if (args.contains("timezone") && args["timezone"].is_string()) {
            tz = args["timezone"].get<std::string>();
          }
          auto now = std::chrono::system_clock::now();
          auto now_t = std::chrono::system_clock::to_time_t(now);
          std::string buf;
          // The calendar date differs by zone near midnight, so resolve it in
          // the requested zone rather than always in UTC.
          if (!format_time_in_zone(now_t, tz, "%Y-%m-%d", buf)) {
            return swaig::FunctionResult(std::string("Error getting date: unknown timezone '") +
                                         tz + "'");
          }
          return swaig::FunctionResult(std::string("Current date: ") + buf);
        }));

    return tools;
  }

  std::vector<SkillPromptSection> get_prompt_sections() const override {
    return {{"Date and Time Information",
             "You can provide current date and time information.",
             {"Use get_current_time to get the current time",
              "Use get_current_date to get the current date"}}};
  }
};

REGISTER_SKILL(DateTimeSkill)

}  // namespace skills
}  // namespace signalwire
