// AgentServer tests

#include "signalwire/server/agent_server.hpp"
#include "signalwire/agent/agent_base.hpp"

using namespace signalwire::server;
using namespace signalwire::agent;
using json = nlohmann::json;

TEST(server_creation) {
    AgentServer server("0.0.0.0", 3001);
    auto routes = server.list_routes();
    ASSERT_EQ(routes.size(), 0u);
    return true;
}

// Reference parity: AgentServer.__init__ stores host / port / log_level as
// public instance attributes (`self.host = host`, …), and `run()` reads them
// back. A caller who hands them in must be able to read them back.
TEST(server_construction_params_readable) {
    AgentServer server("127.0.0.1", 3099, "debug");
    ASSERT_EQ(server.host(), "127.0.0.1");
    ASSERT_EQ(server.port(), 3099);
    ASSERT_EQ(server.log_level(), "debug");
    return true;
}

// The reference lowercases log_level (`log_level.lower()`) and defaults it to
// "info"; host/port default to 0.0.0.0/3000.
TEST(server_construction_params_defaults_and_lowercasing) {
    AgentServer defaults;
    ASSERT_EQ(defaults.host(), "0.0.0.0");
    ASSERT_EQ(defaults.log_level(), "info");
    AgentServer shouty("0.0.0.0", 3000, "WARNING");
    ASSERT_EQ(shouty.log_level(), "warning");
    return true;
}

TEST(server_register_agent) {
    AgentServer server;
    auto agent = std::make_shared<AgentBase>("agent1", "/agent1");
    agent->set_auth("u", "p");
    server.register_agent(agent, "/agent1");
    auto routes = server.list_routes();
    ASSERT_EQ(routes.size(), 1u);
    ASSERT_EQ(routes[0], "/agent1");
    return true;
}

TEST(server_register_multiple_agents) {
    AgentServer server;
    auto a1 = std::make_shared<AgentBase>("a1", "/a1");
    auto a2 = std::make_shared<AgentBase>("a2", "/a2");
    a1->set_auth("u1", "p1");
    a2->set_auth("u2", "p2");
    server.register_agent(a1, "/a1");
    server.register_agent(a2, "/a2");
    auto routes = server.list_routes();
    ASSERT_EQ(routes.size(), 2u);
    return true;
}

TEST(server_unregister_agent) {
    AgentServer server;
    auto agent = std::make_shared<AgentBase>("agent1", "/agent1");
    agent->set_auth("u", "p");
    server.register_agent(agent, "/agent1");
    server.unregister_agent("/agent1");
    auto routes = server.list_routes();
    ASSERT_EQ(routes.size(), 0u);
    return true;
}

TEST(server_sip_routing) {
    AgentServer server;
    server.enable_sip_routing();
    server.map_sip_username("alice", "/alice-agent");
    server.map_sip_username("bob", "/bob-agent");
    // Valid setup, no crash
    return true;
}

TEST(server_sip_invalid_username) {
    AgentServer server;
    server.enable_sip_routing();
    // Invalid SIP username with special chars should be rejected silently
    server.map_sip_username("user@invalid", "/agent");
    // Should not crash
    return true;
}

TEST(server_route_normalization) {
    AgentServer server;
    auto agent = std::make_shared<AgentBase>("agent1");
    agent->set_auth("u", "p");
    server.register_agent(agent, "no-slash");
    auto routes = server.list_routes();
    ASSERT_EQ(routes.size(), 1u);
    ASSERT_EQ(routes[0], "/no-slash"); // Should have prepended slash
    return true;
}
