// Copyright (c) 2025 SignalWire — MIT License
// LLM parameter tuning for different use cases.

#include <signalwire/agent/agent_base.hpp>

using namespace signalwire;
using json = nlohmann::json;

int main() {
    agent::AgentBase agent("llm-params", "/llm-params");

    agent.prompt_add_section("Role", "You are a technical support agent.");

    // Conservative parameters for technical support
    agent.set_prompt_llm_params({
        {"temperature", 0.2},
        {"top_p", 0.9},
        {"barge_confidence", 0.8},
        {"presence_penalty", 0.0},
        {"frequency_penalty", 0.1}
    });

    // Different parameters for post-prompt analysis
    agent.set_post_prompt_llm_params({
        {"temperature", 0.1},
        {"top_p", 0.95}
    });

    agent.set_post_prompt("Classify the issue: {\"category\": \"...\", \"severity\": \"low|medium|high\"}");

    agent.set_params({
        {"ai_model", "gpt-4.1-nano"},
        {"end_of_speech_timeout", 1500},
        {"ai_volume", 5}
    });

    std::cout << "LLM params demo at http://0.0.0.0:3000/llm-params\n";
    agent.run();
}
