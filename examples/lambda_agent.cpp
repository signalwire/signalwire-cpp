// Copyright (c) 2025 SignalWire — MIT License
// Serverless Lambda handler pattern. Agent created at global scope for
// cold-start reuse. In production, wrap agent.render_swml() / HTTP handlers
// with a Lambda adapter (API Gateway proxy integration).
// For local testing, runs as a normal HTTP server.

#include <signalwire/agent/agent_base.hpp>
#include <ctime>

using namespace signalwire;
using json = nlohmann::json;

// Global agent created once per Lambda cold start
static agent::AgentBase& get_agent() {
    static agent::AgentBase a("lambda-agent", "/");

    a.add_language({"English", "en-US", "inworld.Mark"});

    a.prompt_add_section("Role",
        "You are a helpful AI assistant running in a serverless environment.");
    a.prompt_add_section("Instructions", "", {
        "Greet users warmly and offer help",
        "Use the greet_user function when asked to greet someone",
        "Use the get_time function when asked about the current time"
    });

    a.define_tool("greet_user", "Greet a user by name",
        {{"type", "object"}, {"properties", {
            {"name", {{"type", "string"}, {"description", "Name of the user"}}}
        }}},
        [](const json& args, const json& raw) -> swaig::FunctionResult {
            (void)raw;
            std::string name = args.value("name", "friend");
            return swaig::FunctionResult("Hello " + name + "! I'm running in serverless mode!");
        });

    a.define_tool("get_time", "Get the current time",
        {{"type", "object"}, {"properties", json::object()}},
        [](const json& args, const json& raw) -> swaig::FunctionResult {
            (void)args; (void)raw;
            auto t = std::time(nullptr);
            char buf[32];
            std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", std::localtime(&t));
            return swaig::FunctionResult(std::string("Current time: ") + buf);
        });

    return a;
}

// In production: wrap get_agent() HTTP endpoints with a Lambda adapter.
// For local testing:
int main() {
    auto& agent = get_agent();
    std::cout << "Starting Lambda agent (local testing) at http://0.0.0.0:3000/\n";
    std::cout << "In production, wrap render_swml() / HTTP handlers with a Lambda adapter.\n";
    agent.run();
}
