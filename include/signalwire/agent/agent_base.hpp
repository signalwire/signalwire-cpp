// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#pragma once

#include <string>
#include <vector>
#include <map>
#include <set>
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
#include "signalwire/pom/pom.hpp"
#include "signalwire/logging.hpp"

namespace httplib { class Server; class Request; class Response; }

namespace signalwire { namespace server { class AgentServer; } }

namespace signalwire {
namespace agent {

using json = nlohmann::json;

/// Back-compat alias for the original ``signalwire::agent::PomSection``
/// type. The implementation now lives in ``signalwire::pom::Section`` —
/// see ``signalwire/pom/pom.hpp`` for the full API (render_markdown,
/// render_xml, numbered/numberedBullets fields, etc.). New code should
/// use ``signalwire::pom::Section`` directly.
using PomSection = signalwire::pom::Section;

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

class AgentBase : public swml::Service {
    friend class signalwire::server::AgentServer;
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
    // Identity — name() / route() inherited from Service. Covariant
    // override on set_name so existing fluent chains like
    // `agent.set_name(..).set_prompt_text(..)` keep an AgentBase& reference.
    // ========================================================================
    AgentBase& set_name(const std::string& n) {
        swml::Service::set_name(n);
        return *this;
    }

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

    /// Read-only snapshot of the agent's POM as a ``PromptObjectModel``.
    ///
    /// Python parity: ``agent.pom`` instance attribute (agent_base.py
    /// line 209). Returns ``std::nullopt`` when ``use_pom`` is false
    /// (mirroring Python's ``self.pom = None``); otherwise returns a
    /// freshly built ``signalwire::pom::PromptObjectModel`` whose
    /// sections are deep-copied from the agent's internal section/
    /// subsection structures so callers cannot mutate them in-place.
    std::optional<signalwire::pom::PromptObjectModel> pom() const;

    /// Returns the post-prompt text whatever ``set_post_prompt`` stored, or
    /// ``std::nullopt`` when no post-prompt has been set.
    ///
    /// Mirrors Python's ``PromptManager.get_post_prompt`` /
    /// ``PromptMixin.get_post_prompt`` — used by SWML rendering when a
    /// post-prompt is configured.
    std::optional<std::string> get_post_prompt() const;

    /// Returns the raw prompt text whatever ``set_prompt_text`` stored, or
    /// ``std::nullopt`` when no raw prompt has been set. Distinct from
    /// ``get_prompt`` which renders the POM array when ``use_pom`` is
    /// true.
    ///
    /// Mirrors Python's ``PromptManager.get_raw_prompt``.
    std::optional<std::string> get_raw_prompt() const;

    /// Sets the prompt as a list of POM section JSON objects. Each
    /// section supports keys "title", "body", "bullets", "numbered",
    /// "numbered_bullets", and "subsections". Switches the agent to POM
    /// mode.
    ///
    /// Mirrors Python's ``PromptManager.set_prompt_pom``.
    AgentBase& set_prompt_pom(const std::vector<json>& pom);

    /// Returns the contexts dictionary as a serialised JSON object, or
    /// ``std::nullopt`` when no contexts have been defined yet.
    ///
    /// Mirrors Python's ``PromptManager.get_contexts`` which returns the
    /// contexts dict or ``None``.
    std::optional<json> get_contexts() const;

    // ========================================================================
    // Tool Methods
    // ========================================================================

    /// Register a SWAIG tool (function) that the AI can invoke during a
    /// call.
    ///
    /// ## How this becomes a tool the model sees
    ///
    /// A SWAIG function is *exactly the same concept* as a "tool" in
    /// native OpenAI / Anthropic tool calling. On every LLM turn, the
    /// SDK renders each registered SWAIG function into the OpenAI tool
    /// schema:
    ///
    ///   {
    ///     "type": "function",
    ///     "function": {
    ///       "name":        "your_name_here",
    ///       "description": "your description text",
    ///       "parameters":  { ... your JSON schema ... }
    ///     }
    ///   }
    ///
    /// That schema is sent to the model as part of the same API call
    /// that produces the next assistant message. The model reads:
    ///   - the function `description` to decide WHEN to call this tool
    ///   - each parameter `description` (inside `parameters`) to decide
    ///     HOW to fill in that argument from the user's utterance
    ///
    /// This means *descriptions are prompt engineering*, not developer
    /// comments. A vague description is the #1 cause of "the model has
    /// the right tool but doesn't call it" failures.
    ///
    /// ## Bad vs good descriptions
    ///
    ///   BAD : description: "Lookup function"
    ///   GOOD: description: "Look up a customer's account details by "
    ///                       "account number. Use this BEFORE quoting "
    ///                       "any account-specific info (balance, plan, "
    ///                       "status). Do not use for general product "
    ///                       "questions."
    ///
    ///   BAD : parameters: {"id": {"type": "string",
    ///                              "description": "the id"}}
    ///   GOOD: parameters: {"account_number": {"type": "string",
    ///             "description": "The customer's 8-digit account "
    ///             "number, no dashes or spaces. Ask the user if they "
    ///             "don't provide it."}}
    ///
    /// ## Tool count matters
    ///
    /// LLM tool selection accuracy degrades past ~7-8
    /// simultaneously-active tools per call. Use
    /// contexts::Step::set_functions to partition tools across steps
    /// so only the relevant subset is active at any moment.
    // define_tool, register_swaig_function, on_function_call, has_tool are
    // inherited from Service. AgentBase keeps thin covariant overrides on
    // the chainable methods so existing fluent-chain users keep an AgentBase
    // reference. on_function_call is overridden to add session-token
    // validation.
    AgentBase& define_tool(const swaig::ToolDefinition& tool);
    AgentBase& define_tool(const std::string& name, const std::string& description,
                            const json& parameters, swaig::ToolHandler handler,
                            bool secure = false);
    AgentBase& register_swaig_function(const json& func_def);
    swaig::FunctionResult on_function_call(const std::string& name,
                                            const json& args,
                                            const json& raw_data) override;
    std::vector<std::string> list_tools() const;

    /// Mint a per-call SWAIG-function token via the agent's SessionManager.
    ///
    /// Python parity: ``state_mixin.StateMixin._create_tool_token`` —
    /// delegates to ``SessionManager::create_token`` and returns an empty
    /// string on any thrown exception (Python catches all exceptions and
    /// returns "" on error).
    std::string create_tool_token(const std::string& tool_name,
                                   const std::string& call_id) const;

    /// Validate a per-call SWAIG-function token. Returns ``false`` when
    /// the function is not registered, when the SessionManager rejects the
    /// token, or on any underlying exception.
    ///
    /// Python parity: ``state_mixin.StateMixin.validate_tool_token`` —
    /// rejects unknown function names up-front and swallows exceptions.
    bool validate_tool_token(const std::string& function_name,
                              const std::string& token,
                              const std::string& call_id) const;

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
    /// The complete set of internal SWAIG function names that accept
    /// fillers, matching the SWAIGInternalFiller schema definition.
    /// Any name outside this set is silently ignored by the runtime —
    /// set_internal_fillers and add_internal_filler warn if you pass
    /// an unknown name.
    ///
    /// Notable absences: change_step, gather_submit, and arbitrary
    /// user-defined SWAIG function names are NOT supported.
    static const std::set<std::string>& supported_internal_filler_names();

    /// Set internal fillers for native SWAIG functions.
    ///
    /// Internal fillers are short phrases the AI agent speaks (via
    /// TTS) while an internal/native function is running, so the
    /// caller doesn't hear dead air during transitions or background
    /// work.
    ///
    /// Supported function names (match the SWAIGInternalFiller
    /// schema): hangup, check_time, wait_for_user, wait_seconds,
    /// adjust_response_latency, next_step, change_context,
    /// get_visual_input, get_ideal_strategy. See
    /// supported_internal_filler_names().
    ///
    /// Notably NOT supported: change_step, gather_submit, or arbitrary
    /// user-defined SWAIG function names. The runtime only honors
    /// fillers for the names listed above; everything else is
    /// silently ignored at the SWML level. This method warns at
    /// registration time if you pass an unknown name so you catch the
    /// typo early.
    ///
    /// Expected JSON shape:
    ///   {"function_name": {"language_code": ["phrase1", ...]}, ...}
    AgentBase& set_internal_fillers(const json& fillers);

    /// Add internal fillers for a single language (legacy overload;
    /// stored under the given language key at the top level).
    AgentBase& add_internal_filler(const std::string& lang,
                                    const std::vector<std::string>& fillers);

    /// Add internal fillers for a single internal function + language.
    /// See set_internal_fillers() for the complete list of supported
    /// function_name values (supported_internal_filler_names()) and
    /// what fillers do. Names outside the supported set log a warning.
    AgentBase& add_internal_filler(const std::string& function_name,
                                    const std::string& language_code,
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

    /// Remove all contexts, returning the agent to a no-contexts state.
    /// Convenience wrapper around define_contexts().reset().
    AgentBase& reset_contexts();

    // ========================================================================
    // Skills Methods
    // ========================================================================

    AgentBase& add_skill(const std::string& skill_name, const json& params = json::object());
    AgentBase& remove_skill(const std::string& skill_name);
    bool has_skill(const std::string& skill_name) const;
    std::vector<std::string> list_skills() const;

    // ========================================================================
    // MCP Integration
    // ========================================================================

    AgentBase& add_mcp_server(const std::string& url,
                               const std::map<std::string, std::string>& headers = {},
                               bool resources = false,
                               const std::map<std::string, std::string>& resource_vars = {});
    AgentBase& enable_mcp_server(bool enable = true);
    bool is_mcp_server_enabled() const { return mcp_server_enabled_; }
    const std::vector<json>& mcp_servers() const { return mcp_servers_; }
    std::vector<json> build_mcp_tool_list() const;
    json handle_mcp_request(const json& body);

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

    // name_, route_, host_, port_, auth_user_, auth_pass_, auth_initialized_,
    // tools_, tool_order_, registered_swaig_functions_ are inherited from
    // Service. The fields below are agent-specific.

    // Prompt
    std::optional<std::string> raw_prompt_text_;
    std::optional<std::string> post_prompt_text_;
    std::optional<std::string> post_prompt_url_;
    std::vector<PomSection> pom_sections_;
    bool use_pom_ = true;

    // Tools — tools_, tool_order_, registered_swaig_functions_ are inherited
    // from Service (lifted so non-agent SWMLService instances can host SWAIG).
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

    // MCP
    std::vector<json> mcp_servers_;
    bool mcp_server_enabled_ = false;

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
