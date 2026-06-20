// emit_corpus.cpp — the C++ port's EMISSION-DUMP program for the cross-port
// emission differ (porting-sdk/scripts/diff_port_emission.py).
//
// It builds the shared FunctionResult corpus
// (porting-sdk/scripts/emission_corpus.py — the single source of truth) using
// the C++ SDK's native signalwire::swaig::FunctionResult API, serialises each
// entry the same way the SDK sends it on the wire (to_json()), and prints ONE
// JSON object mapping
//
//     corpus-id -> emission
//
// to stdout. The differ runs this program, parses that object, and byte-compares
// each entry against Python's to_dict(). See the "per-port dump contract" in the
// differ's --help and IDIOM_PASS_JOURNAL.md §4 Tier-0. This mirrors the Go
// reference dump (signalwire-go/cmd/emit-corpus).
//
// CONTRACT (why this file looks the way it does):
//   - Every corpus id in emission_corpus.corpus_ids() MUST appear here exactly
//     once (the differ rejects an id-set mismatch as a setup error — a skewed set
//     would mask real diffs). When the shared corpus grows, add the new id here.
//   - The argument VALUES are the WIRE values (plain strings/numbers/bools/maps).
//     Where the C++ API types a closed set (RecordFormat, RecordDirection,
//     TapDirection, Codec, the join_conference enums) we pass the typed constant
//     whose string value is the wire value, proving the typed path emits
//     byte-identically to the string.
//   - nlohmann::json serialises object keys in sorted order, so the printed JSON
//     is canonical; only stdout carries the JSON object, logs go to stderr.
//
// Build: a CMake target `emit_corpus` (see CMakeLists.txt). Built in the swcpp
// OpenSSL-3.0 container; run via the EMISSION gate in scripts/run-ci.sh.

#include <functional>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "signalwire/swaig/function_result.hpp"

using signalwire::swaig::CallbackMethod;
using signalwire::swaig::Codec;
using signalwire::swaig::ConferenceBeep;
using signalwire::swaig::ConferenceRecord;
using signalwire::swaig::ConferenceTrim;
using signalwire::swaig::FunctionResult;
using signalwire::swaig::JoinConferenceOptions;
using signalwire::swaig::RecordDirection;
using signalwire::swaig::RecordFormat;
using signalwire::swaig::TapDirection;
using json = nlohmann::json;

namespace {

// The Python default ai_response for pay(); spelled out so pay.full pins the
// full-arity emission deterministically (matches emission_corpus._PAY_AI_RESPONSE).
const char* kPayAiResponse =
    "The payment status is ${pay_result}, do not mention anything else about "
    "collecting payment if successful.";

// An entry pairs a stable corpus id with the FunctionResult it produces. The
// build closure keeps each line a single, readable native call (mirrors the Go
// dump's `entry{id, build}` shape).
struct Entry {
  std::string id;
  std::function<FunctionResult()> build;
};

// Tiny constructor helper: FunctionResult(response).
FunctionResult fr(const std::string& response = "") { return FunctionResult(response); }

// The C++-native mirror of porting-sdk/scripts/emission_corpus.py. The ids and
// the resulting emission must match the Python oracle exactly (modulo the
// whole-float artifact the differ normalises: Python 44.0 == C++ 44.0 -> 44).
std::vector<Entry> corpus() {
  return {
      // ---- envelope edge cases (to_json() shape) --------------------------
      {"envelope.empty", [] { return fr(""); }},
      {"envelope.response_only", [] { return fr("Hello, world!"); }},
      {"envelope.post_process_no_action", [] { return fr("hi").set_post_process(true); }},
      {"envelope.action_only", [] { return fr("").hangup(); }},
      {"envelope.post_process_with_action",
       [] { return fr("Transferring").set_post_process(true).hangup(); }},
      {"envelope.response_and_action", [] { return fr("Goodbye").hangup(); }},

      // ---- connect --------------------------------------------------------
      {"connect.final_true", [] { return fr("").connect("+15551234567", true); }},
      {"connect.final_false", [] { return fr("").connect("+15551234567", false); }},
      {"connect.from_addr",
       [] { return fr("").connect("support@example.com", false, "+15559876543"); }},

      // ---- swml_transfer --------------------------------------------------
      {"swml_transfer.default",
       [] { return fr("").swml_transfer("https://dest.example.com/swml", "Goodbye!"); }},
      {"swml_transfer.final_false",
       [] {
         return fr("").swml_transfer("https://dest.example.com/swml",
                                     "Welcome back. How else can I help?", false);
       }},

      // ---- simple call-control actions ------------------------------------
      {"hangup", [] { return fr("").hangup(); }},
      {"hold.default", [] { return fr("").hold(); }},
      {"hold.value", [] { return fr("").hold(120); }},
      {"hold.clamp_high", [] { return fr("").hold(5000); }},
      {"hold.clamp_low", [] { return fr("").hold(-5); }},
      {"stop", [] { return fr("").stop(); }},
      {"say", [] { return fr("").say("Please hold while I connect you."); }},

      // ---- wait_for_user (each branch) ------------------------------------
      {"wait_for_user.default", [] { return fr("").wait_for_user(); }},
      {"wait_for_user.answer_first",
       [] { return fr("").wait_for_user(std::nullopt, std::nullopt, true); }},
      {"wait_for_user.timeout", [] { return fr("").wait_for_user(std::nullopt, 30); }},
      {"wait_for_user.enabled_true", [] { return fr("").wait_for_user(true); }},
      {"wait_for_user.enabled_false", [] { return fr("").wait_for_user(false); }},

      // ---- global data / metadata -----------------------------------------
      {"set_global_data",
       [] { return fr("").update_global_data({{"plan", "premium"}, {"chips", 1000}}); }},
      {"unset_global_data.list",
       [] { return fr("").remove_global_data(json::array({"plan", "chips"})); }},
      {"unset_global_data.str", [] { return fr("").remove_global_data("plan"); }},
      {"set_metadata", [] { return fr("").set_metadata({{"token", "abc"}, {"count", 3}}); }},
      {"unset_metadata.list",
       [] { return fr("").remove_metadata(json::array({"token", "count"})); }},
      {"unset_metadata.str", [] { return fr("").remove_metadata("token"); }},

      // ---- swml_user_event ------------------------------------------------
      {"swml_user_event",
       [] {
         return fr("").swml_user_event({{"type", "cards_dealt"},
                                        {"player_hand", json::array({"AS", "KH"})},
                                        {"player_score", 21}});
       }},

      // ---- step / context changes -----------------------------------------
      {"change_step", [] { return fr("").swml_change_step("collect_payment"); }},
      {"change_context", [] { return fr("").swml_change_context("billing"); }},

      // ---- switch_context (simple vs object) ------------------------------
      // Python passes None for user_prompt in full_reset; C++ takes a bare
      // std::string and Python treats None and "" identically (`if user_prompt:`),
      // so "" reproduces the same wire shape.
      {"switch_context.simple",
       [] { return fr("").switch_context("You are now a billing agent."); }},
      {"switch_context.object",
       [] {
         return fr("").switch_context("New system prompt", "User said something", true, false);
       }},
      {"switch_context.full_reset",
       [] { return fr("").switch_context("Reset prompt", "", false, true); }},

      // ---- background file play/stop --------------------------------------
      {"playback_bg.simple", [] { return fr("").play_background_file("music.mp3"); }},
      {"playback_bg.wait", [] { return fr("").play_background_file("music.mp3", true); }},
      {"stop_playback_bg", [] { return fr("").stop_background_file(); }},

      // ---- join_room / sip_refer ------------------------------------------
      {"join_room", [] { return fr("").join_room("team-standup"); }},
      {"sip_refer", [] { return fr("").sip_refer("sip:agent@example.com"); }},

      // ---- send_sms -------------------------------------------------------
      {"send_sms.body",
       [] {
         return fr("").send_sms("+15551112222", "+15553334444", "Your appointment is confirmed.");
       }},
      {"send_sms.full",
       [] {
         return fr("").send_sms("+15551112222", "+15553334444", "See attached.",
                                {"https://ex.com/a.jpg"}, {"receipt", "vip"}, "us");
       }},

      // ---- pay ------------------------------------------------------------
      {"pay.minimal", [] { return fr("").pay("https://pay.example.com/connector"); }},
      {"pay.full",
       [] {
         return fr("").pay(
             "https://pay.example.com/connector",
             /*input_method=*/"dtmf",
             /*status_url=*/"https://ex.com/status",
             /*payment_method=*/"credit-card",
             /*timeout=*/7,
             /*max_attempts=*/2,
             /*security_code=*/false,
             /*postal_code=*/"90210",
             /*min_postal_code_length=*/5,
             /*token_type=*/"one-time",
             /*charge_amount=*/"9.99",
             /*currency=*/"usd",
             /*language=*/"en-US",
             /*voice=*/"woman",
             /*description=*/"Order 42",
             /*valid_card_types=*/"visa amex",
             /*parameters=*/{json::object({{"name", "order_id"}, {"value", "42"}})},
             /*prompts=*/
             {json::object(
                 {{"for", "payment-card-number"},
                  {"actions", json::array({json::object(
                                  {{"type", "Say"}, {"phrase", "Enter your card number"}})})},
                  {"card_type", "visa amex"}})},
             /*ai_response=*/kPayAiResponse);
       }},
      // postal_code=True in Python -> "true"; C++'s default postal_code is
      // already the string "true", so the bare call reproduces it.
      {"pay.postal_bool", [] { return fr("").pay("https://pay.example.com/connector"); }},

      // ---- record_call (incl. mp4 + each direction) -----------------------
      // Use the typed RecordFormat/RecordDirection enum overload (proves the
      // typed path emits byte-identically to the bare-string path).
      {"record_call.defaults",
       [] { return fr("").record_call("", false, RecordFormat::Wav, RecordDirection::Both); }},
      {"record_call.wav_speak",
       [] { return fr("").record_call("", false, RecordFormat::Wav, RecordDirection::Speak); }},
      {"record_call.mp3_listen",
       [] { return fr("").record_call("", false, RecordFormat::Mp3, RecordDirection::Listen); }},
      {"record_call.mp4_both",
       [] { return fr("").record_call("", false, RecordFormat::Mp4, RecordDirection::Both); }},
      {"record_call.full",
       [] {
         return fr("").record_call("rec1", /*stereo=*/true, /*format=*/"mp3",
                                   /*direction=*/"both", /*terminators=*/"#",
                                   /*beep=*/true, /*input_sensitivity=*/30.0,
                                   /*initial_timeout=*/5.0, /*end_silence_timeout=*/3.0,
                                   /*max_length=*/120.0, /*status_url=*/"https://ex.com/rec");
       }},
      {"stop_record_call.bare", [] { return fr("").stop_record_call(); }},
      {"stop_record_call.id", [] { return fr("").stop_record_call("rec1"); }},

      // ---- tap (each direction / codec) -----------------------------------
      // Typed TapDirection/Codec overload where the corpus sets a non-default
      // direction/codec; the defaults entry uses the bare-string overload
      // (control_id="" => the std::string overload's defaults).
      {"tap.defaults", [] { return fr("").tap("rtp://10.0.0.1:5004"); }},
      {"tap.speak_pcma",
       [] { return fr("").tap("ws://ex.com/tap", "", TapDirection::Speak, Codec::Pcma); }},
      {"tap.hear_pcmu",
       [] { return fr("").tap("wss://ex.com/tap", "", TapDirection::Hear, Codec::Pcmu); }},
      {"tap.both_full",
       [] {
         return fr("").tap("rtp://10.0.0.1:5004", "tap1", TapDirection::Both, Codec::Pcma, 40,
                           "https://ex.com/tapstatus");
       }},
      {"stop_tap.bare", [] { return fr("").stop_tap(); }},
      {"stop_tap.id", [] { return fr("").stop_tap("tap1"); }},

      // ---- join_conference (simple + full) --------------------------------
      {"join_conference.simple", [] { return fr("").join_conference("sales-floor"); }},
      {"join_conference.full",
       [] {
         // Flat positional overload — mirrors the Python signature 1:1, so
         // the corpus kwargs line up positionally.
         return fr("").join_conference(
             "sales-floor", /*muted=*/true, /*beep=*/"onEnter",
             /*start_on_enter=*/false, /*end_on_exit=*/true,
             /*wait_url=*/std::string("https://ex.com/hold"), /*max_participants=*/50,
             /*record=*/"record-from-start", /*region=*/std::string("us-east"),
             /*trim=*/"do-not-trim", /*coach=*/std::string("call-123"),
             /*status_callback_event=*/std::string("start end join leave"),
             /*status_callback=*/std::string("https://ex.com/cb"),
             /*status_callback_method=*/"GET",
             /*recording_status_callback=*/std::string("https://ex.com/rcb"),
             /*recording_status_callback_method=*/"GET",
             /*recording_status_callback_event=*/"in-progress completed");
       }},

      // ---- execute_rpc + the three rpc helpers ----------------------------
      {"execute_rpc.minimal", [] { return fr("").execute_rpc("ai_unhold"); }},
      {"execute_rpc.full",
       [] {
         return fr("").execute_rpc("ai_message",
                                   json::object({{"role", "system"}, {"message_text", "Hello"}}),
                                   "call-abc", "node-1");
       }},
      {"rpc_dial",
       [] { return fr("").rpc_dial("+15551234567", "+15559876543", "https://ex.com/call-agent"); }},
      {"rpc_ai_message",
       [] { return fr("").rpc_ai_message("call-abc", "Please take a message."); }},
      {"rpc_ai_unhold", [] { return fr("").rpc_ai_unhold("call-abc"); }},

      // ---- simulate_user_input --------------------------------------------
      {"simulate_user_input",
       [] { return fr("").simulate_user_input("I'd like to pay my bill."); }},

      // ---- dynamic hints --------------------------------------------------
      {"add_dynamic_hints",
       [] {
         return fr("").add_dynamic_hints(json::array(
             {"Cabby",
              json::object(
                  {{"pattern", "cab bee"}, {"replace", "Cabby"}, {"ignore_case", true}})}));
       }},
      {"clear_dynamic_hints", [] { return fr("").clear_dynamic_hints(); }},

      // ---- toggle_functions / functions-on-timeout ------------------------
      {"toggle_functions",
       [] {
         return fr("").toggle_functions(
             json::array({json::object({{"function", "transfer"}, {"active", false}}),
                          json::object({{"function", "lookup"}, {"active", true}})}));
       }},
      {"functions_on_speaker_timeout.true", [] { return fr("").enable_functions_on_timeout(); }},
      {"functions_on_speaker_timeout.false",
       [] { return fr("").enable_functions_on_timeout(false); }},

      // ---- extensive_data -------------------------------------------------
      {"extensive_data.true", [] { return fr("").enable_extensive_data(); }},
      {"extensive_data.false", [] { return fr("").enable_extensive_data(false); }},

      // ---- replace_in_history (str + bool) --------------------------------
      {"replace_in_history.bool", [] { return fr("").replace_in_history(json(true)); }},
      {"replace_in_history.str",
       [] { return fr("").replace_in_history(json("Summarized the order.")); }},

      // ---- settings -------------------------------------------------------
      {"settings",
       [] {
         return fr("").update_settings({{"temperature", 0.7}, {"max-tokens", 256}, {"top-p", 0.9}});
       }},

      // ---- speech timeouts ------------------------------------------------
      {"end_of_speech_timeout", [] { return fr("").set_end_of_speech_timeout(800); }},
      {"speech_event_timeout", [] { return fr("").set_speech_event_timeout(1200); }},

      // ---- execute_swml (dict + JSON-string + transfer) -------------------
      {"execute_swml.dict",
       [] {
         return fr("").execute_swml(json::object(
             {{"version", "1.0.0"},
              {"sections", json::object({{"main", json::array({json::object(
                                                      {{"answer", json::object()}})})}})}}));
       }},
      {"execute_swml.dict_transfer",
       [] {
         return fr("").execute_swml(
             json::object(
                 {{"version", "1.0.0"},
                  {"sections", json::object({{"main", json::array({json::object(
                                                          {{"answer", json::object()}})})}})}}),
             true);
       }},
      {"execute_swml.json_string",
       [] {
         // Python's execute_swml parses a JSON string into a dict before
         // wrapping; C++ does the same. Pass the raw JSON text.
         return fr("").execute_swml(
             json("{\"version\": \"1.0.0\", \"sections\": {\"main\": [{\"hangup\": {}}]}}"));
       }},
  };
}

}  // namespace

int main() {
  json out = json::object();
  std::vector<std::string> seen;
  for (auto& e : corpus()) {
    for (const auto& s : seen) {
      if (s == e.id) {
        std::cerr << "emit_corpus: duplicate corpus id " << e.id << "\n";
        return 1;
      }
    }
    seen.push_back(e.id);
    out[e.id] = e.build().to_json();
  }

  // nlohmann sorts object keys -> canonical JSON. dump() without indent for a
  // single compact line; ensure_ascii is off by default so '+'/'&' stay literal
  // (matches Python's json output, like Go's SetEscapeHTML(false)).
  std::cout << out.dump() << "\n";
  return 0;
}
