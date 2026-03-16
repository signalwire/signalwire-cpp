// Copyright (c) 2025 SignalWire — MIT License
// Demonstrates DataMap tools: server-side API calls without webhooks.

#include <signalwire/agent/agent_base.hpp>
#include <signalwire/datamap/datamap.hpp>

using namespace signalwire;
using json = nlohmann::json;

int main() {
    agent::AgentBase agent("datamap-demo", "/datamap-demo");
    agent.prompt_add_section("Role", "You have data_map tools for testing.");

    // 1. Simple weather API
    auto weather = datamap::DataMap("get_weather")
        .purpose("Get current weather for a city")
        .parameter("location", "string", "City name", true)
        .webhook("GET", "https://api.weather.com/v1/current?key=KEY&q=${args.location}")
        .output(swaig::FunctionResult(
            "Weather in ${args.location}: ${response.current.condition.text}, ${response.current.temp_f}F"))
        .error_keys({"error", "message"});
    agent.register_swaig_function(weather.to_swaig_function());

    // 2. Expression-based file control
    auto file_ctrl = datamap::DataMap("file_control")
        .description("Control audio/video playback")
        .parameter("command", "string", "Playback command", true)
        .parameter("filename", "string", "File to control")
        .expression("${args.command}", "start.*",
            swaig::FunctionResult("Starting playback")
                .add_action("start_playback", {{"file", "${args.filename}"}}))
        .expression("${args.command}", "stop.*",
            swaig::FunctionResult("Stopping playback")
                .add_action("stop_playback", true));
    agent.register_swaig_function(file_ctrl.to_swaig_function());

    // 3. Knowledge search with foreach
    auto search = datamap::DataMap("search_knowledge")
        .description("Search knowledge base")
        .parameter("query", "string", "Search query", true)
        .webhook("POST", "https://api.knowledge.com/search",
            {{"Authorization", "Bearer TOKEN"}, {"Content-Type", "application/json"}})
        .body({{"query", "${query}"}, {"limit", 5}})
        .foreach({{"input_key", "${response.results}"}, {"output_key", "foreach"}, {"append", true}})
        .output(swaig::FunctionResult("Found: ${foreach.title} - ${foreach.summary}"));
    agent.register_swaig_function(search.to_swaig_function());

    std::cout << "DataMap demo at http://0.0.0.0:3000/datamap-demo\n";
    agent.run();
}
