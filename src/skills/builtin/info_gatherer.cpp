// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#include "signalwire/skills/skill_base.hpp"
#include "signalwire/skills/skill_registry.hpp"

namespace signalwire {
namespace skills {

class InfoGathererSkill : public SkillBase {
public:
    std::string skill_name() const override { return "info_gatherer"; }
    std::string skill_description() const override {
        return "Gather answers to a configurable list of questions";
    }
    bool supports_multiple_instances() const override { return true; }

    bool setup(const json& params) override {
        params_ = params;
        prefix_ = get_param<std::string>(params, "prefix", "");
        completion_msg_ = get_param<std::string>(params, "completion_message",
            "Thank you for providing all the information!");
        return params.contains("questions");
    }

    std::vector<swaig::ToolDefinition> register_tools() override {
        std::string start_name = prefix_.empty() ? "start_questions" : prefix_ + "_start_questions";
        std::string submit_name = prefix_.empty() ? "submit_answer" : prefix_ + "_submit_answer";
        std::string instance_key = get_instance_key();

        auto questions = params_.value("questions", json::array());

        std::vector<swaig::ToolDefinition> tools;

        tools.push_back(define_tool(start_name, "Start the information gathering process",
            json::object({{"type", "object"}, {"properties", json::object()}}),
            [questions, instance_key](const json&, const json&) -> swaig::FunctionResult {
                if (questions.empty()) {
                    return swaig::FunctionResult("No questions configured");
                }
                std::string first_q = questions[0].value("question_text", "");
                swaig::FunctionResult result("Starting questions. First question: " + first_q);
                result.update_global_data(json::object({
                    {instance_key, json::object({
                        {"question_index", 0},
                        {"answers", json::array()},
                        {"questions", questions}
                    })}
                }));
                return result;
            }));

        tools.push_back(define_tool(submit_name, "Submit an answer to the current question",
            json::object({{"type", "object"}, {"properties", json::object({
                {"answer", json::object({{"type", "string"}, {"description", "The answer"}})},
                {"confirmed_by_user", json::object({{"type", "boolean"}, {"description", "User confirmed"}})}
            })}, {"required", json::array({"answer"})}}),
            [this, questions, instance_key](const json& args, const json& raw) -> swaig::FunctionResult {
                std::string answer = args.value("answer", "");
                bool confirmed = args.value("confirmed_by_user", true);

                // Get current state from global data
                int q_idx = 0;
                json answers = json::array();
                if (raw.contains("global_data") && raw["global_data"].contains(instance_key)) {
                    auto& state = raw["global_data"][instance_key];
                    q_idx = state.value("question_index", 0);
                    answers = state.value("answers", json::array());
                }

                if (q_idx >= static_cast<int>(questions.size())) {
                    return swaig::FunctionResult(completion_msg_);
                }

                // Check if confirmation needed
                bool needs_confirm = questions[q_idx].value("confirm", false);
                if (needs_confirm && !confirmed) {
                    return swaig::FunctionResult(
                        "Please confirm the answer: " + answer);
                }

                answers.push_back(json::object({
                    {"key", questions[q_idx].value("key_name", "")},
                    {"answer", answer}
                }));

                int next_idx = q_idx + 1;
                swaig::FunctionResult result("");

                if (next_idx >= static_cast<int>(questions.size())) {
                    result.set_response(completion_msg_);
                } else {
                    std::string next_q = questions[next_idx].value("question_text", "");
                    result.set_response("Got it. Next question: " + next_q);
                }

                result.update_global_data(json::object({
                    {instance_key, json::object({
                        {"question_index", next_idx},
                        {"answers", answers},
                        {"questions", questions}
                    })}
                }));

                return result;
            }));

        return tools;
    }

    json get_global_data() const override {
        std::string key = get_instance_key();
        auto questions = params_.value("questions", json::array());
        return json::object({
            {key, json::object({
                {"questions", questions},
                {"question_index", 0},
                {"answers", json::array()}
            })}
        });
    }

    std::vector<SkillPromptSection> get_prompt_sections() const override {
        std::string key = get_instance_key();
        return {{"Info Gatherer (" + key + ")",
                 "You need to gather information by asking questions one at a time.",
                 {"Start by calling start_questions to get the first question",
                  "For each answer, call submit_answer",
                  "Ask questions one at a time in order"}}};
    }

    std::string get_instance_key() const override {
        if (!prefix_.empty()) return "info_gatherer_" + prefix_;
        return "info_gatherer";
    }

private:
    std::string prefix_;
    std::string completion_msg_;
};

REGISTER_SKILL(InfoGathererSkill)

} // namespace skills
} // namespace signalwire
