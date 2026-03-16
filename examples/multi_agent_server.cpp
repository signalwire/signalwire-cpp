// Copyright (c) 2025 SignalWire — MIT License
// Multi-agent server hosting several agents on one port.

#include <signalwire/agent/agent_base.hpp>
#include <signalwire/server/agent_server.hpp>

using namespace signalwire;
using json = nlohmann::json;

int main() {
    // Create agents
    auto sales = std::make_shared<agent::AgentBase>("sales", "/sales");
    sales->prompt_add_section("Role", "You are a sales representative.");
    sales->add_skill("datetime");

    auto support = std::make_shared<agent::AgentBase>("support", "/support");
    support->prompt_add_section("Role", "You are a technical support specialist.");
    support->add_skill("math");

    auto billing = std::make_shared<agent::AgentBase>("billing", "/billing");
    billing->prompt_add_section("Role", "You are a billing assistant.");

    // Host them all on one server
    server::AgentServer srv("0.0.0.0", 3000);
    srv.register_agent(sales, "/sales");
    srv.register_agent(support, "/support");
    srv.register_agent(billing, "/billing");
    srv.enable_sip_routing(true);
    srv.map_sip_username("sales-team", "/sales");

    std::cout << "Multi-agent server running on http://0.0.0.0:3000\n";
    std::cout << "  /sales, /support, /billing\n";
    srv.run();
}
