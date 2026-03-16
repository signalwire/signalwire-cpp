// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#include "signalwire/skills/skill_base.hpp"
#include "signalwire/skills/skill_registry.hpp"
#include "signalwire/datamap/datamap.hpp"

namespace signalwire {
namespace skills {

class PlayBackgroundFileSkill : public SkillBase {
public:
    std::string skill_name() const override { return "play_background_file"; }
    std::string skill_description() const override { return "Control background file playback"; }
    bool supports_multiple_instances() const override { return true; }

    bool setup(const json& params) override {
        params_ = params;
        tool_name_ = get_param<std::string>(params, "tool_name", "play_background_file");
        return params.contains("files");
    }

    std::vector<swaig::ToolDefinition> register_tools() override { return {}; }

    std::vector<json> get_datamap_functions() const override {
        if (!params_.contains("files") || !params_["files"].is_array()) return {};

        std::vector<std::string> actions;
        for (const auto& f : params_["files"]) {
            if (f.contains("key")) {
                actions.push_back("start_" + f["key"].get<std::string>());
            }
        }
        actions.push_back("stop");

        datamap::DataMap dm(tool_name_);
        dm.purpose("Control background file playback for " + tool_name_)
          .parameter("action", "string", "Playback action", true, actions);

        // Add expression for stop
        dm.expression("${args.action}", "stop",
                       swaig::FunctionResult("Stopping background playback"));

        // Add expressions for each file
        for (const auto& f : params_["files"]) {
            if (f.contains("key") && f.contains("url")) {
                std::string key = f["key"].get<std::string>();
                std::string url = f["url"].get<std::string>();
                bool wait = f.value("wait", false);
                swaig::FunctionResult output("Playing " + key);
                output.play_background_file(url, wait);
                dm.expression("${args.action}", "start_" + key, output);
            }
        }

        return {dm.to_swaig_function()};
    }

private:
    std::string tool_name_;
};

REGISTER_SKILL(PlayBackgroundFileSkill)

} // namespace skills
} // namespace signalwire
