// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#include "signalwire/skills/skill_base.hpp"
#include "signalwire/skills/skill_registry.hpp"
#include "signalwire/datamap/datamap.hpp"

namespace signalwire {
namespace skills {

class JokeSkill : public SkillBase {
public:
    std::string skill_name() const override { return "joke"; }
    std::string skill_description() const override { return "Tell jokes using the API Ninjas joke API"; }

    bool setup(const json& params) override {
        params_ = params;
        api_key_ = get_param_or_env(params, "api_key", "API_NINJAS_KEY");
        tool_name_ = get_param<std::string>(params, "tool_name", "get_joke");
        return !api_key_.empty();
    }

    std::vector<swaig::ToolDefinition> register_tools() override { return {}; }

    std::vector<json> get_datamap_functions() const override {
        datamap::DataMap dm(tool_name_);
        dm.purpose("Get a random joke from API Ninjas")
          .parameter("type", "string", "Type of joke", true, {"jokes", "dadjokes"})
          .webhook("GET", "https://api.api-ninjas.com/v1/${args.type}",
                   json::object({{"X-Api-Key", api_key_}}))
          .output(swaig::FunctionResult("Here's a joke: ${array[0].joke}"))
          .fallback_output(swaig::FunctionResult(
              "Why do programmers prefer dark mode? Because light attracts bugs!"));
        return {dm.to_swaig_function()};
    }

    std::vector<SkillPromptSection> get_prompt_sections() const override {
        return {{"Joke Telling", "You can tell jokes to entertain the user.",
                 {"Use " + tool_name_ + " to get a random joke"}}};
    }

    json get_global_data() const override {
        return json::object({{"joke_skill_enabled", true}});
    }

private:
    std::string api_key_;
    std::string tool_name_ = "get_joke";
};

REGISTER_SKILL(JokeSkill)

} // namespace skills
} // namespace signalwire
