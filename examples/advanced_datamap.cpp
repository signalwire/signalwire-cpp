// Copyright (c) 2025 SignalWire — MIT License
// Advanced DataMap patterns: multi-webhook, foreach, expression, fallback.

#include <signalwire/agent/agent_base.hpp>
#include <signalwire/datamap/datamap.hpp>

using namespace signalwire;
using json = nlohmann::json;

int main() {
    agent::AgentBase agent("advanced-datamap", "/advanced-datamap");
    agent.prompt_add_section("Role", "You have advanced data_map tools.");

    // Multi-webhook with fallback
    auto search = datamap::DataMap("multi_search")
        .description("Search with fallback APIs")
        .parameter("query", "string", "Search query", true)
        .parameter("priority", "string", "fast or comprehensive", false,
            {"fast", "comprehensive"})
        .webhook("GET", "https://api.fast.com/q?term=${args.query}",
            {{"X-API-Key", "FAST_KEY"}})
        .webhook("GET", "https://api.fallback.com/search?q=${args.query}",
            {{"Authorization", "Bearer TOKEN"}})
        .foreach({{"input_key", "${response.items}"}, {"output_key", "foreach"}, {"append", true}})
        .output(swaig::FunctionResult("Result: ${foreach.title} - Score: ${foreach.relevance}"))
        .error_keys({"error", "failed"});
    agent.register_swaig_function(search.to_swaig_function());

    // Expression-based routing
    auto router = datamap::DataMap("route_request")
        .description("Route requests by type")
        .parameter("type", "string", "Request type", true)
        .expression("${args.type}", "billing.*",
            swaig::FunctionResult("Routing to billing department")
                .connect("+15551001", true))
        .expression("${args.type}", "tech.*",
            swaig::FunctionResult("Routing to tech support")
                .connect("+15551002", true))
        .expression("${args.type}", "sales.*",
            swaig::FunctionResult("Routing to sales")
                .connect("+15551003", true));
    agent.register_swaig_function(router.to_swaig_function());

    std::cout << "Advanced DataMap at http://0.0.0.0:3000/advanced-datamap\n";
    agent.run();
}
