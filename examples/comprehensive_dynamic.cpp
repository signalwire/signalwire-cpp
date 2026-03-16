// Copyright (c) 2025 SignalWire — MIT License
// Comprehensive dynamic agent with per-request customization.

#include <signalwire/agent/agent_base.hpp>

using namespace signalwire;
using json = nlohmann::json;

int main() {
    agent::AgentBase agent("dynamic-full", "/dynamic-full");

    agent.prompt_add_section("Role", "You are a configurable multi-tenant assistant.");
    agent.set_params({{"ai_model", "gpt-4.1-nano"}});
    agent.add_skill("datetime");
    agent.add_skill("math");

    agent.set_dynamic_config_callback(
        [](const std::map<std::string, std::string>& query,
           const json& body, const std::map<std::string, std::string>& headers,
           agent::AgentBase& copy) {
            (void)body; (void)headers;

            // Tenant customization
            auto t = query.find("tenant");
            if (t != query.end()) {
                copy.set_name(t->second + " Assistant");
                copy.prompt_add_section("Tenant", "You work for " + t->second);
                copy.set_global_data({{"tenant_id", t->second}});
            }

            // Voice selection
            auto v = query.find("voice");
            if (v != query.end()) {
                copy.add_language({"Custom", "en-US", v->second});
            }

            // Model override
            auto m = query.find("model");
            if (m != query.end()) {
                copy.set_params({{"ai_model", m->second}});
            }

            // Extra instructions
            auto i = query.find("instructions");
            if (i != query.end()) {
                copy.prompt_add_section("Extra Instructions", i->second);
            }
        });

    std::cout << "Comprehensive dynamic at http://0.0.0.0:3000/dynamic-full\n";
    std::cout << "Try: ?tenant=Acme&voice=inworld.Sarah&model=gpt-4.1-nano\n";
    agent.run();
}
