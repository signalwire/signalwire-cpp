// Web mixin tests — webhook URLs, proxy detection, query params, dynamic config

#include "signalwire/agent/agent_base.hpp"
#include "signalwire/swml/service.hpp"

using namespace signalwire::agent;
using json = nlohmann::json;

// ========================================================================
// Proxy URL
// ========================================================================

TEST(web_manual_proxy_url) {
    AgentBase agent;
    agent.manual_set_proxy_url("https://proxy.example.com");
    agent.set_auth("u", "p");
    agent.define_tool("test_tool", "Test", json::object(),
        [](const json&, const json&) { return signalwire::swaig::FunctionResult("ok"); });

    // Render WITH a call_id: a per-tool web_hook_url is only emitted when the
    // entry carries a token (or SWAIG query params) — reference
    // agent_base.py:1085-1099. Without one there is no URL to inspect at all.
    const std::map<std::string, std::string> q = {{"call_id", "call-abc"}};
    json swml = agent.render_swml_for_request(q, json::object(), {});
    auto& main = swml["sections"]["main"];
    for (const auto& verb : main) {
        if (verb.contains("ai") && verb["ai"].contains("SWAIG")) {
            auto url = verb["ai"]["SWAIG"]["functions"][0]["web_hook_url"].get<std::string>();
            ASSERT_TRUE(url.find("proxy.example.com") != std::string::npos);
            return true;
        }
    }
    ASSERT_TRUE(false);
    return true;
}

TEST(web_webhook_url_override) {
    AgentBase agent;
    agent.set_webhook_url("https://custom.webhook.com/swaig");
    agent.set_auth("u", "p");
    agent.define_tool("test_tool", "Test", json::object(), nullptr);

    // Render WITH a call_id — see web_manual_proxy_url.
    const std::map<std::string, std::string> q = {{"call_id", "call-abc"}};
    json swml = agent.render_swml_for_request(q, json::object(), {});
    auto& main = swml["sections"]["main"];
    for (const auto& verb : main) {
        if (verb.contains("ai") && verb["ai"].contains("SWAIG")) {
            auto url = verb["ai"]["SWAIG"]["functions"][0]["web_hook_url"].get<std::string>();
            ASSERT_TRUE(url.rfind("https://custom.webhook.com/swaig", 0) == 0);
            return true;
        }
    }
    ASSERT_TRUE(false);
    return true;
}

// ========================================================================
// SWAIG query params
// ========================================================================

TEST(web_swaig_query_params) {
    AgentBase agent;
    agent.set_auth("u", "p");
    agent.add_swaig_query_param("tenant", "acme");
    agent.add_swaig_query_param("mode", "test");
    agent.define_tool("test_tool", "Test", json::object(), nullptr);

    json swml = agent.render_swml();
    auto& main = swml["sections"]["main"];
    for (const auto& verb : main) {
        if (verb.contains("ai") && verb["ai"].contains("SWAIG")) {
            auto url = verb["ai"]["SWAIG"]["functions"][0]["web_hook_url"].get<std::string>();
            ASSERT_TRUE(url.find("tenant=acme") != std::string::npos);
            ASSERT_TRUE(url.find("mode=test") != std::string::npos);
            return true;
        }
    }
    ASSERT_TRUE(false);
    return true;
}

TEST(web_clear_swaig_query_params) {
    AgentBase agent;
    agent.set_auth("u", "p");
    agent.add_swaig_query_param("key", "val");
    agent.clear_swaig_query_params();
    agent.define_tool("test_tool", "Test", json::object(), nullptr);

    // Render WITH a call_id — with the params cleared, the token is now the only
    // thing that earns this entry its own web_hook_url (see web_manual_proxy_url).
    const std::map<std::string, std::string> q = {{"call_id", "call-abc"}};
    json swml = agent.render_swml_for_request(q, json::object(), {});
    auto& main = swml["sections"]["main"];
    for (const auto& verb : main) {
        if (verb.contains("ai") && verb["ai"].contains("SWAIG")) {
            auto url = verb["ai"]["SWAIG"]["functions"][0]["web_hook_url"].get<std::string>();
            ASSERT_TRUE(url.find("key=val") == std::string::npos);
            return true;
        }
    }
    ASSERT_TRUE(false);
    return true;
}

// ========================================================================
// Dynamic config
// ========================================================================

TEST(web_dynamic_config_modifies_copy) {
    AgentBase agent;
    agent.set_auth("u", "p");
    agent.set_prompt_text("Original prompt");

    agent.set_dynamic_config_callback(
        [](const std::map<std::string, std::string>& qp,
           const json&,
           const std::map<std::string, std::string>&,
           AgentBase& copy) {
            auto it = qp.find("tenant");
            if (it != qp.end()) {
                copy.set_prompt_text("Tenant: " + it->second);
            }
        });

    std::map<std::string, std::string> qp = {{"tenant", "acme"}};
    json swml = agent.render_swml_for_request(qp, json::object(), {});

    // Original agent should not change
    ASSERT_EQ(agent.get_prompt(), "Original prompt");

    // Rendered SWML should have the modified prompt
    auto& main = swml["sections"]["main"];
    for (const auto& verb : main) {
        if (verb.contains("ai") && verb["ai"].contains("prompt")) {
            auto text = verb["ai"]["prompt"]["text"].get<std::string>();
            ASSERT_EQ(text, "Tenant: acme");
            return true;
        }
    }
    ASSERT_TRUE(false);
    return true;
}

TEST(web_dynamic_config_without_callback) {
    AgentBase agent;
    agent.set_prompt_text("Static");
    json swml = agent.render_swml_for_request({}, json::object(), {});
    auto& main = swml["sections"]["main"];
    for (const auto& verb : main) {
        if (verb.contains("ai") && verb["ai"].contains("prompt")) {
            ASSERT_EQ(verb["ai"]["prompt"]["text"].get<std::string>(), "Static");
            return true;
        }
    }
    ASSERT_TRUE(false);
    return true;
}

// ========================================================================
// Proxy detection from headers
// ========================================================================

TEST(web_proxy_from_forwarded_headers) {
    AgentBase agent;
    agent.set_auth("u", "p");
    agent.define_tool("test_tool", "Test", json::object(), nullptr);

    std::map<std::string, std::string> headers = {
        {"x-forwarded-proto", "https"},
        {"x-forwarded-host", "myapp.example.com"}
    };
    // Render WITH a call_id — see web_manual_proxy_url.
    const std::map<std::string, std::string> q = {{"call_id", "call-abc"}};
    json swml = agent.render_swml_for_request(q, json::object(), headers);

    auto& main = swml["sections"]["main"];
    for (const auto& verb : main) {
        if (verb.contains("ai") && verb["ai"].contains("SWAIG")) {
            auto url = verb["ai"]["SWAIG"]["functions"][0]["web_hook_url"].get<std::string>();
            ASSERT_TRUE(url.find("myapp.example.com") != std::string::npos);
            ASSERT_TRUE(url.find("https://") != std::string::npos);
            return true;
        }
    }
    ASSERT_TRUE(false);
    return true;
}

// ========================================================================
// Post-prompt URL
// ========================================================================

TEST(web_post_prompt_url_auto_generated) {
    AgentBase agent;
    agent.set_auth("u", "p");
    json swml = agent.render_swml();
    auto& main = swml["sections"]["main"];
    for (const auto& verb : main) {
        if (verb.contains("ai") && verb["ai"].contains("post_prompt_url")) {
            auto url = verb["ai"]["post_prompt_url"].get<std::string>();
            ASSERT_TRUE(url.find("/post_prompt") != std::string::npos);
            return true;
        }
    }
    ASSERT_TRUE(false);
    return true;
}

TEST(web_post_prompt_url_direct_override) {
    AgentBase agent;
    agent.set_post_prompt_url_direct("https://custom.com/post_prompt");
    json swml = agent.render_swml();
    auto& main = swml["sections"]["main"];
    for (const auto& verb : main) {
        if (verb.contains("ai") && verb["ai"].contains("post_prompt_url")) {
            ASSERT_EQ(verb["ai"]["post_prompt_url"].get<std::string>(),
                       "https://custom.com/post_prompt");
            return true;
        }
    }
    ASSERT_TRUE(false);
    return true;
}

// ========================================================================
// Debug routes
// ========================================================================

TEST(web_enable_debug_routes) {
    AgentBase agent;
    agent.enable_debug_routes(true);
    // Just verify it doesn't crash; actual route testing requires server
    return true;
}

// ========================================================================
// SIP routing
// ========================================================================

TEST(web_sip_routing_enable) {
    AgentBase agent;
    agent.enable_sip_routing(true);
    // No crash
    return true;
}

TEST(web_sip_register_valid_username) {
    AgentBase agent;
    agent.enable_sip_routing(true);
    agent.register_sip_username("alice");
    agent.register_sip_username("bob_123");
    // No crash; valid usernames accepted
    return true;
}

TEST(web_auto_map_sip_usernames) {
    AgentBase agent;
    agent.auto_map_sip_usernames(true);
    // No crash
    return true;
}

// ========================================================================
// WebMixin parity: on_request / on_swml_request
//
// Python parity:
//   tests/unit/core/mixins/test_web_mixin.py::
//     test_on_request_delegates_to_on_swml_request
//     test_on_swml_request_called
// ========================================================================

namespace {
class CustomSwmlService : public signalwire::swml::Service {
public:
    json last_request_data;
    std::string last_callback_path;
    std::optional<json> custom_return;

    std::optional<json> on_swml_request(
        const std::optional<json>& request_data,
        const std::optional<std::string>& callback_path) override {
        last_request_data = request_data.value_or(json{});
        last_callback_path = callback_path.value_or(std::string{});
        return custom_return;
    }
};
}

TEST(web_on_request_delegates_to_on_swml_request) {
    CustomSwmlService svc;
    svc.custom_return = json{{"custom", true}};

    json rd{{"data", "val"}};
    auto result = svc.on_request(rd, std::string{"/cb"});

    ASSERT_EQ(svc.last_request_data, rd);
    ASSERT_EQ(svc.last_callback_path, std::string{"/cb"});
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ((*result)["custom"].get<bool>(), true);
    return true;
}

TEST(web_on_swml_request_default_returns_nullopt) {
    signalwire::swml::Service svc;
    auto result = svc.on_swml_request(std::nullopt, std::nullopt);
    ASSERT_FALSE(result.has_value());
    return true;
}

TEST(web_on_request_default_returns_nullopt) {
    signalwire::swml::Service svc;
    auto result = svc.on_request(std::nullopt, std::nullopt);
    ASSERT_FALSE(result.has_value());
    return true;
}

TEST(web_on_request_passes_nulls_to_hook) {
    CustomSwmlService svc;
    svc.custom_return = std::nullopt;
    auto result = svc.on_request(std::nullopt, std::nullopt);
    ASSERT_FALSE(result.has_value());
    ASSERT_EQ(svc.last_request_data, json{});
    ASSERT_EQ(svc.last_callback_path, std::string{});
    return true;
}
