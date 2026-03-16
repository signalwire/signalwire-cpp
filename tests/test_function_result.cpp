// SwaigFunctionResult tests

#include "signalwire/swaig/function_result.hpp"
#include "signalwire/swaig/tool_definition.hpp"

using namespace signalwire::swaig;
using json = nlohmann::json;

// ========================================================================
// Core
// ========================================================================

TEST(function_result_default) {
    FunctionResult r;
    auto j = r.to_json();
    ASSERT_TRUE(j.contains("response"));
    ASSERT_FALSE(j.contains("action"));
    ASSERT_FALSE(j.contains("post_process"));
    return true;
}

TEST(function_result_with_response) {
    FunctionResult r("Hello world");
    auto j = r.to_json();
    ASSERT_EQ(j["response"].get<std::string>(), "Hello world");
    return true;
}

TEST(function_result_empty_gives_default_response) {
    FunctionResult r;
    auto j = r.to_json();
    ASSERT_EQ(j["response"].get<std::string>(), "Action completed.");
    return true;
}

TEST(function_result_set_response) {
    FunctionResult r;
    r.set_response("Updated");
    auto j = r.to_json();
    ASSERT_EQ(j["response"].get<std::string>(), "Updated");
    return true;
}

TEST(function_result_post_process) {
    FunctionResult r("test", true);
    r.add_action("test", json(true));
    auto j = r.to_json();
    ASSERT_TRUE(j.contains("post_process"));
    ASSERT_EQ(j["post_process"].get<bool>(), true);
    return true;
}

TEST(function_result_post_process_without_actions) {
    // post_process only included when there are actions
    FunctionResult r("test", true);
    auto j = r.to_json();
    ASSERT_FALSE(j.contains("post_process"));
    return true;
}

TEST(function_result_add_action) {
    FunctionResult r("test");
    r.add_action("say", json("hello"));
    auto j = r.to_json();
    ASSERT_TRUE(j.contains("action"));
    ASSERT_EQ(j["action"].size(), 1u);
    ASSERT_TRUE(j["action"][0].contains("say"));
    return true;
}

TEST(function_result_add_actions) {
    FunctionResult r("test");
    r.add_actions({
        json::object({{"say", "hello"}}),
        json::object({{"stop", true}})
    });
    auto j = r.to_json();
    ASSERT_EQ(j["action"].size(), 2u);
    return true;
}

TEST(function_result_chaining) {
    auto j = FunctionResult("chained")
        .add_action("say", json("hi"))
        .hangup()
        .to_json();
    ASSERT_EQ(j["response"].get<std::string>(), "chained");
    ASSERT_EQ(j["action"].size(), 2u);
    return true;
}

// ========================================================================
// Call Control
// ========================================================================

TEST(function_result_connect) {
    FunctionResult r("Transferring");
    r.connect("+15551234567");
    auto j = r.to_json();
    auto& action = j["action"][0];
    ASSERT_TRUE(action.contains("SWML"));
    ASSERT_TRUE(action.contains("transfer"));
    ASSERT_EQ(action["transfer"].get<std::string>(), "true");
    return true;
}

TEST(function_result_connect_with_from) {
    FunctionResult r("Transferring");
    r.connect("+15551234567", false, "+15559876543");
    auto j = r.to_json();
    auto& action = j["action"][0];
    ASSERT_EQ(action["transfer"].get<std::string>(), "false");
    auto& swml = action["SWML"];
    auto& main = swml["sections"]["main"];
    ASSERT_TRUE(main[0]["connect"].contains("from"));
    return true;
}

TEST(function_result_swml_transfer) {
    FunctionResult r("Transferring");
    r.swml_transfer("https://example.com/swml", "Goodbye!", true);
    auto j = r.to_json();
    auto& action = j["action"][0];
    ASSERT_TRUE(action.contains("SWML"));
    ASSERT_EQ(action["transfer"].get<std::string>(), "true");
    return true;
}

TEST(function_result_hangup) {
    FunctionResult r("Bye");
    r.hangup();
    auto j = r.to_json();
    ASSERT_EQ(j["action"][0]["hangup"].get<bool>(), true);
    return true;
}

TEST(function_result_hold) {
    FunctionResult r("Hold please");
    r.hold(120);
    auto j = r.to_json();
    ASSERT_EQ(j["action"][0]["hold"].get<int>(), 120);
    return true;
}

TEST(function_result_hold_clamp_max) {
    FunctionResult r("Hold");
    r.hold(1500);
    auto j = r.to_json();
    ASSERT_EQ(j["action"][0]["hold"].get<int>(), 900);
    return true;
}

TEST(function_result_hold_clamp_min) {
    FunctionResult r("Hold");
    r.hold(-10);
    auto j = r.to_json();
    ASSERT_EQ(j["action"][0]["hold"].get<int>(), 0);
    return true;
}

TEST(function_result_wait_for_user_default) {
    FunctionResult r("Wait");
    r.wait_for_user();
    auto j = r.to_json();
    ASSERT_EQ(j["action"][0]["wait_for_user"].get<bool>(), true);
    return true;
}

TEST(function_result_wait_for_user_answer_first) {
    FunctionResult r("Wait");
    r.wait_for_user(std::nullopt, std::nullopt, true);
    auto j = r.to_json();
    ASSERT_EQ(j["action"][0]["wait_for_user"].get<std::string>(), "answer_first");
    return true;
}

TEST(function_result_wait_for_user_timeout) {
    FunctionResult r("Wait");
    r.wait_for_user(std::nullopt, 30);
    auto j = r.to_json();
    ASSERT_EQ(j["action"][0]["wait_for_user"].get<int>(), 30);
    return true;
}

TEST(function_result_stop) {
    FunctionResult r("Stopping");
    r.stop();
    auto j = r.to_json();
    ASSERT_EQ(j["action"][0]["stop"].get<bool>(), true);
    return true;
}

// ========================================================================
// State & Data
// ========================================================================

TEST(function_result_update_global_data) {
    FunctionResult r("Updated");
    r.update_global_data({{"key", "value"}});
    auto j = r.to_json();
    ASSERT_TRUE(j["action"][0].contains("set_global_data"));
    ASSERT_EQ(j["action"][0]["set_global_data"]["key"].get<std::string>(), "value");
    return true;
}

TEST(function_result_remove_global_data) {
    FunctionResult r("Removed");
    r.remove_global_data(json::array({"key1", "key2"}));
    auto j = r.to_json();
    ASSERT_TRUE(j["action"][0].contains("unset_global_data"));
    return true;
}

TEST(function_result_set_metadata) {
    FunctionResult r("Meta");
    r.set_metadata({{"session", "abc"}});
    auto j = r.to_json();
    ASSERT_TRUE(j["action"][0].contains("set_meta_data"));
    return true;
}

TEST(function_result_remove_metadata) {
    FunctionResult r("Meta");
    r.remove_metadata(json::array({"key1"}));
    auto j = r.to_json();
    ASSERT_TRUE(j["action"][0].contains("unset_meta_data"));
    return true;
}

TEST(function_result_swml_user_event) {
    FunctionResult r("Event");
    r.swml_user_event({{"type", "cards_dealt"}, {"score", 21}});
    auto j = r.to_json();
    ASSERT_TRUE(j["action"][0].contains("SWML"));
    return true;
}

TEST(function_result_change_step) {
    FunctionResult r("Step change");
    r.swml_change_step("betting");
    auto j = r.to_json();
    ASSERT_TRUE(j["action"][0].contains("change_step"));
    ASSERT_EQ(j["action"][0]["change_step"].get<std::string>(), "betting");
    return true;
}

TEST(function_result_change_context) {
    FunctionResult r("Context change");
    r.swml_change_context("support");
    auto j = r.to_json();
    ASSERT_TRUE(j["action"][0].contains("change_context"));
    ASSERT_EQ(j["action"][0]["change_context"].get<std::string>(), "support");
    return true;
}

TEST(function_result_switch_context_simple) {
    FunctionResult r("Switch");
    r.switch_context("You are now a sales agent.");
    auto j = r.to_json();
    ASSERT_TRUE(j["action"][0].contains("context_switch"));
    ASSERT_EQ(j["action"][0]["context_switch"].get<std::string>(), "You are now a sales agent.");
    return true;
}

TEST(function_result_switch_context_advanced) {
    FunctionResult r("Switch");
    r.switch_context("system prompt", "user prompt", true, false);
    auto j = r.to_json();
    auto& cs = j["action"][0]["context_switch"];
    ASSERT_EQ(cs["system_prompt"].get<std::string>(), "system prompt");
    ASSERT_EQ(cs["user_prompt"].get<std::string>(), "user prompt");
    ASSERT_EQ(cs["consolidate"].get<bool>(), true);
    return true;
}

TEST(function_result_replace_in_history) {
    FunctionResult r("Replace");
    r.replace_in_history(json(true));
    auto j = r.to_json();
    ASSERT_TRUE(j["action"][0].contains("replace_in_history"));
    ASSERT_EQ(j["action"][0]["replace_in_history"].get<bool>(), true);
    return true;
}

// ========================================================================
// Media
// ========================================================================

TEST(function_result_say) {
    FunctionResult r("test");
    r.say("Hello there!");
    auto j = r.to_json();
    ASSERT_TRUE(j["action"][0].contains("say"));
    ASSERT_EQ(j["action"][0]["say"].get<std::string>(), "Hello there!");
    return true;
}

TEST(function_result_play_background_file) {
    FunctionResult r("test");
    r.play_background_file("music.mp3");
    auto j = r.to_json();
    ASSERT_TRUE(j["action"][0].contains("playback_bg"));
    ASSERT_EQ(j["action"][0]["playback_bg"].get<std::string>(), "music.mp3");
    return true;
}

TEST(function_result_play_background_file_wait) {
    FunctionResult r("test");
    r.play_background_file("music.mp3", true);
    auto j = r.to_json();
    ASSERT_TRUE(j["action"][0].contains("playback_bg"));
    ASSERT_EQ(j["action"][0]["playback_bg"]["file"].get<std::string>(), "music.mp3");
    ASSERT_EQ(j["action"][0]["playback_bg"]["wait"].get<bool>(), true);
    return true;
}

TEST(function_result_stop_background_file) {
    FunctionResult r("test");
    r.stop_background_file();
    auto j = r.to_json();
    ASSERT_TRUE(j["action"][0].contains("stop_playback_bg"));
    return true;
}

// ========================================================================
// Speech & AI
// ========================================================================

TEST(function_result_add_dynamic_hints) {
    FunctionResult r("test");
    r.add_dynamic_hints(json::array({"Cabby", "SignalWire"}));
    auto j = r.to_json();
    ASSERT_TRUE(j["action"][0].contains("add_dynamic_hints"));
    return true;
}

TEST(function_result_clear_dynamic_hints) {
    FunctionResult r("test");
    r.clear_dynamic_hints();
    auto j = r.to_json();
    ASSERT_TRUE(j["action"][0].contains("clear_dynamic_hints"));
    return true;
}

TEST(function_result_end_of_speech_timeout) {
    FunctionResult r("test");
    r.set_end_of_speech_timeout(500);
    auto j = r.to_json();
    ASSERT_EQ(j["action"][0]["end_of_speech_timeout"].get<int>(), 500);
    return true;
}

TEST(function_result_speech_event_timeout) {
    FunctionResult r("test");
    r.set_speech_event_timeout(1000);
    auto j = r.to_json();
    ASSERT_EQ(j["action"][0]["speech_event_timeout"].get<int>(), 1000);
    return true;
}

TEST(function_result_toggle_functions) {
    FunctionResult r("test");
    r.toggle_functions(json::array({
        json::object({{"function", "get_weather"}, {"active", false}}),
        json::object({{"function", "search"}, {"active", true}})
    }));
    auto j = r.to_json();
    ASSERT_TRUE(j["action"][0].contains("toggle_functions"));
    return true;
}

TEST(function_result_enable_functions_on_timeout) {
    FunctionResult r("test");
    r.enable_functions_on_timeout(true);
    auto j = r.to_json();
    ASSERT_EQ(j["action"][0]["functions_on_speaker_timeout"].get<bool>(), true);
    return true;
}

TEST(function_result_enable_extensive_data) {
    FunctionResult r("test");
    r.enable_extensive_data(true);
    auto j = r.to_json();
    ASSERT_EQ(j["action"][0]["extensive_data"].get<bool>(), true);
    return true;
}

TEST(function_result_update_settings) {
    FunctionResult r("test");
    r.update_settings({{"temperature", 0.7}});
    auto j = r.to_json();
    ASSERT_TRUE(j["action"][0].contains("settings"));
    return true;
}

TEST(function_result_simulate_user_input) {
    FunctionResult r("test");
    r.simulate_user_input("Hello AI");
    auto j = r.to_json();
    ASSERT_EQ(j["action"][0]["user_input"].get<std::string>(), "Hello AI");
    return true;
}

// ========================================================================
// Advanced / SWML
// ========================================================================

TEST(function_result_execute_swml) {
    FunctionResult r("test");
    json swml = {{"version", "1.0.0"}, {"sections", {{"main", json::array()}}}};
    r.execute_swml(swml);
    auto j = r.to_json();
    ASSERT_TRUE(j["action"][0].contains("SWML"));
    return true;
}

TEST(function_result_execute_swml_transfer) {
    FunctionResult r("test");
    json swml = {{"version", "1.0.0"}, {"sections", {{"main", json::array()}}}};
    r.execute_swml(swml, true);
    auto j = r.to_json();
    ASSERT_TRUE(j["action"][0]["SWML"].contains("transfer"));
    return true;
}

TEST(function_result_join_conference_simple) {
    FunctionResult r("test");
    r.join_conference("my_conf");
    auto j = r.to_json();
    ASSERT_TRUE(j["action"][0].contains("SWML"));
    return true;
}

TEST(function_result_join_room) {
    FunctionResult r("test");
    r.join_room("my_room");
    auto j = r.to_json();
    ASSERT_TRUE(j["action"][0].contains("SWML"));
    return true;
}

TEST(function_result_sip_refer) {
    FunctionResult r("test");
    r.sip_refer("sip:user@example.com");
    auto j = r.to_json();
    ASSERT_TRUE(j["action"][0].contains("SWML"));
    return true;
}

TEST(function_result_tap) {
    FunctionResult r("test");
    r.tap("wss://example.com/stream");
    auto j = r.to_json();
    ASSERT_TRUE(j["action"][0].contains("SWML"));
    return true;
}

TEST(function_result_stop_tap) {
    FunctionResult r("test");
    r.stop_tap("ctrl-123");
    auto j = r.to_json();
    ASSERT_TRUE(j["action"][0].contains("SWML"));
    return true;
}

TEST(function_result_send_sms) {
    FunctionResult r("SMS sent");
    r.send_sms("+15551234567", "+15559876543", "Hello!");
    auto j = r.to_json();
    ASSERT_TRUE(j["action"][0].contains("SWML"));
    return true;
}

TEST(function_result_send_sms_requires_body_or_media) {
    FunctionResult r("test");
    ASSERT_THROWS(r.send_sms("+15551234567", "+15559876543"));
    return true;
}

TEST(function_result_pay) {
    FunctionResult r("Processing payment");
    r.pay("https://pay.example.com/process");
    auto j = r.to_json();
    ASSERT_TRUE(j["action"][0].contains("SWML"));
    return true;
}

TEST(function_result_record_call) {
    FunctionResult r("Recording");
    r.record_call("ctrl-1", true, "mp3");
    auto j = r.to_json();
    ASSERT_TRUE(j["action"][0].contains("SWML"));
    return true;
}

TEST(function_result_stop_record_call) {
    FunctionResult r("Stopped");
    r.stop_record_call("ctrl-1");
    auto j = r.to_json();
    ASSERT_TRUE(j["action"][0].contains("SWML"));
    return true;
}

// ========================================================================
// RPC
// ========================================================================

TEST(function_result_execute_rpc) {
    FunctionResult r("RPC");
    r.execute_rpc("dial", json::object({{"to", "+1555"}}));
    auto j = r.to_json();
    ASSERT_TRUE(j["action"][0].contains("SWML"));
    return true;
}

TEST(function_result_rpc_dial) {
    FunctionResult r("Dialing");
    r.rpc_dial("+15551234567", "+15559876543", "https://example.com/swml");
    auto j = r.to_json();
    ASSERT_EQ(j["action"].size(), 1u);
    return true;
}

TEST(function_result_rpc_ai_message) {
    FunctionResult r("Message sent");
    r.rpc_ai_message("call-123", "Hello from other call");
    auto j = r.to_json();
    ASSERT_EQ(j["action"].size(), 1u);
    return true;
}

TEST(function_result_rpc_ai_unhold) {
    FunctionResult r("Unholding");
    r.rpc_ai_unhold("call-123");
    auto j = r.to_json();
    ASSERT_EQ(j["action"].size(), 1u);
    return true;
}

// ========================================================================
// Payment Helpers
// ========================================================================

TEST(function_result_create_payment_prompt) {
    auto prompt = FunctionResult::create_payment_prompt(
        "payment-card-number",
        {FunctionResult::create_payment_action("Say", "Please enter card number")},
        "visa mastercard"
    );
    ASSERT_EQ(prompt["for"].get<std::string>(), "payment-card-number");
    ASSERT_TRUE(prompt.contains("actions"));
    ASSERT_TRUE(prompt.contains("card_type"));
    return true;
}

TEST(function_result_create_payment_action) {
    auto action = FunctionResult::create_payment_action("Say", "Enter your card");
    ASSERT_EQ(action["type"].get<std::string>(), "Say");
    ASSERT_EQ(action["phrase"].get<std::string>(), "Enter your card");
    return true;
}

TEST(function_result_create_payment_parameter) {
    auto param = FunctionResult::create_payment_parameter("amount", "19.99");
    ASSERT_EQ(param["name"].get<std::string>(), "amount");
    ASSERT_EQ(param["value"].get<std::string>(), "19.99");
    return true;
}

// ========================================================================
// ToolDefinition
// ========================================================================

TEST(tool_definition_to_swaig_json) {
    ToolDefinition td;
    td.name = "get_weather";
    td.description = "Get weather for a city";
    td.parameters = json::object({
        {"type", "object"},
        {"properties", json::object({
            {"city", json::object({{"type", "string"}, {"description", "City name"}})}
        })},
        {"required", json::array({"city"})}
    });
    td.secure = true;

    auto j = td.to_swaig_json("https://agent.example.com/swaig");
    ASSERT_EQ(j["function"].get<std::string>(), "get_weather");
    ASSERT_EQ(j["description"].get<std::string>(), "Get weather for a city");
    ASSERT_TRUE(j.contains("parameters"));
    ASSERT_TRUE(j.contains("web_hook_url"));
    ASSERT_TRUE(j.contains("secure"));
    return true;
}

TEST(tool_definition_default_parameters) {
    ToolDefinition td;
    td.name = "test";
    td.description = "test tool";

    auto j = td.to_swaig_json();
    ASSERT_TRUE(j.contains("parameters"));
    ASSERT_EQ(j["parameters"]["type"].get<std::string>(), "object");
    ASSERT_FALSE(j.contains("web_hook_url"));
    ASSERT_FALSE(j.contains("secure"));
    return true;
}
