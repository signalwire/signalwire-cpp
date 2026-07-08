// Tests proving SWML::Service can host SWAIG functions and serve a non-agent
// SWML doc (e.g. ai_sidecar) without subclassing AgentBase. This is the
// contract that lets sidecar / non-agent verbs reuse the SWAIG dispatch
// surface that previously lived only on AgentBase.

#include <chrono>
#include <thread>

#include "httplib.h"
#include "signalwire/swml/service.hpp"
#include "signalwire/swaig/function_result.hpp"

using namespace signalwire::swml;
using namespace signalwire::swaig;
using json = nlohmann::json;

TEST(service_has_swaig_methods) {
    Service svc;
    // These are smoke checks: if the methods didn't exist, this file
    // wouldn't compile at all.
    svc.set_name("svc");
    ASSERT_EQ(svc.name(), "svc");
    ASSERT_EQ(svc.list_tool_names().size(), 0u);
    return true;
}

TEST(service_define_tool_dispatches_via_on_function_call) {
    Service svc;
    bool called = false;
    json captured;
    svc.define_tool(
        "lookup", "Look it up",
        json::object(),
        [&](const json& args, const json&) -> FunctionResult {
            called = true;
            captured = args;
            return FunctionResult("ok");
        });
    auto result = svc.on_function_call("lookup", json::object({{"x", "y"}}), json::object());
    ASSERT_TRUE(called);
    ASSERT_EQ(captured["x"].get<std::string>(), "y");
    auto out = result.to_json();
    ASSERT_EQ(out["response"].get<std::string>(), "ok");
    return true;
}

TEST(service_on_function_call_returns_not_found_response_for_unknown) {
    Service svc;
    auto result = svc.on_function_call("no_such_fn", json::object(), json::object());
    auto out = result.to_json();
    ASSERT_TRUE(out["response"].get<std::string>().find("not found") != std::string::npos);
    return true;
}

TEST(service_list_tool_names_returns_registered_order) {
    Service svc;
    svc.define_tool("first", "f", json::object(), [](const json&, const json&) {
        return FunctionResult();
    });
    svc.define_tool("second", "s", json::object(), [](const json&, const json&) {
        return FunctionResult();
    });
    auto names = svc.list_tool_names();
    ASSERT_EQ(names.size(), 2u);
    ASSERT_EQ(names[0], "first");
    ASSERT_EQ(names[1], "second");
    return true;
}

TEST(service_register_swaig_function_tracks_in_order) {
    Service svc;
    svc.register_swaig_function(json::object({
        {"function", "datamap_tool"},
        {"description", "from data map"},
    }));
    auto names = svc.list_tool_names();
    ASSERT_EQ(names.size(), 1u);
    ASSERT_EQ(names[0], "datamap_tool");
    return true;
}

// -------- Sidecar pattern: non-agent SWML + tool registration --------

TEST(service_sidecar_pattern_emits_verb_and_registers_tool) {
    Service svc;
    svc.set_name("sidecar").set_route("/sidecar");

    // 1. Build the SWML — answer + ai_sidecar verb config.
    svc.answer();
    svc.add_verb("main", "ai_sidecar", json::object({
        {"prompt", "real-time copilot"},
        {"lang", "en-US"},
        {"direction", json::array({"remote-caller", "local-caller"})},
    }));

    auto rendered = svc.render_swml();
    ASSERT_TRUE(rendered.contains("sections"));
    auto& main = rendered["sections"]["main"];
    ASSERT_TRUE(main.is_array());
    bool has_answer = false, has_sidecar = false;
    for (const auto& v : main) {
        if (v.contains("answer")) has_answer = true;
        if (v.contains("ai_sidecar")) has_sidecar = true;
    }
    ASSERT_TRUE(has_answer);
    ASSERT_TRUE(has_sidecar);

    // 2. Register a SWAIG tool the sidecar's LLM can call.
    svc.define_tool(
        "lookup_competitor",
        "Look up competitor pricing.",
        json::object({{"competitor", json::object({{"type", "string"}})}}),
        [](const json& args, const json&) -> FunctionResult {
            return FunctionResult(
                args["competitor"].get<std::string>() + " is $99/seat; we're $79."
            );
        });

    // 3. Dispatch end-to-end through on_function_call.
    auto result = svc.on_function_call(
        "lookup_competitor",
        json::object({{"competitor", "ACME"}}),
        json::object()
    );
    auto out = result.to_json();
    auto resp = out["response"].get<std::string>();
    ASSERT_TRUE(resp.find("ACME") != std::string::npos);
    ASSERT_TRUE(resp.find("$79") != std::string::npos);
    return true;
}

TEST(service_build_tool_registry_json_dumps_runtime_registry) {
    // build_tool_registry_json is the introspect helper the SDK's serve()
    // calls when SWAIG_LIST_TOOLS=1 is set. It must produce
    // {"tools":[<each tool's SWAIG definition>]} in tool_order_, capturing
    // whatever shape define_tool / register_swaig_function actually stored.
    Service svc;
    svc.define_tool(ToolDefinition{
        "lookup_competitor",
        "Look up competitor pricing.",
        json::object({
            {"type", "object"},
            {"properties", json::object({
                {"competitor", json::object({{"type", "string"}})},
            })},
        }),
        [](const json&, const json&) -> FunctionResult {
            return FunctionResult("ok");
        },
        false,
    });
    svc.define_tool(ToolDefinition{
        "get_weather",
        "Get the weather.",
        json::object({{"type", "object"}}),
        [](const json&, const json&) -> FunctionResult { return FunctionResult("sunny"); },
        false,
    });

    auto payload = svc.build_tool_registry_json();
    auto parsed = json::parse(payload);
    ASSERT_TRUE(parsed.contains("tools"));
    ASSERT_TRUE(parsed["tools"].is_array());
    ASSERT_EQ(parsed["tools"].size(), 2u);
    ASSERT_EQ(parsed["tools"][0]["function"].get<std::string>(), "lookup_competitor");
    ASSERT_EQ(parsed["tools"][1]["function"].get<std::string>(), "get_weather");
    ASSERT_EQ(parsed["tools"][0]["description"].get<std::string>(), "Look up competitor pricing.");
    return true;
}

TEST(service_extract_introspect_payload_finds_json_between_sentinels) {
    // The companion extractor used by the swaig-test --example CLI.
    std::string captured =
        "noise\n__SWAIG_TOOLS_BEGIN__\n{\"tools\":[]}\n__SWAIG_TOOLS_END__\nmore noise\n";
    auto payload = Service::extract_introspect_payload(captured);
    ASSERT_EQ(payload, std::string("{\"tools\":[]}"));
    return true;
}

TEST(service_extract_introspect_payload_returns_empty_when_markers_missing) {
    ASSERT_EQ(Service::extract_introspect_payload("no markers anywhere"), std::string());
    ASSERT_EQ(Service::extract_introspect_payload("__SWAIG_TOOLS_BEGIN__\n{}"), std::string());
    return true;
}

// as_router() — the cross-port "embed my routes in a host app" capability.
// Python's WebMixin.as_router / SWMLService.as_router return a mountable
// FastAPI APIRouter; C++ returns a std::shared_ptr<httplib::Server> populated
// with the service's routes but NOT bound to any port, for the caller to embed.
TEST(service_as_router_returns_mountable_unpopulated_server) {
    Service svc;
    svc.set_name("router-svc");
    auto router = svc.as_router();
    // A real, valid, mountable Server (not null, not yet listening).
    ASSERT_TRUE(router != nullptr);
    ASSERT_TRUE(router->is_valid());
    ASSERT_FALSE(router->is_running());
    // Each call hands out a fresh unit the caller owns.
    auto second = svc.as_router();
    ASSERT_NE(router.get(), second.get());
    return true;
}

TEST(service_as_router_registers_the_services_routes) {
    // Prove the returned Server actually carries the service's routes by
    // mounting it on a free ephemeral port and driving it with an httplib
    // client — the same "embed into a host" path a caller uses. Free-port
    // discipline (MOCK_TEST_HARNESS): bind_to_any_port, never a fixed port.
    Service svc;
    svc.set_name("mounted-svc");

    auto router = svc.as_router();
    int port = router->bind_to_any_port("127.0.0.1");
    ASSERT_TRUE(port > 0);

    std::thread server_thread([&router]() { router->listen_after_bind(); });
    // Wait for the server to accept connections.
    for (int i = 0; i < 100 && !router->is_running(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    bool health_ok = false;
    bool swaig_ok = false;
    {
        httplib::Client cli("127.0.0.1", port);
        cli.set_connection_timeout(2, 0);
        // /health is registered by setup_routes and needs no auth.
        auto health = cli.Get("/health");
        health_ok = (health != nullptr && health->status == 200);
        // GET /swaig is registered by setup_routes and returns SWML (200)
        // once the service's basic-auth credentials are supplied.
        auto creds = svc.get_basic_auth_credentials();
        cli.set_basic_auth(creds.first, creds.second);
        auto swaig = cli.Get("/swaig");
        swaig_ok = (swaig != nullptr && swaig->status == 200);
    }

    router->stop();
    server_thread.join();

    ASSERT_TRUE(health_ok);
    ASSERT_TRUE(swaig_ok);
    return true;
}
