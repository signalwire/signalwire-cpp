#pragma once

#include <string>
#include <vector>
#include <optional>
#include <nlohmann/json.hpp>

namespace signalwire {
namespace swaig {

using json = nlohmann::json;

// ===========================================================================
// join_conference closed-set enums
//
// Mirrors the Gender / SkillName affordance pattern: each closed set the
// Python reference documents as a bare string gets a typed `enum class` so a
// typo fails at the call site instead of on the server. The string form still
// works (Python uses bare `str`/`Optional[str]`), so engine-/CXML-specific
// values that aren't one of the canonical members remain expressible. The
// `*_value()` helpers are the single normalization point — enum and string
// emit the identical wire string.
// ===========================================================================

/// `beep` — when the conference plays an enter/leave tone.
enum class ConferenceBeep { True, False, OnEnter, OnExit };
/// `record` — conference recording mode.
enum class ConferenceRecord { DoNotRecord, RecordFromStart };
/// `trim` — leading/trailing silence handling on recordings.
enum class ConferenceTrim { TrimSilence, DoNotTrim };
/// `status_callback_method` / `recording_status_callback_method` — HTTP verb.
enum class CallbackMethod { Get, Post };

inline std::string conference_beep_value(ConferenceBeep v) {
    switch (v) {
        case ConferenceBeep::True:    return "true";
        case ConferenceBeep::False:   return "false";
        case ConferenceBeep::OnEnter: return "onEnter";
        case ConferenceBeep::OnExit:  return "onExit";
    }
    return "";
}
inline std::string conference_record_value(ConferenceRecord v) {
    switch (v) {
        case ConferenceRecord::DoNotRecord:     return "do-not-record";
        case ConferenceRecord::RecordFromStart: return "record-from-start";
    }
    return "";
}
inline std::string conference_trim_value(ConferenceTrim v) {
    switch (v) {
        case ConferenceTrim::TrimSilence: return "trim-silence";
        case ConferenceTrim::DoNotTrim:   return "do-not-trim";
    }
    return "";
}
inline std::string callback_method_value(CallbackMethod v) {
    switch (v) {
        case CallbackMethod::Get:  return "GET";
        case CallbackMethod::Post: return "POST";
    }
    return "";
}

// ===========================================================================
// record_call / tap media closed-set enums
//
// Same SkillName / Gender / join_conference affordance: the Python reference
// validates each of these against a fixed set in `core/function_result.py`
// (`raise ValueError` on a miss), so a typo only fails at runtime on the
// server. The typed `enum class` makes the typo fail at the call site with
// editor autocompletion, while the bare-`std::string` overloads stay canonical
// (parity with Python's bare `str`). The `*_value()` helpers are the single
// normalization point — the enum overload routes its wire string into the
// EXACT string method, so the emitted SWML is byte-identical.
//
// ★ Three direction vocabularies + two codec vocabularies that must NEVER be
// unified (they are bug generators): record_call's direction is
// {speak,listen,both}, tap's direction is {speak,hear,both} (`hear`, not
// `listen`), and tap's codec is the 2-value SWAIG set {PCMU,PCMA} — NOT the
// wider RELAY connect/stream codec superset. Each set gets its OWN enum,
// faithfully mirroring the reference's separate validation lists.
// ===========================================================================

/// Recording container format for `FunctionResult::record_call`.
/// Mirrors the reference's `format in {"wav","mp3","mp4"}` validation.
enum class RecordFormat { Wav, Mp3, Mp4 };

/// Audio direction for `FunctionResult::record_call`.
/// Mirrors the reference's `direction in {"speak","listen","both"}` validation.
/// NOTE: differs from `TapDirection` — record_call uses `listen`, tap uses `hear`.
enum class RecordDirection { Speak, Listen, Both };

/// Audio direction for `FunctionResult::tap`.
/// Mirrors the reference's `direction in {"speak","hear","both"}` validation.
/// NOTE: differs from `RecordDirection` — tap uses `hear`, record_call uses `listen`.
enum class TapDirection { Speak, Hear, Both };

/// Media codec for `FunctionResult::tap` (SWAIG tap only).
/// Mirrors the reference's `codec in {"PCMU","PCMA"}` validation. The wire
/// strings are upper-case. Distinct from the wider RELAY codec set — do not unify.
enum class Codec { Pcmu, Pcma };

inline std::string record_format_value(RecordFormat v) {
    switch (v) {
        case RecordFormat::Wav: return "wav";
        case RecordFormat::Mp3: return "mp3";
        case RecordFormat::Mp4: return "mp4";
    }
    return "";
}
inline std::string record_direction_value(RecordDirection v) {
    switch (v) {
        case RecordDirection::Speak:  return "speak";
        case RecordDirection::Listen: return "listen";
        case RecordDirection::Both:   return "both";
    }
    return "";
}
inline std::string tap_direction_value(TapDirection v) {
    switch (v) {
        case TapDirection::Speak: return "speak";
        case TapDirection::Hear:  return "hear";
        case TapDirection::Both:  return "both";
    }
    return "";
}
inline std::string codec_value(Codec v) {
    switch (v) {
        case Codec::Pcmu: return "PCMU";
        case Codec::Pcma: return "PCMA";
    }
    return "";
}

/// ADL `to_string` overloads so these enums stringify the same way the
/// `*_value()` mappers do (the single wire-string normalization point).
inline std::string to_string(RecordFormat v)    { return record_format_value(v); }
inline std::string to_string(RecordDirection v) { return record_direction_value(v); }
inline std::string to_string(TapDirection v)    { return tap_direction_value(v); }
inline std::string to_string(Codec v)           { return codec_value(v); }

/// A closed-set field that accepts EITHER the typed enum OR a bare string.
///
/// `JoinConferenceOptions::beep = ConferenceBeep::OnEnter;` and
/// `... = "onEnter";` both compile and resolve to the same wire string; the
/// open-string path keeps parity with Python's bare `str` (the validation in
/// `join_conference` then rejects out-of-set strings exactly as Python does).
/// Templated on the enum type plus its `*_value()` mapper so one definition
/// covers all four sets.
template <typename E, std::string (*Map)(E)>
struct EnumOrString {
    std::string value;
    EnumOrString(E e) : value(Map(e)) {}                 // NOLINT(google-explicit-constructor)
    EnumOrString(const std::string& s) : value(s) {}     // NOLINT
    EnumOrString(const char* s) : value(s) {}            // NOLINT
    const std::string& str() const { return value; }
};

using BeepField   = EnumOrString<ConferenceBeep, &conference_beep_value>;
using RecordField = EnumOrString<ConferenceRecord, &conference_record_value>;
using TrimField   = EnumOrString<ConferenceTrim, &conference_trim_value>;
using MethodField = EnumOrString<CallbackMethod, &callback_method_value>;

/// Options bag for `FunctionResult::join_conference`.
///
/// Every field is `std::optional` and unset means "Python default" — so a
/// default-constructed `JoinConferenceOptions` collapses to the bare
/// conference-name string form, matching the reference's simple case. Closed
/// sets use the enum-or-string wrapper above; open fields are plain
/// `std::optional`. `result` is a free-form `json` (Python's `Optional[Any]`).
struct JoinConferenceOptions {
    std::optional<bool>        muted;
    std::optional<BeepField>   beep;
    std::optional<bool>        start_on_enter;
    std::optional<bool>        end_on_exit;
    std::optional<std::string> wait_url;
    std::optional<int>         max_participants;
    std::optional<RecordField> record;
    std::optional<std::string> region;
    std::optional<TrimField>   trim;
    std::optional<std::string> coach;
    std::optional<std::string> status_callback_event;
    std::optional<std::string> status_callback;
    std::optional<MethodField> status_callback_method;
    std::optional<std::string> recording_status_callback;
    std::optional<MethodField> recording_status_callback_method;
    std::optional<std::string> recording_status_callback_event;
    std::optional<json>        result;
};

/// Builder for SWAIG function results with 40+ action methods.
/// Every method returns *this for chaining.
class FunctionResult {
public:
    explicit FunctionResult(const std::string& response = "", bool post_process = false);

    // ========================================================================
    // Core
    // ========================================================================

    FunctionResult& set_response(const std::string& response);
    FunctionResult& set_post_process(bool pp);
    FunctionResult& add_action(const std::string& name, const json& data);
    FunctionResult& add_actions(const std::vector<json>& actions);

    // ========================================================================
    // Call Control
    // ========================================================================

    FunctionResult& connect(const std::string& destination, bool final = true,
                            const std::string& from_addr = "");
    FunctionResult& swml_transfer(const std::string& dest, const std::string& ai_response,
                                   bool final = true);
    FunctionResult& hangup();
    FunctionResult& hold(int timeout = 300);
    FunctionResult& wait_for_user(std::optional<bool> enabled = std::nullopt,
                                   std::optional<int> timeout = std::nullopt,
                                   bool answer_first = false);
    FunctionResult& stop();

    // ========================================================================
    // State & Data
    // ========================================================================

    FunctionResult& update_global_data(const json& data);
    FunctionResult& remove_global_data(const json& keys);
    FunctionResult& set_metadata(const json& data);
    FunctionResult& remove_metadata(const json& keys);
    FunctionResult& swml_user_event(const json& event_data);
    FunctionResult& swml_change_step(const std::string& step_name);
    FunctionResult& swml_change_context(const std::string& context_name);
    FunctionResult& switch_context(const std::string& system_prompt = "",
                                    const std::string& user_prompt = "",
                                    bool consolidate = false,
                                    bool full_reset = false);
    FunctionResult& replace_in_history(const json& text);

    // ========================================================================
    // Media
    // ========================================================================

    FunctionResult& say(const std::string& text);
    FunctionResult& play_background_file(const std::string& filename, bool wait = false);
    FunctionResult& stop_background_file();
    FunctionResult& record_call(const std::string& control_id = "",
                                 bool stereo = false,
                                 const std::string& format = "wav",
                                 const std::string& direction = "both",
                                 const std::string& terminators = "",
                                 bool beep = false,
                                 double input_sensitivity = 44.0,
                                 std::optional<double> initial_timeout = std::nullopt,
                                 std::optional<double> end_silence_timeout = std::nullopt,
                                 std::optional<double> max_length = std::nullopt,
                                 const std::string& status_url = "");

    /// Typed overload — `format`/`direction` as the `RecordFormat` /
    /// `RecordDirection` closed-set enums for call-site typo checking. Declared
    /// after the std::string overload (equal arity) so the enumerator's dedup
    /// keeps the string signature canonical; normalizes the enums to their wire
    /// strings via `record_format_value`/`record_direction_value` and delegates
    /// to the std::string `record_call`, so the emitted SWML is byte-identical.
    FunctionResult& record_call(const std::string& control_id,
                                 bool stereo,
                                 RecordFormat format,
                                 RecordDirection direction,
                                 const std::string& terminators = "",
                                 bool beep = false,
                                 double input_sensitivity = 44.0,
                                 std::optional<double> initial_timeout = std::nullopt,
                                 std::optional<double> end_silence_timeout = std::nullopt,
                                 std::optional<double> max_length = std::nullopt,
                                 const std::string& status_url = "");
    FunctionResult& stop_record_call(const std::string& control_id = "");

    // ========================================================================
    // Speech & AI
    // ========================================================================

    FunctionResult& add_dynamic_hints(const json& hints);
    FunctionResult& clear_dynamic_hints();
    FunctionResult& set_end_of_speech_timeout(int milliseconds);
    FunctionResult& set_speech_event_timeout(int milliseconds);
    FunctionResult& toggle_functions(const json& function_toggles);
    FunctionResult& enable_functions_on_timeout(bool enabled = true);
    FunctionResult& enable_extensive_data(bool enabled = true);
    FunctionResult& update_settings(const json& settings);
    FunctionResult& simulate_user_input(const std::string& text);

    // ========================================================================
    // Advanced / SWML
    // ========================================================================

    FunctionResult& execute_swml(const json& swml_content, bool transfer = false);

    /// Join an ad-hoc audio conference (SWML `join_conference`). Full parity
    /// with Python `core/function_result.py`: 18 optional params past `name`,
    /// 7 validations, and simple (bare-name) vs full-object emission.
    ///
    /// Flat positional overload — mirrors the Python signature 1:1 so the
    /// cross-language audit lines up on parameter count/types. The closed-set
    /// params are bare `std::string` (Python uses bare `str`); the
    /// options-struct overload below adds the typed `enum class` affordance.
    FunctionResult& join_conference(
        const std::string& name,
        bool muted = false,
        const std::string& beep = "true",
        bool start_on_enter = true,
        bool end_on_exit = false,
        std::optional<std::string> wait_url = std::nullopt,
        int max_participants = 250,
        const std::string& record = "do-not-record",
        std::optional<std::string> region = std::nullopt,
        const std::string& trim = "trim-silence",
        std::optional<std::string> coach = std::nullopt,
        std::optional<std::string> status_callback_event = std::nullopt,
        std::optional<std::string> status_callback = std::nullopt,
        const std::string& status_callback_method = "POST",
        std::optional<std::string> recording_status_callback = std::nullopt,
        const std::string& recording_status_callback_method = "POST",
        const std::string& recording_status_callback_event = "completed",
        std::optional<json> result = std::nullopt);

    /// Options-bag overload — the C++-idiomatic way to pass the 18 optional
    /// params (named `std::optional` fields + closed-set enums). Delegates to
    /// the flat overload, so behavior + validation + emission are identical.
    FunctionResult& join_conference(const std::string& name,
                                    const JoinConferenceOptions& opts);
    FunctionResult& join_room(const std::string& name);
    FunctionResult& sip_refer(const std::string& to_uri);
    FunctionResult& tap(const std::string& uri, const std::string& control_id = "",
                        const std::string& direction = "both",
                        const std::string& codec = "PCMU",
                        int rtp_ptime = 20,
                        const std::string& status_url = "");

    /// Typed overload — `direction`/`codec` as the `TapDirection` / `Codec`
    /// closed-set enums for call-site typo checking. Declared after the
    /// std::string overload (equal arity) so the enumerator's dedup keeps the
    /// string signature canonical; normalizes the enums via
    /// `tap_direction_value`/`codec_value` and delegates to the std::string
    /// `tap`, so the emitted SWML is byte-identical. NOTE the tap direction set
    /// is {speak,hear,both} (`hear`, not record_call's `listen`).
    FunctionResult& tap(const std::string& uri,
                        const std::string& control_id,
                        TapDirection direction,
                        Codec codec,
                        int rtp_ptime = 20,
                        const std::string& status_url = "");
    FunctionResult& stop_tap(const std::string& control_id = "");
    FunctionResult& send_sms(const std::string& to, const std::string& from,
                              const std::string& body = "",
                              const std::vector<std::string>& media = {},
                              const std::vector<std::string>& tags = {},
                              const std::string& region = "");
    FunctionResult& pay(const std::string& payment_connector_url,
                         const std::string& input_method = "dtmf",
                         const std::string& status_url = "",
                         const std::string& payment_method = "credit-card",
                         int timeout = 5,
                         int max_attempts = 1,
                         bool security_code = true,
                         const std::string& postal_code = "true",
                         int min_postal_code_length = 0,
                         const std::string& token_type = "reusable",
                         const std::string& charge_amount = "",
                         const std::string& currency = "usd",
                         const std::string& language = "en-US",
                         const std::string& voice = "woman",
                         const std::string& description = "",
                         const std::string& valid_card_types = "visa mastercard amex",
                         const std::vector<json>& parameters = {},
                         const std::vector<json>& prompts = {},
                         const std::string& ai_response =
                             "The payment status is ${pay_result}, do not mention anything else about collecting payment if successful.");

    // ========================================================================
    // RPC
    // ========================================================================

    FunctionResult& execute_rpc(const std::string& method, const json& params = json::object(),
                                 const std::string& call_id = "",
                                 const std::string& node_id = "");
    FunctionResult& rpc_dial(const std::string& to_number, const std::string& from_number,
                              const std::string& dest_swml,
                              const std::string& device_type = "phone");
    FunctionResult& rpc_ai_message(const std::string& call_id, const std::string& message_text,
                                    const std::string& role = "system");
    FunctionResult& rpc_ai_unhold(const std::string& call_id);

    // ========================================================================
    // Static Payment Helpers
    // ========================================================================

    static json create_payment_prompt(const std::string& for_situation,
                                       const std::vector<json>& actions,
                                       const std::string& card_type = "",
                                       const std::string& error_type = "");
    static json create_payment_action(const std::string& action_type,
                                       const std::string& phrase);
    static json create_payment_parameter(const std::string& name,
                                          const std::string& value);

    // ========================================================================
    // Serialization
    // ========================================================================

    json to_json() const;
    std::string to_string(int indent = -1) const;

private:
    std::string response_;
    std::vector<json> actions_;
    bool post_process_;
};

} // namespace swaig
} // namespace signalwire
