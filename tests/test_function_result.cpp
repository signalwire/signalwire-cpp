// SwaigFunctionResult tests

#include <stdexcept>
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

// ------------------------------------------------------------------------
// join_conference — full parity with Python core/function_result.py
// (19 params: name + 18 optional; 7 validations; simple/full emission).
// Parity ref: signalwire/signalwire/core/function_result.py join_conference.
// The verb lives under the SWML action wrapper that execute_swml builds:
//   action[0]["SWML"]["sections"]["main"][0]["join_conference"]
// ------------------------------------------------------------------------

// Reach into the join_conference verb payload of a just-built result.
static json jc_verb(const FunctionResult& r) {
    return r.to_json()["action"][0]["SWML"]["sections"]["main"][0]["join_conference"];
}

TEST(function_result_join_conference_simple) {
    // All-defaults collapses to the bare conference-name string form.
    FunctionResult r("test");
    r.join_conference("my_conf");
    auto verb = jc_verb(r);
    ASSERT_TRUE(verb.is_string());
    ASSERT_EQ(verb.get<std::string>(), "my_conf");
    return true;
}

TEST(function_result_join_conference_simple_explicit_defaults) {
    // Passing the documented defaults explicitly must ALSO collapse to the
    // bare-name form (every param == its default).
    FunctionResult r("test");
    JoinConferenceOptions opts;  // all std::optional fields unset == defaults
    r.join_conference("conf-default", opts);
    auto verb = jc_verb(r);
    ASSERT_TRUE(verb.is_string());
    ASSERT_EQ(verb.get<std::string>(), "conf-default");
    return true;
}

TEST(function_result_join_conference_full_object) {
    // A non-default param forces the full object form; assert EVERY
    // non-default wire key (snake_case) appears with the right value, and
    // that default-valued params are omitted.
    FunctionResult r("test");
    JoinConferenceOptions opts;
    opts.muted = true;
    opts.beep = "onEnter";
    opts.start_on_enter = false;
    opts.end_on_exit = true;
    opts.wait_url = "https://hold.example.com/swml";
    opts.max_participants = 10;
    opts.record = "record-from-start";
    opts.region = "us-east";
    opts.trim = "do-not-trim";
    opts.coach = "call-abc";
    opts.status_callback_event = "start end join leave";
    opts.status_callback = "https://cb.example.com/status";
    opts.status_callback_method = "GET";
    opts.recording_status_callback = "https://cb.example.com/rec";
    opts.recording_status_callback_method = "GET";
    opts.recording_status_callback_event = "in-progress";
    opts.result = json::object({{"switch", "x"}});
    r.join_conference("big-conf", opts);

    auto verb = jc_verb(r);
    ASSERT_TRUE(verb.is_object());
    ASSERT_EQ(verb["name"].get<std::string>(), "big-conf");
    ASSERT_EQ(verb["muted"].get<bool>(), true);
    ASSERT_EQ(verb["beep"].get<std::string>(), "onEnter");
    ASSERT_EQ(verb["start_on_enter"].get<bool>(), false);
    ASSERT_EQ(verb["end_on_exit"].get<bool>(), true);
    ASSERT_EQ(verb["wait_url"].get<std::string>(), "https://hold.example.com/swml");
    ASSERT_EQ(verb["max_participants"].get<int>(), 10);
    ASSERT_EQ(verb["record"].get<std::string>(), "record-from-start");
    ASSERT_EQ(verb["region"].get<std::string>(), "us-east");
    ASSERT_EQ(verb["trim"].get<std::string>(), "do-not-trim");
    ASSERT_EQ(verb["coach"].get<std::string>(), "call-abc");
    ASSERT_EQ(verb["status_callback_event"].get<std::string>(), "start end join leave");
    ASSERT_EQ(verb["status_callback"].get<std::string>(), "https://cb.example.com/status");
    ASSERT_EQ(verb["status_callback_method"].get<std::string>(), "GET");
    ASSERT_EQ(verb["recording_status_callback"].get<std::string>(), "https://cb.example.com/rec");
    ASSERT_EQ(verb["recording_status_callback_method"].get<std::string>(), "GET");
    ASSERT_EQ(verb["recording_status_callback_event"].get<std::string>(), "in-progress");
    ASSERT_EQ(verb["result"]["switch"].get<std::string>(), "x");
    // No holdAudio key — Python uses wait_url.
    ASSERT_FALSE(verb.contains("holdAudio"));
    return true;
}

TEST(function_result_join_conference_full_omits_defaults) {
    // Full form is triggered by ONE non-default (muted); every other param
    // is left at its default and must NOT appear in the emitted object.
    FunctionResult r("test");
    JoinConferenceOptions opts;
    opts.muted = true;
    r.join_conference("conf", opts);
    auto verb = jc_verb(r);
    ASSERT_TRUE(verb.is_object());
    ASSERT_EQ(verb["name"].get<std::string>(), "conf");
    ASSERT_EQ(verb["muted"].get<bool>(), true);
    // Defaults omitted:
    ASSERT_FALSE(verb.contains("beep"));
    ASSERT_FALSE(verb.contains("start_on_enter"));
    ASSERT_FALSE(verb.contains("end_on_exit"));
    ASSERT_FALSE(verb.contains("wait_url"));
    ASSERT_FALSE(verb.contains("max_participants"));
    ASSERT_FALSE(verb.contains("record"));
    ASSERT_FALSE(verb.contains("region"));
    ASSERT_FALSE(verb.contains("trim"));
    ASSERT_FALSE(verb.contains("coach"));
    ASSERT_FALSE(verb.contains("status_callback_event"));
    ASSERT_FALSE(verb.contains("status_callback"));
    ASSERT_FALSE(verb.contains("status_callback_method"));
    ASSERT_FALSE(verb.contains("recording_status_callback"));
    ASSERT_FALSE(verb.contains("recording_status_callback_method"));
    ASSERT_FALSE(verb.contains("recording_status_callback_event"));
    ASSERT_FALSE(verb.contains("result"));
    return true;
}

TEST(function_result_join_conference_flat_overload) {
    // The flat positional overload (mirrors Python's signature exactly) must
    // emit identically to the options-struct form.
    FunctionResult r("test");
    r.join_conference("flat-conf", /*muted=*/true, /*beep=*/"onExit");
    auto verb = jc_verb(r);
    ASSERT_TRUE(verb.is_object());
    ASSERT_EQ(verb["name"].get<std::string>(), "flat-conf");
    ASSERT_EQ(verb["muted"].get<bool>(), true);
    ASSERT_EQ(verb["beep"].get<std::string>(), "onExit");
    return true;
}

TEST(function_result_join_conference_invalid_beep) {
    FunctionResult r("test");
    JoinConferenceOptions opts;
    opts.beep = "sometimes";
    ASSERT_THROWS(r.join_conference("conf", opts));
    // Message mirrors Python's f"beep must be one of {list}" with the list
    // rendered the way Python's repr does (single-quoted members).
    bool checked = false;
    try {
        r.join_conference("conf", opts);
    } catch (const std::invalid_argument& e) {
        std::string msg = e.what();
        ASSERT_EQ(msg, "beep must be one of ['true', 'false', 'onEnter', 'onExit']");
        checked = true;
    }
    ASSERT_TRUE(checked);
    return true;
}

TEST(function_result_join_conference_max_participants_over) {
    FunctionResult r("test");
    JoinConferenceOptions opts;
    opts.max_participants = 251;
    ASSERT_THROWS(r.join_conference("conf", opts));
    return true;
}

TEST(function_result_join_conference_max_participants_zero) {
    FunctionResult r("test");
    JoinConferenceOptions opts;
    opts.max_participants = 0;
    ASSERT_THROWS(r.join_conference("conf", opts));
    return true;
}

TEST(function_result_join_conference_max_participants_negative) {
    FunctionResult r("test");
    JoinConferenceOptions opts;
    opts.max_participants = -5;
    ASSERT_THROWS(r.join_conference("conf", opts));
    return true;
}

TEST(function_result_join_conference_max_participants_boundary) {
    // 250 is the inclusive upper bound — must NOT throw (and equals default,
    // so it collapses to the simple name form).
    FunctionResult r("test");
    JoinConferenceOptions opts;
    opts.max_participants = 250;
    r.join_conference("edge", opts);
    auto verb = jc_verb(r);
    ASSERT_TRUE(verb.is_string());
    ASSERT_EQ(verb.get<std::string>(), "edge");
    return true;
}

TEST(function_result_join_conference_invalid_record) {
    FunctionResult r("test");
    JoinConferenceOptions opts;
    opts.record = "always";
    ASSERT_THROWS(r.join_conference("conf", opts));
    return true;
}

TEST(function_result_join_conference_invalid_trim) {
    FunctionResult r("test");
    JoinConferenceOptions opts;
    opts.trim = "maybe";
    ASSERT_THROWS(r.join_conference("conf", opts));
    return true;
}

TEST(function_result_join_conference_invalid_status_callback_method) {
    FunctionResult r("test");
    JoinConferenceOptions opts;
    opts.status_callback_method = "PUT";
    ASSERT_THROWS(r.join_conference("conf", opts));
    return true;
}

TEST(function_result_join_conference_invalid_recording_status_callback_method) {
    FunctionResult r("test");
    JoinConferenceOptions opts;
    opts.recording_status_callback_method = "DELETE";
    ASSERT_THROWS(r.join_conference("conf", opts));
    return true;
}

TEST(function_result_join_conference_empty_name) {
    FunctionResult r("test");
    ASSERT_THROWS(r.join_conference(""));
    return true;
}

TEST(function_result_join_conference_whitespace_name) {
    // Python trims then checks emptiness: "   " is empty after strip().
    FunctionResult r("test");
    ASSERT_THROWS(r.join_conference("   "));
    return true;
}

TEST(function_result_join_conference_chaining) {
    // Returns *this for fluent chaining; the chained say() lands as a 2nd
    // action after the join_conference SWML action.
    FunctionResult r("test");
    r.join_conference("chain-conf").say("joined");
    auto j = r.to_json();
    ASSERT_EQ(j["action"].size(), 2u);
    ASSERT_TRUE(j["action"][0].contains("SWML"));
    ASSERT_EQ(j["action"][1]["say"].get<std::string>(), "joined");
    return true;
}

TEST(function_result_join_conference_enum_typed_sets) {
    // The closed-set enums emit the identical wire strings as the bare
    // strings (Gender/SkillName affordance pattern).
    FunctionResult r("test");
    JoinConferenceOptions opts;
    opts.beep = ConferenceBeep::OnEnter;
    opts.record = ConferenceRecord::RecordFromStart;
    opts.trim = ConferenceTrim::DoNotTrim;
    opts.status_callback_method = CallbackMethod::Get;
    r.join_conference("typed-conf", opts);
    auto verb = jc_verb(r);
    ASSERT_EQ(verb["beep"].get<std::string>(), "onEnter");
    ASSERT_EQ(verb["record"].get<std::string>(), "record-from-start");
    ASSERT_EQ(verb["trim"].get<std::string>(), "do-not-trim");
    ASSERT_EQ(verb["status_callback_method"].get<std::string>(), "GET");
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

// --- tap validation — byte-exact Python ValueError messages ----------------
// Python uses f"... must be one of {list}", repr-rendered with single quotes.

TEST(function_result_tap_invalid_direction) {
    FunctionResult r("test");
    ASSERT_THROWS(r.tap("wss://x", "", "sideways"));
    bool checked = false;
    try {
        r.tap("wss://x", "", "sideways");
    } catch (const std::invalid_argument& e) {
        ASSERT_EQ(std::string(e.what()),
                  "direction must be one of ['speak', 'hear', 'both']");
        checked = true;
    }
    ASSERT_TRUE(checked);
    return true;
}

TEST(function_result_tap_invalid_codec) {
    FunctionResult r("test");
    ASSERT_THROWS(r.tap("wss://x", "", "both", "OPUS"));
    bool checked = false;
    try {
        r.tap("wss://x", "", "both", "OPUS");
    } catch (const std::invalid_argument& e) {
        ASSERT_EQ(std::string(e.what()),
                  "codec must be one of ['PCMU', 'PCMA']");
        checked = true;
    }
    ASSERT_TRUE(checked);
    return true;
}

TEST(function_result_tap_invalid_rtp_ptime) {
    FunctionResult r("test");
    ASSERT_THROWS(r.tap("wss://x", "", "both", "PCMU", 0));
    bool checked = false;
    try {
        r.tap("wss://x", "", "both", "PCMU", -5);
    } catch (const std::invalid_argument& e) {
        ASSERT_EQ(std::string(e.what()), "rtp_ptime must be a positive integer");
        checked = true;
    }
    ASSERT_TRUE(checked);
    return true;
}

TEST(function_result_tap_valid_hear_codec_pcma) {
    // The in-set non-default values must NOT throw and must round-trip.
    FunctionResult r("test");
    r.tap("rtp://1.2.3.4:5000", "tap-1", "hear", "PCMA", 30, "https://cb/x");
    auto verb = r.to_json()["action"][0]["SWML"]["sections"]["main"][0]["tap"];
    ASSERT_EQ(verb["uri"].get<std::string>(), "rtp://1.2.3.4:5000");
    ASSERT_EQ(verb["control_id"].get<std::string>(), "tap-1");
    ASSERT_EQ(verb["direction"].get<std::string>(), "hear");
    ASSERT_EQ(verb["codec"].get<std::string>(), "PCMA");
    ASSERT_EQ(verb["rtp_ptime"].get<int>(), 30);
    ASSERT_EQ(verb["status_url"].get<std::string>(), "https://cb/x");
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

// --- record_call validation — byte-exact Python ValueError messages --------
// Python: "format must be 'wav', 'mp3', or 'mp4'" and
//         "direction must be 'speak', 'listen', or 'both'" (hand-written, NOT
// the list-repr form).

TEST(function_result_record_call_invalid_format) {
    FunctionResult r("Recording");
    ASSERT_THROWS(r.record_call("", false, "flac"));
    bool checked = false;
    try {
        r.record_call("", false, "flac");
    } catch (const std::invalid_argument& e) {
        ASSERT_EQ(std::string(e.what()), "format must be 'wav', 'mp3', or 'mp4'");
        checked = true;
    }
    ASSERT_TRUE(checked);
    return true;
}

TEST(function_result_record_call_invalid_direction) {
    FunctionResult r("Recording");
    ASSERT_THROWS(r.record_call("", false, "wav", "sideways"));
    bool checked = false;
    try {
        r.record_call("", false, "wav", "sideways");
    } catch (const std::invalid_argument& e) {
        ASSERT_EQ(std::string(e.what()),
                  "direction must be 'speak', 'listen', or 'both'");
        checked = true;
    }
    ASSERT_TRUE(checked);
    return true;
}

TEST(function_result_record_call_mp4_accepted) {
    // mp4 is in the {wav,mp3,mp4} set — must NOT throw.
    FunctionResult r("Recording");
    r.record_call("ctrl-9", false, "mp4", "listen");
    auto verb = r.to_json()["action"][0]["SWML"]["sections"]["main"][0]["record_call"];
    ASSERT_EQ(verb["format"].get<std::string>(), "mp4");
    ASSERT_EQ(verb["direction"].get<std::string>(), "listen");
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

// ------------------------------------------------------------------------
// execute_rpc — byte-exact parity with Python core/function_result.py.
// Python builds the rpc command as {method, call_id?, node_id?, params?}
// where call_id/node_id are SIBLINGS of params (top-level rpc fields), and
// params is OMITTED entirely when empty. The verb lives at:
//   action[0]["SWML"]["sections"]["main"][0]["execute_rpc"]
// ------------------------------------------------------------------------

// Reach into the execute_rpc command payload of a just-built result.
static json rpc_cmd(const FunctionResult& r) {
    return r.to_json()["action"][0]["SWML"]["sections"]["main"][0]["execute_rpc"];
}

TEST(function_result_execute_rpc) {
    FunctionResult r("RPC");
    r.execute_rpc("dial", json::object({{"to", "+1555"}}));
    auto j = r.to_json();
    ASSERT_TRUE(j["action"][0].contains("SWML"));
    return true;
}

TEST(function_result_execute_rpc_shape) {
    // method present; caller params nested UNDER "params" (not spread at top
    // level); no call_id/node_id when not supplied.
    FunctionResult r("RPC");
    r.execute_rpc("dial", json::object({{"to", "+1555"}}));
    auto cmd = rpc_cmd(r);
    ASSERT_EQ(cmd["method"].get<std::string>(), "dial");
    ASSERT_TRUE(cmd.contains("params"));
    ASSERT_EQ(cmd["params"]["to"].get<std::string>(), "+1555");
    // Caller param must NOT have leaked to the top level.
    ASSERT_FALSE(cmd.contains("to"));
    ASSERT_FALSE(cmd.contains("call_id"));
    ASSERT_FALSE(cmd.contains("node_id"));
    return true;
}

TEST(function_result_execute_rpc_call_id_sibling_of_params) {
    // call_id/node_id are TOP-LEVEL rpc fields, SIBLINGS of params — they must
    // NOT be buried inside the params object.
    FunctionResult r("RPC");
    r.execute_rpc("ai_message",
                  json::object({{"role", "system"}, {"message_text", "hi"}}),
                  "call-xyz", "node-7");
    auto cmd = rpc_cmd(r);
    ASSERT_EQ(cmd["method"].get<std::string>(), "ai_message");
    ASSERT_EQ(cmd["call_id"].get<std::string>(), "call-xyz");
    ASSERT_EQ(cmd["node_id"].get<std::string>(), "node-7");
    ASSERT_TRUE(cmd.contains("params"));
    ASSERT_EQ(cmd["params"]["role"].get<std::string>(), "system");
    ASSERT_EQ(cmd["params"]["message_text"].get<std::string>(), "hi");
    // call_id/node_id must NOT be inside params.
    ASSERT_FALSE(cmd["params"].contains("call_id"));
    ASSERT_FALSE(cmd["params"].contains("node_id"));
    return true;
}

TEST(function_result_execute_rpc_empty_params_omitted) {
    // Empty params -> the "params" key is OMITTED entirely (Python `if params:`).
    FunctionResult r("RPC");
    r.execute_rpc("ai_unhold", json::object(), "call-abc");
    auto cmd = rpc_cmd(r);
    ASSERT_EQ(cmd["method"].get<std::string>(), "ai_unhold");
    ASSERT_EQ(cmd["call_id"].get<std::string>(), "call-abc");
    ASSERT_FALSE(cmd.contains("params"));
    return true;
}

TEST(function_result_rpc_dial) {
    FunctionResult r("Dialing");
    r.rpc_dial("+15551234567", "+15559876543", "https://example.com/swml");
    auto j = r.to_json();
    ASSERT_EQ(j["action"].size(), 1u);
    return true;
}

TEST(function_result_rpc_dial_shape) {
    // rpc_dial uses method "dial" (NOT "calling.dial"); the device + dest_swml
    // ride inside params; no call_id (none supplied).
    FunctionResult r("Dialing");
    r.rpc_dial("+15551234567", "+15559876543", "https://example.com/swml");
    auto cmd = rpc_cmd(r);
    ASSERT_EQ(cmd["method"].get<std::string>(), "dial");
    ASSERT_FALSE(cmd.contains("call_id"));
    ASSERT_TRUE(cmd.contains("params"));
    ASSERT_EQ(cmd["params"]["devices"]["type"].get<std::string>(), "phone");
    ASSERT_EQ(cmd["params"]["devices"]["params"]["to_number"].get<std::string>(), "+15551234567");
    ASSERT_EQ(cmd["params"]["devices"]["params"]["from_number"].get<std::string>(), "+15559876543");
    ASSERT_EQ(cmd["params"]["dest_swml"].get<std::string>(), "https://example.com/swml");
    return true;
}

TEST(function_result_rpc_ai_message) {
    FunctionResult r("Message sent");
    r.rpc_ai_message("call-123", "Hello from other call");
    auto j = r.to_json();
    ASSERT_EQ(j["action"].size(), 1u);
    return true;
}

TEST(function_result_rpc_ai_message_shape) {
    // call_id is a top-level rpc field (sibling of params); role/message_text
    // ride inside params.
    FunctionResult r("Message sent");
    r.rpc_ai_message("call-123", "Hello from other call");
    auto cmd = rpc_cmd(r);
    ASSERT_EQ(cmd["method"].get<std::string>(), "ai_message");
    ASSERT_EQ(cmd["call_id"].get<std::string>(), "call-123");
    ASSERT_TRUE(cmd.contains("params"));
    ASSERT_EQ(cmd["params"]["role"].get<std::string>(), "system");
    ASSERT_EQ(cmd["params"]["message_text"].get<std::string>(), "Hello from other call");
    ASSERT_FALSE(cmd["params"].contains("call_id"));
    return true;
}

TEST(function_result_rpc_ai_unhold) {
    FunctionResult r("Unholding");
    r.rpc_ai_unhold("call-123");
    auto j = r.to_json();
    ASSERT_EQ(j["action"].size(), 1u);
    return true;
}

TEST(function_result_rpc_ai_unhold_shape) {
    // ai_unhold passes empty params -> NO "params" key; call_id at top level.
    FunctionResult r("Unholding");
    r.rpc_ai_unhold("call-123");
    auto cmd = rpc_cmd(r);
    ASSERT_EQ(cmd["method"].get<std::string>(), "ai_unhold");
    ASSERT_EQ(cmd["call_id"].get<std::string>(), "call-123");
    ASSERT_FALSE(cmd.contains("params"));
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
