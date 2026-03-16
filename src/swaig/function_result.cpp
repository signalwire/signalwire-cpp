#include "signalwire/swaig/function_result.hpp"
#include <stdexcept>

namespace signalwire {
namespace swaig {

FunctionResult::FunctionResult(const std::string& response, bool post_process)
    : response_(response), post_process_(post_process) {}

// ========================================================================
// Core
// ========================================================================

FunctionResult& FunctionResult::set_response(const std::string& response) {
    response_ = response;
    return *this;
}

FunctionResult& FunctionResult::set_post_process(bool pp) {
    post_process_ = pp;
    return *this;
}

FunctionResult& FunctionResult::add_action(const std::string& name, const json& data) {
    actions_.push_back(json::object({{name, data}}));
    return *this;
}

FunctionResult& FunctionResult::add_actions(const std::vector<json>& actions) {
    for (const auto& a : actions) {
        actions_.push_back(a);
    }
    return *this;
}

// ========================================================================
// Call Control
// ========================================================================

FunctionResult& FunctionResult::connect(const std::string& destination, bool final_val,
                                         const std::string& from_addr) {
    json connect_params;
    connect_params["to"] = destination;
    if (!from_addr.empty()) {
        connect_params["from"] = from_addr;
    }

    json swml_action;
    swml_action["SWML"] = {
        {"sections", {{"main", json::array({json::object({{"connect", connect_params}})})}}},
        {"version", "1.0.0"}
    };
    swml_action["transfer"] = final_val ? "true" : "false";

    actions_.push_back(swml_action);
    return *this;
}

FunctionResult& FunctionResult::swml_transfer(const std::string& dest,
                                               const std::string& ai_response,
                                               bool final_val) {
    json swml_action;
    swml_action["SWML"] = {
        {"version", "1.0.0"},
        {"sections", {{"main", json::array({
            json::object({{"set", json::object({{"ai_response", ai_response}})}}),
            json::object({{"transfer", json::object({{"dest", dest}})}})
        })}}}
    };
    swml_action["transfer"] = final_val ? "true" : "false";

    actions_.push_back(swml_action);
    return *this;
}

FunctionResult& FunctionResult::hangup() {
    return add_action("hangup", json(true));
}

FunctionResult& FunctionResult::hold(int timeout) {
    int clamped = std::max(0, std::min(timeout, 900));
    return add_action("hold", json(clamped));
}

FunctionResult& FunctionResult::wait_for_user(std::optional<bool> enabled,
                                               std::optional<int> timeout,
                                               bool answer_first) {
    if (answer_first) {
        return add_action("wait_for_user", json("answer_first"));
    } else if (timeout.has_value()) {
        return add_action("wait_for_user", json(*timeout));
    } else if (enabled.has_value()) {
        return add_action("wait_for_user", json(*enabled));
    } else {
        return add_action("wait_for_user", json(true));
    }
}

FunctionResult& FunctionResult::stop() {
    return add_action("stop", json(true));
}

// ========================================================================
// State & Data
// ========================================================================

FunctionResult& FunctionResult::update_global_data(const json& data) {
    return add_action("set_global_data", data);
}

FunctionResult& FunctionResult::remove_global_data(const json& keys) {
    return add_action("unset_global_data", keys);
}

FunctionResult& FunctionResult::set_metadata(const json& data) {
    return add_action("set_meta_data", data);
}

FunctionResult& FunctionResult::remove_metadata(const json& keys) {
    return add_action("unset_meta_data", keys);
}

FunctionResult& FunctionResult::swml_user_event(const json& event_data) {
    json swml_action = {
        {"sections", {{"main", json::array({
            json::object({{"user_event", json::object({{"event", event_data}})}})
        })}}},
        {"version", "1.0.0"}
    };
    return add_action("SWML", swml_action);
}

FunctionResult& FunctionResult::swml_change_step(const std::string& step_name) {
    return add_action("change_step", json(step_name));
}

FunctionResult& FunctionResult::swml_change_context(const std::string& context_name) {
    return add_action("change_context", json(context_name));
}

FunctionResult& FunctionResult::switch_context(const std::string& system_prompt,
                                                const std::string& user_prompt,
                                                bool consolidate,
                                                bool full_reset) {
    if (!system_prompt.empty() && user_prompt.empty() && !consolidate && !full_reset) {
        return add_action("context_switch", json(system_prompt));
    }

    json context_data;
    if (!system_prompt.empty()) context_data["system_prompt"] = system_prompt;
    if (!user_prompt.empty()) context_data["user_prompt"] = user_prompt;
    if (consolidate) context_data["consolidate"] = true;
    if (full_reset) context_data["full_reset"] = true;
    return add_action("context_switch", context_data);
}

FunctionResult& FunctionResult::replace_in_history(const json& text) {
    return add_action("replace_in_history", text);
}

// ========================================================================
// Media
// ========================================================================

FunctionResult& FunctionResult::say(const std::string& text) {
    return add_action("say", json(text));
}

FunctionResult& FunctionResult::play_background_file(const std::string& filename, bool wait) {
    if (wait) {
        return add_action("playback_bg", json::object({{"file", filename}, {"wait", true}}));
    }
    return add_action("playback_bg", json(filename));
}

FunctionResult& FunctionResult::stop_background_file() {
    return add_action("stop_playback_bg", json(true));
}

FunctionResult& FunctionResult::record_call(const std::string& control_id,
                                              bool stereo, const std::string& format,
                                              const std::string& direction,
                                              const std::string& terminators,
                                              bool beep, double input_sensitivity,
                                              std::optional<double> initial_timeout,
                                              std::optional<double> end_silence_timeout,
                                              std::optional<double> max_length,
                                              const std::string& status_url) {
    json record_params;
    record_params["stereo"] = stereo;
    record_params["format"] = format;
    record_params["direction"] = direction;
    record_params["beep"] = beep;
    record_params["input_sensitivity"] = input_sensitivity;

    if (!control_id.empty()) record_params["control_id"] = control_id;
    if (!terminators.empty()) record_params["terminators"] = terminators;
    if (initial_timeout.has_value()) record_params["initial_timeout"] = *initial_timeout;
    if (end_silence_timeout.has_value()) record_params["end_silence_timeout"] = *end_silence_timeout;
    if (max_length.has_value()) record_params["max_length"] = *max_length;
    if (!status_url.empty()) record_params["status_url"] = status_url;

    json swml_doc = {
        {"version", "1.0.0"},
        {"sections", {{"main", json::array({json::object({{"record_call", record_params}})})}}}
    };
    return execute_swml(swml_doc);
}

FunctionResult& FunctionResult::stop_record_call(const std::string& control_id) {
    json stop_params = json::object();
    if (!control_id.empty()) {
        stop_params["control_id"] = control_id;
    }

    json swml_doc = {
        {"version", "1.0.0"},
        {"sections", {{"main", json::array({json::object({{"stop_record_call", stop_params}})})}}}
    };
    return execute_swml(swml_doc);
}

// ========================================================================
// Speech & AI
// ========================================================================

FunctionResult& FunctionResult::add_dynamic_hints(const json& hints) {
    return add_action("add_dynamic_hints", hints);
}

FunctionResult& FunctionResult::clear_dynamic_hints() {
    actions_.push_back(json::object({{"clear_dynamic_hints", json::object()}}));
    return *this;
}

FunctionResult& FunctionResult::set_end_of_speech_timeout(int ms) {
    return add_action("end_of_speech_timeout", json(ms));
}

FunctionResult& FunctionResult::set_speech_event_timeout(int ms) {
    return add_action("speech_event_timeout", json(ms));
}

FunctionResult& FunctionResult::toggle_functions(const json& function_toggles) {
    return add_action("toggle_functions", function_toggles);
}

FunctionResult& FunctionResult::enable_functions_on_timeout(bool enabled) {
    return add_action("functions_on_speaker_timeout", json(enabled));
}

FunctionResult& FunctionResult::enable_extensive_data(bool enabled) {
    return add_action("extensive_data", json(enabled));
}

FunctionResult& FunctionResult::update_settings(const json& settings) {
    return add_action("settings", settings);
}

FunctionResult& FunctionResult::simulate_user_input(const std::string& text) {
    return add_action("user_input", json(text));
}

// ========================================================================
// Advanced / SWML
// ========================================================================

FunctionResult& FunctionResult::execute_swml(const json& swml_content, bool transfer) {
    json action = swml_content;
    if (transfer) {
        action["transfer"] = "true";
    }
    return add_action("SWML", action);
}

FunctionResult& FunctionResult::join_conference(const std::string& name, bool muted,
                                                 const std::string& beep) {
    json join_params;
    if (!muted && beep == "true") {
        // Simple form
        json swml_doc = {
            {"version", "1.0.0"},
            {"sections", {{"main", json::array({json::object({{"join_conference", json(name)}})})}}}
        };
        return execute_swml(swml_doc);
    }

    join_params["name"] = name;
    if (muted) join_params["muted"] = true;
    if (beep != "true") join_params["beep"] = beep;

    json swml_doc = {
        {"version", "1.0.0"},
        {"sections", {{"main", json::array({json::object({{"join_conference", join_params}})})}}}
    };
    return execute_swml(swml_doc);
}

FunctionResult& FunctionResult::join_room(const std::string& name) {
    json swml_doc = {
        {"version", "1.0.0"},
        {"sections", {{"main", json::array({json::object({{"join_room", json::object({{"name", name}})}})})}}}
    };
    return execute_swml(swml_doc);
}

FunctionResult& FunctionResult::sip_refer(const std::string& to_uri) {
    json swml_doc = {
        {"version", "1.0.0"},
        {"sections", {{"main", json::array({json::object({{"sip_refer", json::object({{"to_uri", to_uri}})}})})}}}
    };
    return execute_swml(swml_doc);
}

FunctionResult& FunctionResult::tap(const std::string& uri,
                                     const std::string& control_id,
                                     const std::string& direction,
                                     const std::string& codec,
                                     int rtp_ptime,
                                     const std::string& status_url) {
    json tap_params;
    tap_params["uri"] = uri;
    if (!control_id.empty()) tap_params["control_id"] = control_id;
    if (direction != "both") tap_params["direction"] = direction;
    if (codec != "PCMU") tap_params["codec"] = codec;
    if (rtp_ptime != 20) tap_params["rtp_ptime"] = rtp_ptime;
    if (!status_url.empty()) tap_params["status_url"] = status_url;

    json swml_doc = {
        {"version", "1.0.0"},
        {"sections", {{"main", json::array({json::object({{"tap", tap_params}})})}}}
    };
    return execute_swml(swml_doc);
}

FunctionResult& FunctionResult::stop_tap(const std::string& control_id) {
    json stop_params = json::object();
    if (!control_id.empty()) {
        stop_params["control_id"] = control_id;
    }

    json swml_doc = {
        {"version", "1.0.0"},
        {"sections", {{"main", json::array({json::object({{"stop_tap", stop_params}})})}}}
    };
    return execute_swml(swml_doc);
}

FunctionResult& FunctionResult::send_sms(const std::string& to, const std::string& from,
                                           const std::string& body,
                                           const std::vector<std::string>& media,
                                           const std::vector<std::string>& tags,
                                           const std::string& region) {
    if (body.empty() && media.empty()) {
        throw std::invalid_argument("Either body or media must be provided");
    }

    json sms_params;
    sms_params["to_number"] = to;
    sms_params["from_number"] = from;
    if (!body.empty()) sms_params["body"] = body;
    if (!media.empty()) sms_params["media"] = media;
    if (!tags.empty()) sms_params["tags"] = tags;
    if (!region.empty()) sms_params["region"] = region;

    json swml_doc = {
        {"version", "1.0.0"},
        {"sections", {{"main", json::array({json::object({{"send_sms", sms_params}})})}}}
    };
    return execute_swml(swml_doc);
}

FunctionResult& FunctionResult::pay(const std::string& payment_connector_url,
                                     const std::string& input_method,
                                     const std::string& status_url,
                                     const std::string& payment_method,
                                     int timeout, int max_attempts,
                                     bool security_code,
                                     const std::string& postal_code,
                                     int min_postal_code_length,
                                     const std::string& token_type,
                                     const std::string& charge_amount,
                                     const std::string& currency,
                                     const std::string& language,
                                     const std::string& voice,
                                     const std::string& description_text,
                                     const std::string& valid_card_types,
                                     const std::vector<json>& parameters,
                                     const std::vector<json>& prompts,
                                     const std::string& ai_response) {
    json pay_params;
    pay_params["payment_connector_url"] = payment_connector_url;
    pay_params["input"] = input_method;
    pay_params["payment_method"] = payment_method;
    pay_params["timeout"] = std::to_string(timeout);
    pay_params["max_attempts"] = std::to_string(max_attempts);
    pay_params["security_code"] = security_code ? "true" : "false";
    pay_params["postal_code"] = postal_code;
    pay_params["min_postal_code_length"] = std::to_string(min_postal_code_length);
    pay_params["token_type"] = token_type;
    pay_params["currency"] = currency;
    pay_params["language"] = language;
    pay_params["voice"] = voice;
    pay_params["valid_card_types"] = valid_card_types;

    if (!status_url.empty()) pay_params["status_url"] = status_url;
    if (!charge_amount.empty()) pay_params["charge_amount"] = charge_amount;
    if (!description_text.empty()) pay_params["description"] = description_text;
    if (!parameters.empty()) pay_params["parameters"] = parameters;
    if (!prompts.empty()) pay_params["prompts"] = prompts;

    json swml_doc = {
        {"version", "1.0.0"},
        {"sections", {{"main", json::array({
            json::object({{"set", json::object({{"ai_response", ai_response}})}}),
            json::object({{"pay", pay_params}})
        })}}}
    };
    return execute_swml(swml_doc);
}

// ========================================================================
// RPC
// ========================================================================

FunctionResult& FunctionResult::execute_rpc(const std::string& method, const json& params,
                                             const std::string& call_id,
                                             const std::string& node_id) {
    json rpc_params = params;
    if (!call_id.empty()) rpc_params["call_id"] = call_id;
    if (!node_id.empty()) rpc_params["node_id"] = node_id;

    json rpc_cmd;
    rpc_cmd["method"] = method;
    rpc_cmd["params"] = rpc_params;

    json swml_doc = {
        {"version", "1.0.0"},
        {"sections", {{"main", json::array({json::object({{"execute_rpc", rpc_cmd}})})}}}
    };
    return execute_swml(swml_doc);
}

FunctionResult& FunctionResult::rpc_dial(const std::string& to_number,
                                          const std::string& from_number,
                                          const std::string& dest_swml,
                                          const std::string& device_type) {
    return execute_rpc("dial", {
        {"devices", json::object({
            {"type", device_type},
            {"params", json::object({
                {"to_number", to_number},
                {"from_number", from_number}
            })}
        })},
        {"dest_swml", dest_swml}
    });
}

FunctionResult& FunctionResult::rpc_ai_message(const std::string& call_id,
                                                const std::string& message_text,
                                                const std::string& role) {
    return execute_rpc("ai_message", {
        {"role", role},
        {"message_text", message_text}
    }, call_id);
}

FunctionResult& FunctionResult::rpc_ai_unhold(const std::string& call_id) {
    return execute_rpc("ai_unhold", json::object(), call_id);
}

// ========================================================================
// Static Payment Helpers
// ========================================================================

json FunctionResult::create_payment_prompt(const std::string& for_situation,
                                            const std::vector<json>& actions,
                                            const std::string& card_type,
                                            const std::string& error_type) {
    json prompt;
    prompt["for"] = for_situation;
    prompt["actions"] = actions;
    if (!card_type.empty()) prompt["card_type"] = card_type;
    if (!error_type.empty()) prompt["error_type"] = error_type;
    return prompt;
}

json FunctionResult::create_payment_action(const std::string& action_type,
                                            const std::string& phrase) {
    return json::object({{"type", action_type}, {"phrase", phrase}});
}

json FunctionResult::create_payment_parameter(const std::string& name,
                                               const std::string& value) {
    return json::object({{"name", name}, {"value", value}});
}

// ========================================================================
// Serialization
// ========================================================================

json FunctionResult::to_json() const {
    json result;

    if (!response_.empty()) {
        result["response"] = response_;
    }

    if (!actions_.empty()) {
        result["action"] = actions_;
    }

    if (post_process_ && !actions_.empty()) {
        result["post_process"] = true;
    }

    // Ensure at least one of response or action
    if (result.empty()) {
        result["response"] = "Action completed.";
    }

    return result;
}

std::string FunctionResult::to_string(int indent) const {
    return to_json().dump(indent);
}

} // namespace swaig
} // namespace signalwire
