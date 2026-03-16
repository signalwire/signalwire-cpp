// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#pragma once

#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <functional>
#include <memory>
#include <optional>
#include <mutex>
#include <shared_mutex>
#include <nlohmann/json.hpp>

#include "signalwire/swml/document.hpp"
#include "signalwire/swml/service.hpp"
#include "signalwire/swaig/function_result.hpp"
#include "signalwire/swaig/tool_definition.hpp"
#include "signalwire/security/session_manager.hpp"
#include "signalwire/contexts/contexts.hpp"
#include "signalwire/logging.hpp"

namespace httplib { class Server; class Request; class Response; }

namespace signalwire {
namespace agent {

using json = nlohmann::json;

// Forward declarations
class SkillBase;
class SkillManager;

/// POM section for structured prompts
struct PomSection {
    std::string title;
    std::string body;
    std::vector<std::string> bullets;
    std::vector<PomSection> subsections;

    json to_json() const {
        json j;
        j["title"] = title;
        if (!body.empty()) j["body"] = body;
        if (!bullets.empty()) j["bullets"] = bullets;
        if (!subsections.empty()) {
            j["subsections"] = json::array();
            for (const auto& s : subsections) {
                j["subsections"].push_back(s.to_json());
            }
        }
        return j;
    }
};

/// Language configuration
struct LanguageConfig {
    std::string name;
    std::string code;
    std::string voice;
    std::string engine;
    std::string fillers;

    json to_json() const {
        json j;
        j["name"] = name;
        j["code"] = code;
        j["voice"] = voice;
        if (!engine.empty()) j["engine"] = engine;
        if (!fillers.empty()) j["fillers"] = fillers;
        return j;
    }
};

/// Pronunciation rule
struct Pronunciation {
    std::string replace_val;
    std::string with_val;
    bool ignore_case = false;

    json to_json() const {
        json j;
        j["replace"] = replace_val;
        j["with"] = with_val;
        if (ignore_case) j["ignore_case"] = true;
        return j;
    }
};

/// Dynamic config callback type
using DynamicConfigCallback = std::function<void(
    const std::map<std::string, std::string>& query_params,
    const json& body_params,
    const std::map<std::string, std::string>& headers,
    class AgentBase& agent_copy
)>;

/// Summary callback type
using SummaryCallback = std::function<void(const json& summary, const json& raw_data)>;

/// Debug event callback type
using DebugEventCallback = std::function<void(const json& event)>;

/// SWAIG query parameters
struct SwaigQueryParam {
    std::string key;
    std::string value;
};

// ============================================================================
// AgentBase
// ============================================================================

class AgentBase {
public:
    explicit AgentBase(const std::string& name = "agent",
                       const std::string& route = "/",
                       const std::string& host = "0.0.0.0",
                       int port = 3000);
    virtual ~AgentBase();

    // Prevent copy (use clone for dynamic config)
    AgentBase(const AgentBase& other);
    AgentBase& operator=(const AgentBase&) = delete;

    // ========================================================================
    // Identity
    // ========================================================================
    const std::string& name() const { return name_; }
    AgentBase& set_name(const std::string& n) { name_ = n; return *this; }
    const std::string& route() const { return route_; }

    // ========================================================================
    // Prompt Methods (POM)
    // ========================================================================

    AgentBase& set_prompt_text(const std::string& text);
    AgentBase& set_post_prompt(const std::string& text);
    AgentBase& set_post_prompt_url(const std::string& url);
    AgentBase& prompt_add_section(const std::string& title,
                                   const std::string& body = "",
                                   const std::vector<std::string>& bullets = {});
    AgentBase& prompt_add_subsection(const std::string& parent_title,
                                      const std::string& title,
                                      const std::string& body = "",
                                      const std::vector<std::string>& bullets = {});
    AgentBase& prompt_add_to_section(const std::string& title,
                                      const std::string& body = "",
                                      const std::vector<std::string>& bullets = {});
    bool prompt_has_section(const std::string& title) const;
    std::string get_prompt() const;
    AgentBase& set_use_pom(bool use_pom);

    // ========================================================================
    // Tool Methods
    // ========================================================================

    AgentBase& define_tool(const swaig::ToolDefinition& tool);
    AgentBase& define_tool(const std::string& name, const std::string& description,
                            const json& parameters, swaig::ToolHandler handler,
                            bool secure = false);
    AgentBase& register_swaig_function(const json& func_def);
    swaig::FunctionResult on_function_call(const std::string& name,
                                            const json& args,
                                            const json& raw_data);
    bool has_tool(const std::string& name) const;
    std::vector<std::string> list_tools() const;

    // ========================================================================
    // AI Config Methods
    // ========================================================================

    AgentBase& add_hint(const std::string& hint);
    AgentBase& add_hints(const std::vector<std::string>& hints);
    AgentBase& add_pattern_hint(const std::string& pattern);
    AgentBase& add_language(const LanguageConfig& lang);
    AgentBase& set_languages(const std::vector<LanguageConfig>& langs);
    AgentBase& add_pronunciation(const std::string& replace_val,
                                  const std::string& with_val,
                                  bool ignore_case = false);
    AgentBase& set_pronunciations(const std::vector<Pronunciation>& pronuns);
    AgentBase& set_param(const std::string& key, const json& value);
    AgentBase& set_params(const json& params);
    AgentBase& set_global_data(const json& data);
    AgentBase& update_global_data(const json& data);
    AgentBase& set_native_functions(const std::vector<std::string>& funcs);
    AgentBase& set_internal_fillers(const json& fillers);
    AgentBase& add_internal_filler(const std::string& lang,
                                    const std::vector<std::string>& fillers);
    AgentBase& enable_debug_events(bool enable = true);
    AgentBase& add_function_include(const json& include);
    AgentBase& set_function_includes(const std::vector<json>& includes);
    AgentBase& set_prompt_llm_params(const json& params);
    AgentBase& set_post_prompt_llm_params(const json& params);

    // ========================================================================
    // Verb Methods (5-phase pipeline)
    // ========================================================================

    AgentBase& add_pre_answer_verb(const std::string& verb_name, const json& params);
    AgentBase& add_answer_verb(const std::string& verb_name, const json& params);
    AgentBase& add_post_answer_verb(const std::string& verb_name, const json& params);
    AgentBase& add_post_ai_verb(const std::string& verb_name, const json& params);
    AgentBase& clear_pre_answer_verbs();
    AgentBase& clear_post_answer_verbs();
    AgentBase& clear_post_ai_verbs();

    // ========================================================================
    // Context Methods
    // ========================================================================

    contexts::ContextBuilder& define_contexts();
    contexts::Context& add_context(const std::string& name);
    bool has_contexts() const;

    // ========================================================================
    // Skills Methods
    // ========================================================================

    AgentBase& add_skill(const std::string& skill_name, const json& params = json::object());
    AgentBase& remove_skill(const std::string& skill_name);
    bool has_skill(const std::string& skill_name) const;
    std::vector<std::string> list_skills() const;

    // ========================================================================
    // Web / Config Methods
    // ========================================================================

    AgentBase& set_dynamic_config_callback(DynamicConfigCallback cb);
    AgentBase& manual_set_proxy_url(const std::string& url);
    AgentBase& set_webhook_url(const std::string& url);
    AgentBase& set_post_prompt_url_direct(const std::string& url);
    AgentBase& add_swaig_query_param(const std::string& key, const std::string& value);
    AgentBase& clear_swaig_query_params();
    AgentBase& enable_debug_routes(bool enable = true);

    // ========================================================================
    // SIP Methods
    // ========================================================================

    AgentBase& enable_sip_routing(bool enable = true);
    AgentBase& register_sip_username(const std::string& username);
    AgentBase& auto_map_sip_usernames(bool enable = true);

    // ========================================================================
    // Auth
    // ========================================================================

    AgentBase& set_auth(const std::string& username, const std::string& password);
    const std::string& auth_username() const { return auth_user_; }
    const std::string& auth_password() const { return auth_pass_; }

    // ========================================================================
    // Lifecycle / Callbacks
    // ========================================================================

    AgentBase& on_summary(SummaryCallback cb);
    AgentBase& on_debug_event(DebugEventCallback cb);

    // ========================================================================
    // SWML Rendering
    // ========================================================================

    json render_swml() const;
    json render_swml_for_request(const std::map<std::string, std::string>& query_params,
                                  const json& body_params,
                                  const std::map<std::string, std::string>& headers) const;

    // ========================================================================
    // Server
    // ========================================================================

    void run();
    void serve();
    void stop();

    // Access to internal components for skills
    security::SessionManager& session_manager() { return session_manager_; }

protected:
    // Clone for dynamic config
    std::unique_ptr<AgentBase> clone() const;

    // Build the webhook URL for SWAIG functions
    std::string build_webhook_url(const std::string& base_url) const;

    // Detect proxy URL from request
    std::string detect_proxy_url(const std::map<std::string, std::string>& headers) const;

    // Build the AI verb JSON
    json build_ai_verb(const std::string& webhook_url) const;

    // Build SWAIG functions array
    json build_swaig_functions(const std::string& webhook_url) const;

    // Build the prompt
    json build_prompt() const;

    // Initialize auth
    void init_auth();

    // Setup HTTP routes
    void setup_routes(httplib::Server& server);

    // Handle SWML request
    void handle_swml_request(const httplib::Request& req, httplib::Response& res);

    // Handle SWAIG request
    void handle_swaig_request(const httplib::Request& req, httplib::Response& res);

    // Handle post_prompt request
    void handle_post_prompt_request(const httplib::Request& req, httplib::Response& res);

    // Validate basic auth
    bool validate_auth(const httplib::Request& req, httplib::Response& res) const;

    // Add security headers
    static void add_security_headers(httplib::Response& res);

    // Internal SWML rendering (used by render_swml_for_request)
    json render_swml_internal(const std::map<std::string, std::string>& headers) const;

    // ========================================================================
    // State
    // ========================================================================

    std::string name_;
    std::string route_;
    std::string host_;
    int port_;

    // Auth
    std::string auth_user_;
    std::string auth_pass_;
    bool auth_initialized_ = false;

    // Prompt
    std::optional<std::string> raw_prompt_text_;
    std::optional<std::string> post_prompt_text_;
    std::optional<std::string> post_prompt_url_;
    std::vector<PomSection> pom_sections_;
    bool use_pom_ = true;

    // Tools
    std::map<std::string, swaig::ToolDefinition> tools_;
    std::vector<std::string> tool_order_;
    std::vector<json> datamap_functions_;  // DataMap (server-side) functions
    std::vector<json> function_includes_;

    // AI Config
    std::vector<std::string> hints_;
    std::vector<LanguageConfig> languages_;
    std::vector<Pronunciation> pronunciations_;
    json ai_params_;
    json global_data_;
    std::vector<std::string> native_functions_;
    json internal_fillers_;
    bool debug_events_ = false;
    json prompt_llm_params_;
    json post_prompt_llm_params_;

    // Verbs (5-phase pipeline)
    std::vector<swml::Verb> pre_answer_verbs_;
    std::vector<swml::Verb> answer_verbs_;
    std::vector<swml::Verb> post_answer_verbs_;
    std::vector<swml::Verb> post_ai_verbs_;

    // Contexts
    std::optional<contexts::ContextBuilder> context_builder_;

    // Skills
    std::vector<std::string> loaded_skills_;
    std::map<std::string, json> skill_configs_;

    // Web config
    DynamicConfigCallback dynamic_config_callback_;
    std::optional<std::string> proxy_url_;
    std::optional<std::string> webhook_url_;
    std::vector<SwaigQueryParam> swaig_query_params_;
    bool debug_routes_ = false;

    // SIP
    bool sip_routing_enabled_ = false;
    std::vector<std::string> sip_usernames_;
    bool auto_map_sip_ = false;

    // Callbacks
    SummaryCallback summary_callback_;
    DebugEventCallback debug_event_callback_;

    // Security
    security::SessionManager session_manager_;

    // Server
    std::unique_ptr<httplib::Server> server_;
    mutable std::shared_mutex state_mutex_;
};

} // namespace agent
} // namespace signalwire
