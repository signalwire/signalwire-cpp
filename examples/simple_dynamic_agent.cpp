// Copyright (c) 2025 SignalWire — MIT License
// Dynamic agent: per-request customization via DynamicConfigCallback.

#include <signalwire/agent/agent_base.hpp>

using namespace signalwire;
using json = nlohmann::json;

int main() {
    agent::AgentBase agent("dynamic", "/dynamic");

    agent.prompt_add_section("Role", "You are a customer support agent.");
    agent.set_params({{"ai_model", "gpt-4.1-nano"}});

    // Dynamic config: customize per request based on query params
    agent.set_dynamic_config_callback(
        [](const std::map<std::string, std::string>& query,
           const json& body,
           const std::map<std::string, std::string>& headers,
           agent::AgentBase& copy) {
            (void)body; (void)headers;
            auto it = query.find("tenant");
            if (it != query.end()) {
                copy.prompt_add_section("Tenant", "You work for " + it->second + ".");
                copy.set_global_data({{"tenant", it->second}});
            }
            auto lang = query.find("lang");
            if (lang != query.end() && lang->second == "es") {
                copy.add_language({"Spanish", "es", "inworld.Sarah"});
            }
        });

    std::cout << "Dynamic agent at http://0.0.0.0:3000/dynamic\n";
    std::cout << "Try: ?tenant=AcmeCorp&lang=es\n";
    agent.run();
}
