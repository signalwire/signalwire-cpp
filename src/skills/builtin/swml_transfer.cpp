// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#include "signalwire/skills/skill_base.hpp"
#include "signalwire/skills/skill_registry.hpp"
#include "signalwire/datamap/datamap.hpp"

namespace signalwire {
namespace skills {

class SwmlTransferSkill : public SkillBase {
public:
    std::string skill_name() const override { return "swml_transfer"; }
    std::string skill_description() const override {
        return "Transfer calls between agents based on pattern matching";
    }
    bool supports_multiple_instances() const override { return true; }

    bool setup(const json& params) override {
        params_ = params;
        tool_name_ = get_param<std::string>(params, "tool_name", "transfer_call");
        return params.contains("transfers");
    }

    std::vector<swaig::ToolDefinition> register_tools() override { return {}; }

    std::vector<json> get_datamap_functions() const override {
        if (!params_.contains("transfers")) return {};

        auto& transfers = params_["transfers"];
        std::string desc = get_param<std::string>(params_, "description", "Transfer call based on pattern matching");
        std::string param_name = get_param<std::string>(params_, "parameter_name", "transfer_type");

        // Build enum values from transfer keys
        std::vector<std::string> enum_vals;
        for (auto& [key, _] : transfers.items()) {
            enum_vals.push_back(key);
        }

        datamap::DataMap dm(tool_name_);
        dm.purpose(desc)
          .parameter(param_name, "string", "Transfer destination", true, enum_vals);

        // Add expressions for each transfer pattern
        for (auto& [pattern, config] : transfers.items()) {
            std::string dest;
            if (config.contains("url")) dest = config["url"].get<std::string>();
            else if (config.contains("address")) dest = config["address"].get<std::string>();

            std::string message = config.value("message", "Transferring your call");
            swaig::FunctionResult output(message);

            if (config.contains("url")) {
                output.swml_transfer(dest, message, config.value("final", true));
            }

            dm.expression("${args." + param_name + "}", pattern, output);
        }

        return {dm.to_swaig_function()};
    }

    std::vector<std::string> get_hints() const override {
        std::vector<std::string> hints = {"transfer", "connect", "speak to", "talk to"};
        if (params_.contains("transfers")) {
            for (auto& [key, _] : params_["transfers"].items()) {
                // Split key on hyphens/underscores for hints
                std::string word;
                for (char c : key) {
                    if (c == '-' || c == '_') {
                        if (!word.empty()) { hints.push_back(word); word.clear(); }
                    } else {
                        word += c;
                    }
                }
                if (!word.empty()) hints.push_back(word);
            }
        }
        return hints;
    }

    std::vector<SkillPromptSection> get_prompt_sections() const override {
        std::vector<std::string> bullets;
        if (params_.contains("transfers")) {
            for (auto& [key, config] : params_["transfers"].items()) {
                std::string desc = key;
                if (config.contains("message")) desc += " - " + config["message"].get<std::string>();
                bullets.push_back(desc);
            }
        }
        return {
            {"Transferring", "Available transfer destinations:", bullets},
            {"Transfer Instructions", "Use " + tool_name_ + " to transfer the call when requested.", {}}
        };
    }

private:
    std::string tool_name_ = "transfer_call";
};

REGISTER_SKILL(SwmlTransferSkill)

} // namespace skills
} // namespace signalwire
