#pragma once

#include <string>
#include <vector>
#include <optional>
#include <nlohmann/json.hpp>

namespace signalwire {
namespace swaig {

using json = nlohmann::json;

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
    FunctionResult& join_conference(const std::string& name, bool muted = false,
                                     const std::string& beep = "true");
    FunctionResult& join_room(const std::string& name);
    FunctionResult& sip_refer(const std::string& to_uri);
    FunctionResult& tap(const std::string& uri, const std::string& control_id = "",
                        const std::string& direction = "both",
                        const std::string& codec = "PCMU",
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
