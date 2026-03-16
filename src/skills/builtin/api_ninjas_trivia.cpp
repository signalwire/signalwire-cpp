// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#include "signalwire/skills/skill_base.hpp"
#include "signalwire/skills/skill_registry.hpp"
#include "signalwire/datamap/datamap.hpp"

namespace signalwire {
namespace skills {

class ApiNinjasTriviaSkill : public SkillBase {
public:
    std::string skill_name() const override { return "api_ninjas_trivia"; }
    std::string skill_description() const override { return "Get trivia questions from API Ninjas"; }
    bool supports_multiple_instances() const override { return true; }

    bool setup(const json& params) override {
        params_ = params;
        api_key_ = get_param_or_env(params, "api_key", "API_NINJAS_KEY");
        tool_name_ = get_param<std::string>(params, "tool_name", "get_trivia");
        return !api_key_.empty();
    }

    std::vector<swaig::ToolDefinition> register_tools() override { return {}; }

    std::vector<json> get_datamap_functions() const override {
        std::vector<std::string> categories = {
            "artliterature", "language", "sciencenature", "general",
            "fooddrink", "peopleplaces", "geography", "historyholidays",
            "entertainment", "toysgames", "music", "mathematics",
            "religionmythology", "sportsleisure"
        };
        if (params_.contains("categories") && params_["categories"].is_array()) {
            categories.clear();
            for (const auto& c : params_["categories"]) {
                categories.push_back(c.get<std::string>());
            }
        }

        datamap::DataMap dm(tool_name_);
        dm.purpose("Get trivia questions for " + tool_name_)
          .parameter("category", "string", "Trivia category", true, categories)
          .webhook("GET", "https://api.api-ninjas.com/v1/trivia?category=${args.category}",
                   json::object({{"X-Api-Key", api_key_}}))
          .output(swaig::FunctionResult(
              "Category ${array[0].category} question: ${array[0].question} "
              "Answer: ${array[0].answer}, be sure to give the user time to answer before saying the answer."));

        return {dm.to_swaig_function()};
    }

private:
    std::string api_key_;
    std::string tool_name_ = "get_trivia";
};

REGISTER_SKILL(ApiNinjasTriviaSkill)

} // namespace skills
} // namespace signalwire
