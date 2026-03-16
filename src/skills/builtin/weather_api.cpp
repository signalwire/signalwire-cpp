// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#include "signalwire/skills/skill_base.hpp"
#include "signalwire/skills/skill_registry.hpp"
#include "signalwire/datamap/datamap.hpp"

namespace signalwire {
namespace skills {

class WeatherApiSkill : public SkillBase {
public:
    std::string skill_name() const override { return "weather_api"; }
    std::string skill_description() const override { return "Get current weather information from WeatherAPI.com"; }

    bool setup(const json& params) override {
        params_ = params;
        api_key_ = get_param_or_env(params, "api_key", "WEATHER_API_KEY");
        tool_name_ = get_param<std::string>(params, "tool_name", "get_weather");
        temp_unit_ = get_param<std::string>(params, "temperature_unit", "fahrenheit");
        return !api_key_.empty();
    }

    std::vector<swaig::ToolDefinition> register_tools() override { return {}; }

    std::vector<json> get_datamap_functions() const override {
        std::string url = "https://api.weatherapi.com/v1/current.json?key=" +
                          api_key_ + "&q=${lc:enc:args.location}&aqi=no";

        std::string output_template;
        if (temp_unit_ == "celsius") {
            output_template = "Weather in ${response.location.name}: ${response.current.temp_c}C "
                            "(feels like ${response.current.feelslike_c}C), "
                            "${response.current.condition.text}, "
                            "Humidity: ${response.current.humidity}%, "
                            "Wind: ${response.current.wind_kph} kph";
        } else {
            output_template = "Weather in ${response.location.name}: ${response.current.temp_f}F "
                            "(feels like ${response.current.feelslike_f}F), "
                            "${response.current.condition.text}, "
                            "Humidity: ${response.current.humidity}%, "
                            "Wind: ${response.current.wind_mph} mph";
        }

        datamap::DataMap dm(tool_name_);
        dm.purpose("Get current weather information for any location")
          .parameter("location", "string", "City name or location", true)
          .webhook("GET", url)
          .output(swaig::FunctionResult(output_template));
        return {dm.to_swaig_function()};
    }

private:
    std::string api_key_;
    std::string tool_name_ = "get_weather";
    std::string temp_unit_ = "fahrenheit";
};

REGISTER_SKILL(WeatherApiSkill)

} // namespace skills
} // namespace signalwire
