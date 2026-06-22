// Tool mixin tests — registration, dispatch, DataMap tools, ordering

#include "signalwire/agent/agent_base.hpp"
#include "signalwire/datamap/datamap.hpp"

using namespace signalwire::agent;
using namespace signalwire::swaig;
using namespace signalwire::datamap;
using json = nlohmann::json;

// ========================================================================
// Tool registration
// ========================================================================

TEST(tool_define_tool_basic) {
    AgentBase agent;
    agent.define_tool("greet", "Say hello", json::object(),
        [](const json&, const json&) -> FunctionResult {
            return FunctionResult("Hello!");
        });
    ASSERT_TRUE(agent.has_tool("greet"));
    return true;
}

TEST(tool_define_tool_with_definition) {
    AgentBase agent;
    ToolDefinition td;
    td.name = "my_tool";
    td.description = "A tool";
    td.parameters = json::object({{"type", "object"}, {"properties", json::object()}});
    td.handler = [](const json&, const json&) { return FunctionResult("ok"); };
    agent.define_tool(td);
    ASSERT_TRUE(agent.has_tool("my_tool"));
    return true;
}

TEST(tool_has_tool_false_for_unknown) {
    AgentBase agent;
    ASSERT_FALSE(agent.has_tool("does_not_exist"));
    return true;
}

TEST(tool_list_tools_empty) {
    AgentBase agent;
    auto tools = agent.list_tools();
    ASSERT_EQ(tools.size(), 0u);
    return true;
}

TEST(tool_list_tools_preserves_order) {
    AgentBase agent;
    agent.define_tool("charlie", "C", json::object(), nullptr);
    agent.define_tool("alpha", "A", json::object(), nullptr);
    agent.define_tool("bravo", "B", json::object(), nullptr);
    auto tools = agent.list_tools();
    ASSERT_EQ(tools.size(), 3u);
    ASSERT_EQ(tools[0], "charlie");
    ASSERT_EQ(tools[1], "alpha");
    ASSERT_EQ(tools[2], "bravo");
    return true;
}

TEST(tool_redefine_does_not_duplicate_order) {
    AgentBase agent;
    agent.define_tool("tool_a", "A", json::object(), nullptr);
    agent.define_tool("tool_b", "B", json::object(), nullptr);
    agent.define_tool("tool_a", "A updated", json::object(), nullptr);
    auto tools = agent.list_tools();
    ASSERT_EQ(tools.size(), 2u);
    ASSERT_EQ(tools[0], "tool_a");
    ASSERT_EQ(tools[1], "tool_b");
    return true;
}

// ========================================================================
// Tool dispatch
// ========================================================================

TEST(tool_dispatch_returns_result) {
    AgentBase agent;
    agent.define_tool("add", "Add numbers", json::object(),
        [](const json& args, const json&) -> FunctionResult {
            int a = args.value("a", 0);
            int b = args.value("b", 0);
            return FunctionResult("Result: " + std::to_string(a + b));
        });
    auto result = agent.on_function_call("add",
        json::object({{"a", 3}, {"b", 4}}), json::object());
    ASSERT_EQ(result.to_json()["response"].get<std::string>(), "Result: 7");
    return true;
}

TEST(tool_dispatch_unknown_function) {
    AgentBase agent;
    auto result = agent.on_function_call("missing", json::object(), json::object());
    auto j = result.to_json();
    ASSERT_TRUE(j["response"].get<std::string>().find("Unknown") != std::string::npos);
    return true;
}

TEST(tool_dispatch_null_handler) {
    AgentBase agent;
    agent.define_tool("null_handler", "No handler", json::object(), nullptr);
    auto result = agent.on_function_call("null_handler", json::object(), json::object());
    auto j = result.to_json();
    ASSERT_TRUE(j["response"].get<std::string>().find("No handler") != std::string::npos);
    return true;
}

TEST(tool_dispatch_with_raw_data) {
    AgentBase agent;
    agent.define_tool("echo_raw", "Echo raw", json::object(),
        [](const json&, const json& raw) -> FunctionResult {
            return FunctionResult("call_id=" + raw.value("call_id", "none"));
        });
    auto result = agent.on_function_call("echo_raw", json::object(),
        json::object({{"call_id", "call-123"}}));
    ASSERT_EQ(result.to_json()["response"].get<std::string>(), "call_id=call-123");
    return true;
}

// ========================================================================
// DataMap registration
// ========================================================================

TEST(tool_register_datamap_function) {
    AgentBase agent;
    agent.set_auth("u", "p");
    auto dm = DataMap("get_weather")
        .purpose("Get weather")
        .parameter("city", "string", "City", true)
        .webhook("GET", "https://api.example.com/weather")
        .output(FunctionResult("Weather: ${response.temp}"))
        .to_swaig_function();
    agent.register_swaig_function(dm);

    json swml = agent.render_swml();
    auto& main = swml["sections"]["main"];
    for (const auto& verb : main) {
        if (verb.contains("ai") && verb["ai"].contains("SWAIG")) {
            auto& funcs = verb["ai"]["SWAIG"]["functions"];
            bool found = false;
            for (const auto& f : funcs) {
                if (f.value("function", "") == "get_weather") {
                    found = true;
                    ASSERT_TRUE(f.contains("data_map"));
                }
            }
            ASSERT_TRUE(found);
            return true;
        }
    }
    ASSERT_TRUE(false);
    return true;
}

// ========================================================================
// SWAIG function rendering in SWML
// ========================================================================

TEST(tool_swaig_functions_in_swml) {
    AgentBase agent;
    agent.set_auth("u", "p");
    agent.define_tool("search", "Search web", json::object({
        {"type", "object"},
        {"properties", json::object({
            {"q", json::object({{"type", "string"}})}
        })}
    }), [](const json&, const json&) { return FunctionResult("ok"); });

    json swml = agent.render_swml();
    auto& main = swml["sections"]["main"];
    for (const auto& verb : main) {
        if (verb.contains("ai") && verb["ai"].contains("SWAIG")) {
            auto& funcs = verb["ai"]["SWAIG"]["functions"];
            ASSERT_EQ(funcs.size(), 1u);
            ASSERT_EQ(funcs[0]["function"].get<std::string>(), "search");
            ASSERT_TRUE(funcs[0].contains("web_hook_url"));
            return true;
        }
    }
    ASSERT_TRUE(false);
    return true;
}

TEST(tool_secure_tool_in_swml) {
    AgentBase agent;
    agent.set_auth("u", "p");
    agent.define_tool("secure_tool", "Secure", json::object(),
        [](const json&, const json&) { return FunctionResult("ok"); },
        true /* secure */);

    json swml = agent.render_swml();
    auto& main = swml["sections"]["main"];
    for (const auto& verb : main) {
        if (verb.contains("ai") && verb["ai"].contains("SWAIG")) {
            auto& funcs = verb["ai"]["SWAIG"]["functions"];
            ASSERT_EQ(funcs.size(), 1u);
            ASSERT_TRUE(funcs[0].contains("secure"));
            ASSERT_EQ(funcs[0]["secure"].get<bool>(), true);
            return true;
        }
    }
    ASSERT_TRUE(false);
    return true;
}

TEST(tool_function_includes) {
    AgentBase agent;
    agent.set_auth("u", "p");
    agent.add_function_include(json::object({
        {"url", "https://example.com/functions.json"},
        {"functions", json::array({"func1", "func2"})}
    }));

    json swml = agent.render_swml();
    auto& main = swml["sections"]["main"];
    for (const auto& verb : main) {
        if (verb.contains("ai") && verb["ai"].contains("SWAIG")) {
            ASSERT_TRUE(verb["ai"]["SWAIG"].contains("includes"));
            ASSERT_EQ(verb["ai"]["SWAIG"]["includes"].size(), 1u);
            return true;
        }
    }
    ASSERT_TRUE(false);
    return true;
}

TEST(tool_set_function_includes_replaces) {
    AgentBase agent;
    agent.set_auth("u", "p");
    // set_function_includes REPLACES the prior add_function_include. Use
    // well-formed entries (non-empty string `url` + array `functions`) so the
    // #191 validity filter keeps them; this test isolates replace-vs-merge.
    agent.add_function_include(
        json::object({{"url", "https://a/swaig"}, {"functions", json::array({"a"})}}));
    agent.set_function_includes({
        json::object({{"url", "https://b/swaig"}, {"functions", json::array({"b"})}}),
        json::object({{"url", "https://c/swaig"}, {"functions", json::array({"c"})}}),
    });

    json swml = agent.render_swml();
    auto& main = swml["sections"]["main"];
    for (const auto& verb : main) {
        if (verb.contains("ai") && verb["ai"].contains("SWAIG")) {
            ASSERT_EQ(verb["ai"]["SWAIG"]["includes"].size(), 2u);
            return true;
        }
    }
    ASSERT_TRUE(false);
    return true;
}
