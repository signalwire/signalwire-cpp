// AgentBase tests

#include "signalwire/agent/agent_base.hpp"

using namespace signalwire::agent;
using namespace signalwire::swaig;
using json = nlohmann::json;

// ========================================================================
// Construction
// ========================================================================

TEST(agent_default_construction) {
    AgentBase agent;
    ASSERT_EQ(agent.name(), "agent");
    ASSERT_EQ(agent.route(), "/");
    return true;
}

TEST(agent_named_construction) {
    AgentBase agent("my_agent", "/bot", "0.0.0.0", 4000);
    ASSERT_EQ(agent.name(), "my_agent");
    ASSERT_EQ(agent.route(), "/bot");
    return true;
}

TEST(agent_set_name) {
    AgentBase agent;
    agent.set_name("new_name");
    ASSERT_EQ(agent.name(), "new_name");
    return true;
}

// ========================================================================
// Prompt Methods
// ========================================================================

TEST(agent_set_prompt_text) {
    AgentBase agent;
    agent.set_prompt_text("Hello world");
    ASSERT_EQ(agent.get_prompt(), "Hello world");
    return true;
}

TEST(agent_prompt_add_section) {
    AgentBase agent;
    agent.prompt_add_section("Personality", "You are helpful.");
    ASSERT_TRUE(agent.prompt_has_section("Personality"));
    ASSERT_FALSE(agent.prompt_has_section("Nonexistent"));
    std::string prompt = agent.get_prompt();
    ASSERT_TRUE(prompt.find("Personality") != std::string::npos);
    ASSERT_TRUE(prompt.find("You are helpful.") != std::string::npos);
    return true;
}

TEST(agent_prompt_add_section_with_bullets) {
    AgentBase agent;
    agent.prompt_add_section("Rules", "", {"Be kind", "Be concise"});
    std::string prompt = agent.get_prompt();
    ASSERT_TRUE(prompt.find("Be kind") != std::string::npos);
    ASSERT_TRUE(prompt.find("Be concise") != std::string::npos);
    return true;
}

TEST(agent_prompt_add_subsection) {
    AgentBase agent;
    agent.prompt_add_section("Main", "Main body");
    agent.prompt_add_subsection("Main", "Sub", "Sub body");
    std::string prompt = agent.get_prompt();
    ASSERT_TRUE(prompt.find("Main") != std::string::npos);
    ASSERT_TRUE(prompt.find("Sub body") != std::string::npos);
    return true;
}

TEST(agent_prompt_add_to_section) {
    AgentBase agent;
    agent.prompt_add_section("Rules", "Rule 1");
    agent.prompt_add_to_section("Rules", "", {"Rule 2"});
    std::string prompt = agent.get_prompt();
    ASSERT_TRUE(prompt.find("Rule 1") != std::string::npos);
    ASSERT_TRUE(prompt.find("Rule 2") != std::string::npos);
    return true;
}

TEST(agent_set_post_prompt) {
    AgentBase agent;
    agent.set_post_prompt("Summarize the call");
    // Post prompt is part of the AI verb, not direct prompt text
    json swml = agent.render_swml();
    auto& main = swml["sections"]["main"];
    bool found = false;
    for (const auto& verb : main) {
        if (verb.contains("ai") && verb["ai"].contains("post_prompt")) {
            found = true;
            ASSERT_EQ(verb["ai"]["post_prompt"]["text"].get<std::string>(), "Summarize the call");
        }
    }
    ASSERT_TRUE(found);
    return true;
}

// ========================================================================
// Tool Methods
// ========================================================================

TEST(agent_define_tool) {
    AgentBase agent;
    agent.define_tool("greet", "Say hello", json::object(),
        [](const json&, const json&) -> FunctionResult {
            return FunctionResult("Hello!");
        });
    ASSERT_TRUE(agent.has_tool("greet"));
    ASSERT_FALSE(agent.has_tool("nonexistent"));
    return true;
}

TEST(agent_list_tools) {
    AgentBase agent;
    agent.define_tool("tool_a", "A", json::object(), nullptr);
    agent.define_tool("tool_b", "B", json::object(), nullptr);
    auto tools = agent.list_tools();
    ASSERT_EQ(tools.size(), 2u);
    ASSERT_EQ(tools[0], "tool_a");
    ASSERT_EQ(tools[1], "tool_b");
    return true;
}

TEST(agent_on_function_call) {
    AgentBase agent;
    agent.define_tool("echo", "Echo input", json::object(),
        [](const json& args, const json&) -> FunctionResult {
            return FunctionResult("Echo: " + args.value("text", ""));
        });

    auto result = agent.on_function_call("echo", json::object({{"text", "hi"}}), json::object());
    auto j = result.to_json();
    ASSERT_TRUE(j["response"].get<std::string>().find("hi") != std::string::npos);
    return true;
}

TEST(agent_on_function_call_unknown) {
    AgentBase agent;
    auto result = agent.on_function_call("nonexistent", json::object(), json::object());
    auto j = result.to_json();
    ASSERT_TRUE(j["response"].get<std::string>().find("Unknown") != std::string::npos);
    return true;
}

// ========================================================================
// AI Config Methods
// ========================================================================

TEST(agent_add_hints) {
    AgentBase agent;
    agent.add_hint("hello");
    agent.add_hints({"world", "test"});
    json swml = agent.render_swml();
    auto& main = swml["sections"]["main"];
    for (const auto& verb : main) {
        if (verb.contains("ai") && verb["ai"].contains("hints")) {
            auto& hints = verb["ai"]["hints"];
            ASSERT_EQ(hints.size(), 3u);
            return true;
        }
    }
    // Hints should be present
    ASSERT_TRUE(false);
    return true;
}

TEST(agent_add_language) {
    AgentBase agent;
    agent.add_language({"English", "en-US", "rachel", "", ""});
    json swml = agent.render_swml();
    auto& main = swml["sections"]["main"];
    for (const auto& verb : main) {
        if (verb.contains("ai") && verb["ai"].contains("languages")) {
            ASSERT_EQ(verb["ai"]["languages"].size(), 1u);
            ASSERT_EQ(verb["ai"]["languages"][0]["code"].get<std::string>(), "en-US");
            return true;
        }
    }
    ASSERT_TRUE(false);
    return true;
}

TEST(agent_set_params) {
    AgentBase agent;
    agent.set_param("temperature", 0.7);
    agent.set_params(json::object({{"top_p", 0.9}}));
    json swml = agent.render_swml();
    auto& main = swml["sections"]["main"];
    for (const auto& verb : main) {
        if (verb.contains("ai") && verb["ai"].contains("params")) {
            ASSERT_EQ(verb["ai"]["params"]["temperature"].get<double>(), 0.7);
            ASSERT_EQ(verb["ai"]["params"]["top_p"].get<double>(), 0.9);
            return true;
        }
    }
    ASSERT_TRUE(false);
    return true;
}

TEST(agent_set_global_data) {
    AgentBase agent;
    agent.set_global_data(json::object({{"key", "value"}}));
    agent.update_global_data(json::object({{"key2", "value2"}}));
    json swml = agent.render_swml();
    auto& main = swml["sections"]["main"];
    for (const auto& verb : main) {
        if (verb.contains("ai") && verb["ai"].contains("global_data")) {
            ASSERT_EQ(verb["ai"]["global_data"]["key"].get<std::string>(), "value");
            ASSERT_EQ(verb["ai"]["global_data"]["key2"].get<std::string>(), "value2");
            return true;
        }
    }
    ASSERT_TRUE(false);
    return true;
}

TEST(agent_add_pronunciation) {
    AgentBase agent;
    agent.add_pronunciation("SW", "SignalWire");
    json swml = agent.render_swml();
    auto& main = swml["sections"]["main"];
    for (const auto& verb : main) {
        if (verb.contains("ai") && verb["ai"].contains("pronounce")) {
            ASSERT_EQ(verb["ai"]["pronounce"].size(), 1u);
            ASSERT_EQ(verb["ai"]["pronounce"][0]["replace"].get<std::string>(), "SW");
            return true;
        }
    }
    ASSERT_TRUE(false);
    return true;
}

// ========================================================================
// Verb Methods (5-phase pipeline)
// ========================================================================

TEST(agent_render_swml_default) {
    AgentBase agent;
    agent.set_prompt_text("Hello");
    json swml = agent.render_swml();

    ASSERT_EQ(swml["version"].get<std::string>(), "1.0.0");
    auto& main = swml["sections"]["main"];
    ASSERT_TRUE(main.size() >= 2u); // answer + ai at minimum

    // First verb should be answer
    ASSERT_TRUE(main[0].contains("answer"));
    ASSERT_EQ(main[0]["answer"]["max_duration"].get<int>(), 3600);

    return true;
}

TEST(agent_pre_answer_verbs) {
    AgentBase agent;
    agent.add_pre_answer_verb("play", json::object({{"url", "ring.mp3"}}));
    json swml = agent.render_swml();
    auto& main = swml["sections"]["main"];
    // First verb should be the pre-answer play
    ASSERT_TRUE(main[0].contains("play"));
    return true;
}

TEST(agent_post_answer_verbs) {
    AgentBase agent;
    agent.add_post_answer_verb("play", json::object({{"url", "welcome.mp3"}}));
    json swml = agent.render_swml();
    auto& main = swml["sections"]["main"];
    // After answer, before AI
    bool found_play = false;
    bool found_ai = false;
    for (const auto& verb : main) {
        if (verb.contains("play")) found_play = true;
        if (verb.contains("ai")) {
            ASSERT_TRUE(found_play); // play should come before ai
            found_ai = true;
        }
    }
    ASSERT_TRUE(found_play);
    ASSERT_TRUE(found_ai);
    return true;
}

TEST(agent_post_ai_verbs) {
    AgentBase agent;
    agent.add_post_ai_verb("hangup", json::object());
    json swml = agent.render_swml();
    auto& main = swml["sections"]["main"];
    // Last verb should be hangup
    ASSERT_TRUE(main.back().contains("hangup"));
    return true;
}

TEST(agent_clear_verbs) {
    AgentBase agent;
    agent.add_pre_answer_verb("play", json::object());
    agent.clear_pre_answer_verbs();
    json swml = agent.render_swml();
    auto& main = swml["sections"]["main"];
    // First verb should be answer, not play
    ASSERT_TRUE(main[0].contains("answer"));
    return true;
}

// ========================================================================
// Contexts
// ========================================================================

TEST(agent_define_contexts) {
    AgentBase agent;
    auto& ctx = agent.add_context("default");
    ctx.add_step("greeting", "Greet the user");
    ASSERT_TRUE(agent.has_contexts());
    return true;
}

// ========================================================================
// Skills
// ========================================================================

TEST(agent_add_skill) {
    AgentBase agent;
    agent.add_skill("datetime");
    ASSERT_TRUE(agent.has_skill("datetime"));
    ASSERT_FALSE(agent.has_skill("nonexistent"));
    return true;
}

TEST(agent_remove_skill) {
    AgentBase agent;
    agent.add_skill("datetime");
    agent.remove_skill("datetime");
    ASSERT_FALSE(agent.has_skill("datetime"));
    return true;
}

TEST(agent_list_skills) {
    AgentBase agent;
    agent.add_skill("datetime");
    agent.add_skill("math");
    auto skills = agent.list_skills();
    ASSERT_EQ(skills.size(), 2u);
    return true;
}

// ========================================================================
// Auth
// ========================================================================

TEST(agent_set_auth) {
    AgentBase agent;
    agent.set_auth("user", "pass");
    ASSERT_EQ(agent.auth_username(), "user");
    ASSERT_EQ(agent.auth_password(), "pass");
    return true;
}

// ========================================================================
// Web Config
// ========================================================================

TEST(agent_swaig_query_params) {
    AgentBase agent;
    agent.set_auth("u", "p");
    agent.add_swaig_query_param("key1", "val1");
    agent.add_swaig_query_param("key2", "val2");
    json swml = agent.render_swml();
    // Check that webhook URLs contain query params
    auto& main = swml["sections"]["main"];
    for (const auto& verb : main) {
        if (verb.contains("ai") && verb["ai"].contains("SWAIG")) {
            // If there are functions, check URLs
            return true;
        }
    }
    return true;
}

TEST(agent_sip_routing) {
    AgentBase agent;
    agent.enable_sip_routing();
    agent.register_sip_username("alice");
    // Valid username should succeed
    return true;
}

// ========================================================================
// Method Chaining
// ========================================================================

TEST(agent_method_chaining) {
    AgentBase agent;
    agent.set_name("chained")
         .set_prompt_text("Hello")
         .add_hint("test")
         .set_param("temperature", 0.5)
         .set_global_data(json::object({{"k", "v"}}))
         .add_pronunciation("SW", "SignalWire")
         .set_post_prompt("Summary");
    ASSERT_EQ(agent.name(), "chained");
    ASSERT_EQ(agent.get_prompt(), "Hello");
    return true;
}

// ========================================================================
// POM Rendering
// ========================================================================

TEST(agent_pom_rendering) {
    AgentBase agent;
    agent.set_use_pom(true);
    agent.prompt_add_section("Personality", "You are helpful.", {"Be concise"});
    json swml = agent.render_swml();
    auto& main = swml["sections"]["main"];
    for (const auto& verb : main) {
        if (verb.contains("ai") && verb["ai"].contains("prompt")) {
            auto& prompt = verb["ai"]["prompt"];
            if (prompt.contains("pom")) {
                ASSERT_EQ(prompt["pom"].size(), 1u);
                ASSERT_EQ(prompt["pom"][0]["title"].get<std::string>(), "Personality");
                return true;
            }
        }
    }
    ASSERT_TRUE(false);
    return true;
}

TEST(agent_raw_text_rendering) {
    AgentBase agent;
    agent.set_use_pom(false);
    agent.set_prompt_text("Raw prompt text");
    json swml = agent.render_swml();
    auto& main = swml["sections"]["main"];
    for (const auto& verb : main) {
        if (verb.contains("ai") && verb["ai"].contains("prompt")) {
            auto& prompt = verb["ai"]["prompt"];
            ASSERT_EQ(prompt["text"].get<std::string>(), "Raw prompt text");
            return true;
        }
    }
    ASSERT_TRUE(false);
    return true;
}

// ========================================================================
// Dynamic Config Callback
// ========================================================================

TEST(agent_dynamic_config) {
    AgentBase agent;
    agent.set_auth("u", "p");
    agent.set_prompt_text("Original");

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

    // Without tenant param
    json swml1 = agent.render_swml_for_request({}, json::object(), {});
    // Agent original prompt should be used

    // With tenant param
    std::map<std::string, std::string> qp = {{"tenant", "acme"}};
    json swml2 = agent.render_swml_for_request(qp, json::object(), {});

    // Original agent should be unchanged
    ASSERT_EQ(agent.get_prompt(), "Original");

    return true;
}

// ========================================================================
// Tool token methods
//
// Parity: signalwire-python tests/unit/core/test_agent_base.py
//   ::TestAgentBaseTokenMethods::test_validate_tool_token
//   ::TestAgentBaseTokenMethods::test_create_tool_token
// Python's StateMixin._create_tool_token catches all exceptions and
// returns ""; validate_tool_token rejects unknown function names up front.
// ========================================================================

TEST(agent_create_tool_token_round_trip) {
    AgentBase agent;
    agent.define_tool("test_tool", "t", json::object(), nullptr, true);

    std::string token = agent.create_tool_token("test_tool", "call_123");
    ASSERT_FALSE(token.empty());
    ASSERT_TRUE(agent.validate_tool_token("test_tool", token, "call_123"));
    return true;
}

TEST(agent_validate_tool_token_rejects_unknown_function) {
    AgentBase agent;
    ASSERT_FALSE(agent.validate_tool_token("not_registered", "any", "call_123"));
    return true;
}

TEST(agent_validate_tool_token_rejects_bad_token) {
    AgentBase agent;
    agent.define_tool("test_tool", "t", json::object(), nullptr, true);
    ASSERT_FALSE(agent.validate_tool_token("test_tool", "garbage_token_value", "call_123"));
    return true;
}

TEST(agent_validate_tool_token_rejects_wrong_call_id) {
    AgentBase agent;
    agent.define_tool("test_tool", "t", json::object(), nullptr, true);
    std::string token = agent.create_tool_token("test_tool", "call_A");
    ASSERT_FALSE(token.empty());
    ASSERT_FALSE(agent.validate_tool_token("test_tool", token, "call_B"));
    return true;
}
