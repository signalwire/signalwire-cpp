// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#include "signalwire/skills/skill_base.hpp"
#include "signalwire/skills/skill_registry.hpp"
#include <ctime>
#include <chrono>
#include <sstream>
#include <iomanip>

namespace signalwire {
namespace skills {

class DateTimeSkill : public SkillBase {
public:
    std::string skill_name() const override { return "datetime"; }
    std::string skill_description() const override { return "Get current date, time, and timezone information"; }
    bool supports_multiple_instances() const override { return false; }

    bool setup(const json& params) override {
        params_ = params;
        return true;
    }

    std::vector<swaig::ToolDefinition> register_tools() override {
        std::vector<swaig::ToolDefinition> tools;

        tools.push_back(define_tool(
            "get_current_time",
            "Get the current time, optionally in a specific timezone",
            json::object({
                {"type", "object"},
                {"properties", json::object({
                    {"timezone", json::object({{"type", "string"}, {"description", "Timezone (e.g., UTC, US/Eastern)"}})}
                })}
            }),
            [](const json& args, const json&) -> swaig::FunctionResult {
                auto now = std::chrono::system_clock::now();
                auto time_t = std::chrono::system_clock::to_time_t(now);
                std::tm tm_buf;
                gmtime_r(&time_t, &tm_buf);
                char buf[64];
                std::strftime(buf, sizeof(buf), "%H:%M:%S UTC", &tm_buf);
                std::string tz = "UTC";
                if (args.contains("timezone") && args["timezone"].is_string()) {
                    tz = args["timezone"].get<std::string>();
                }
                return swaig::FunctionResult(std::string("Current time (") + tz + "): " + buf);
            }
        ));

        tools.push_back(define_tool(
            "get_current_date",
            "Get the current date",
            json::object({
                {"type", "object"},
                {"properties", json::object({
                    {"timezone", json::object({{"type", "string"}, {"description", "Timezone"}})}
                })}
            }),
            [](const json& args, const json&) -> swaig::FunctionResult {
                auto now = std::chrono::system_clock::now();
                auto time_t = std::chrono::system_clock::to_time_t(now);
                std::tm tm_buf;
                gmtime_r(&time_t, &tm_buf);
                char buf[64];
                std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm_buf);
                return swaig::FunctionResult(std::string("Current date: ") + buf);
            }
        ));

        return tools;
    }

    std::vector<SkillPromptSection> get_prompt_sections() const override {
        return {{
            "Date and Time Information",
            "You can provide current date and time information.",
            {"Use get_current_time to get the current time", "Use get_current_date to get the current date"}
        }};
    }
};

REGISTER_SKILL(DateTimeSkill)

} // namespace skills
} // namespace signalwire
