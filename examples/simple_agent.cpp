// Copyright (c) 2025 SignalWire — MIT License
// Simple agent demonstrating POM prompts, SWAIG tools, hints, and languages.

#include <signalwire/agent/agent_base.hpp>
#include <ctime>

using namespace signalwire;
using json = nlohmann::json;

class SimpleAgent : public agent::AgentBase {
public:
    SimpleAgent() : AgentBase("simple", "/simple") {
        // Structured prompt via POM
        prompt_add_section("Personality", "You are a friendly and helpful assistant.");
        prompt_add_section("Goal", "Help users with basic tasks and answer questions.");
        prompt_add_section("Instructions", "", {
            "Be concise and direct in your responses.",
            "If you don't know something, say so clearly.",
            "Use the get_time function when asked about the current time.",
            "Use the get_weather function when asked about the weather."
        });

        // LLM parameters
        set_prompt_llm_params({
            {"temperature", 0.3}, {"top_p", 0.9},
            {"barge_confidence", 0.7}, {"presence_penalty", 0.1}
        });

        // Post-prompt for summary
        set_post_prompt("Return a JSON summary: {\"topic\": \"...\", \"satisfied\": true/false}");

        // Hints and pronunciation
        add_hints({"SignalWire", "SWML", "SWAIG"});
        add_pronunciation("API", "A P I", false);
        add_pronunciation("SIP", "sip", true);

        // Languages
        add_language({"English", "en-US", "inworld.Mark"});
        add_language({"Spanish", "es", "inworld.Sarah"});

        // AI parameters
        set_params({{"ai_model", "gpt-4.1-nano"}, {"wait_for_user", false}});

        // Global data
        set_global_data({{"company_name", "SignalWire"}, {"product", "AI Agent SDK"}});

        // Native functions
        set_native_functions({"check_time", "wait_seconds"});

        // SIP routing
        enable_sip_routing(true);
        register_sip_username("simple_agent");

        // Tools
        define_tool("get_time", "Get the current time",
            {{"type", "object"}, {"properties", json::object()}},
            [](const json& args, const json& raw) -> swaig::FunctionResult {
                (void)args; (void)raw;
                auto t = std::time(nullptr);
                char buf[32];
                std::strftime(buf, sizeof(buf), "%H:%M:%S", std::localtime(&t));
                return swaig::FunctionResult(std::string("The current time is ") + buf);
            });

        define_tool("get_weather", "Get weather for a location",
            {{"type", "object"}, {"properties", {
                {"location", {{"type", "string"}, {"description", "City name"}}}
            }}},
            [](const json& args, const json& raw) -> swaig::FunctionResult {
                (void)raw;
                std::string loc = args.value("location", "Unknown");
                return swaig::FunctionResult("It's sunny and 72F in " + loc + ".")
                    .update_global_data({{"weather_location", loc}});
            });
    }
};

int main() {
    SimpleAgent agent;
    std::cout << "Starting simple agent at http://0.0.0.0:3000/simple\n";
    agent.run();
}
