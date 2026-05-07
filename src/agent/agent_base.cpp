// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#include "signalwire/agent/agent_base.hpp"
#include "signalwire/skills/skill_registry.hpp"
#include "signalwire/security/webhook_middleware.hpp"
#include "signalwire/common.hpp"
#include "httplib.h"
#include <openssl/crypto.h>
#include <openssl/rand.h>
#include <algorithm>
#include <set>

namespace signalwire {
namespace agent {

// ============================================================================
// Constructor / Destructor
// ============================================================================

AgentBase::AgentBase(const std::string& name, const std::string& route,
                     const std::string& host, int port)
    : swml::Service() {
    name_ = name;
    route_ = route;
    host_ = host;
    port_ = port;
    if (!route_.empty() && route_.front() != '/') {
        route_ = "/" + route_;
    }
    // Set default port from env
    std::string env_port = get_env("PORT", "");
    if (!env_port.empty()) {
        try { port_ = std::stoi(env_port); } catch (...) {}
    }

    // Webhook signature validation (porting-sdk/webhooks.md):
    // pick up SIGNALWIRE_SIGNING_KEY at construction time as a fallback;
    // explicit set_signing_key(...) wins. The actual route mounting + the
    // "disabled" warning is emitted at serve() time so callers who set
    // the key after construction don't get the misleading warning.
    std::string env_key = get_env("SIGNALWIRE_SIGNING_KEY", "");
    if (!env_key.empty()) {
        signing_key_ = env_key;
    }
}

AgentBase::~AgentBase() {
    stop();
}

AgentBase::AgentBase(const AgentBase& other)
    : swml::Service() {
    // Service-level state copied through the protected fields the parent owns.
    name_ = other.name_;
    route_ = other.route_;
    host_ = other.host_;
    port_ = other.port_;
    auth_user_ = other.auth_user_;
    auth_pass_ = other.auth_pass_;
    auth_initialized_ = other.auth_initialized_;
    tools_ = other.tools_;
    tool_order_ = other.tool_order_;
    registered_swaig_functions_ = other.registered_swaig_functions_;

    raw_prompt_text_ = other.raw_prompt_text_;
    post_prompt_text_ = other.post_prompt_text_;
    post_prompt_url_ = other.post_prompt_url_;
    pom_sections_ = other.pom_sections_;
    use_pom_ = other.use_pom_;
    datamap_functions_ = other.datamap_functions_;
    function_includes_ = other.function_includes_;
    hints_ = other.hints_;
    languages_ = other.languages_;
    pronunciations_ = other.pronunciations_;
    ai_params_ = other.ai_params_;
    global_data_ = other.global_data_;
    native_functions_ = other.native_functions_;
    internal_fillers_ = other.internal_fillers_;
    debug_events_ = other.debug_events_;
    prompt_llm_params_ = other.prompt_llm_params_;
    post_prompt_llm_params_ = other.post_prompt_llm_params_;
    pre_answer_verbs_ = other.pre_answer_verbs_;
    answer_verbs_ = other.answer_verbs_;
    post_answer_verbs_ = other.post_answer_verbs_;
    post_ai_verbs_ = other.post_ai_verbs_;
    context_builder_ = other.context_builder_;
    loaded_skills_ = other.loaded_skills_;
    skill_configs_ = other.skill_configs_;
    mcp_servers_ = other.mcp_servers_;
    mcp_server_enabled_ = other.mcp_server_enabled_;
    proxy_url_ = other.proxy_url_;
    webhook_url_ = other.webhook_url_;
    swaig_query_params_ = other.swaig_query_params_;
    debug_routes_ = other.debug_routes_;
    sip_routing_enabled_ = other.sip_routing_enabled_;
    sip_usernames_ = other.sip_usernames_;
    auto_map_sip_ = other.auto_map_sip_;
    summary_callback_ = other.summary_callback_;
    debug_event_callback_ = other.debug_event_callback_;
    signing_key_ = other.signing_key_;
    signing_key_warning_emitted_ = other.signing_key_warning_emitted_;
    trust_proxy_for_signature_ = other.trust_proxy_for_signature_;
    // Note: server_ is NOT copied; session_manager_ gets a new secret
}

// ============================================================================
// Prompt Methods
// ============================================================================

AgentBase& AgentBase::set_prompt_text(const std::string& text) {
    raw_prompt_text_ = text;
    return *this;
}

AgentBase& AgentBase::set_post_prompt(const std::string& text) {
    post_prompt_text_ = text;
    return *this;
}

AgentBase& AgentBase::set_post_prompt_url(const std::string& url) {
    post_prompt_url_ = url;
    return *this;
}

AgentBase& AgentBase::prompt_add_section(const std::string& title,
                                          const std::string& body,
                                          const std::vector<std::string>& bullets) {
    PomSection section;
    section.title = title;
    section.body = body;
    section.bullets = bullets;
    pom_sections_.push_back(std::move(section));
    return *this;
}

AgentBase& AgentBase::prompt_add_subsection(const std::string& parent_title,
                                             const std::string& title,
                                             const std::string& body,
                                             const std::vector<std::string>& bullets) {
    for (auto& section : pom_sections_) {
        if (section.title.has_value() && *section.title == parent_title) {
            PomSection sub;
            sub.title = title;
            sub.body = body;
            sub.bullets = bullets;
            section.subsections.push_back(std::move(sub));
            break;
        }
    }
    return *this;
}

AgentBase& AgentBase::prompt_add_to_section(const std::string& title,
                                             const std::string& body,
                                             const std::vector<std::string>& bullets) {
    for (auto& section : pom_sections_) {
        if (section.title.has_value() && *section.title == title) {
            if (!body.empty()) section.body += "\n" + body;
            for (const auto& b : bullets) section.bullets.push_back(b);
            return *this;
        }
    }
    // If section doesn't exist, create it
    return prompt_add_section(title, body, bullets);
}

bool AgentBase::prompt_has_section(const std::string& title) const {
    for (const auto& s : pom_sections_) {
        if (s.title.has_value() && *s.title == title) return true;
    }
    return false;
}

std::string AgentBase::get_prompt() const {
    if (raw_prompt_text_) return *raw_prompt_text_;
    std::string result;
    for (const auto& s : pom_sections_) {
        const std::string& title_str = s.title.has_value() ? *s.title : std::string();
        result += "## " + title_str + "\n";
        if (!s.body.empty()) result += s.body + "\n";
        for (const auto& b : s.bullets) result += "- " + b + "\n";
        for (const auto& sub : s.subsections) {
            const std::string& sub_title = sub.title.has_value() ? *sub.title : std::string();
            result += "### " + sub_title + "\n";
            if (!sub.body.empty()) result += sub.body + "\n";
            for (const auto& b : sub.bullets) result += "- " + b + "\n";
        }
        result += "\n";
    }
    return result;
}

AgentBase& AgentBase::set_use_pom(bool use_pom) {
    use_pom_ = use_pom;
    return *this;
}

std::optional<signalwire::pom::PromptObjectModel> AgentBase::pom() const {
    if (!use_pom_) {
        return std::nullopt;
    }
    // Deep-copy the sections so the caller can't mutate the agent's state
    // through the returned model. Each PomSection is itself a value type,
    // so vector copy already deep-copies.
    signalwire::pom::PromptObjectModel result;
    result.sections = pom_sections_;
    return result;
}

std::optional<std::string> AgentBase::get_post_prompt() const {
    return post_prompt_text_;
}

std::optional<std::string> AgentBase::get_raw_prompt() const {
    return raw_prompt_text_;
}

namespace {

PomSection json_to_section(const json& j) {
    PomSection s;
    if (j.contains("title") && j["title"].is_string()) {
        s.title = j["title"].get<std::string>();
    }
    if (j.contains("body") && j["body"].is_string()) {
        s.body = j["body"].get<std::string>();
    }
    if (j.contains("bullets") && j["bullets"].is_array()) {
        for (const auto& b : j["bullets"]) {
            if (b.is_string()) {
                s.bullets.push_back(b.get<std::string>());
            }
        }
    }
    if (j.contains("subsections") && j["subsections"].is_array()) {
        for (const auto& sub : j["subsections"]) {
            s.subsections.push_back(json_to_section(sub));
        }
    }
    return s;
}

}  // namespace

AgentBase& AgentBase::set_prompt_pom(const std::vector<json>& pom) {
    use_pom_ = true;
    pom_sections_.clear();
    pom_sections_.reserve(pom.size());
    for (const auto& section_json : pom) {
        if (section_json.is_object()) {
            pom_sections_.push_back(json_to_section(section_json));
        }
    }
    return *this;
}

std::optional<json> AgentBase::get_contexts() const {
    if (!context_builder_.has_value()) {
        return std::nullopt;
    }
    return context_builder_->to_json();
}

// ============================================================================
// Tool Methods
// ============================================================================

// Covariant overrides: Service does the registry work; AgentBase just
// returns *this typed as AgentBase& so existing fluent chains keep their
// derived-type reference.

AgentBase& AgentBase::define_tool(const swaig::ToolDefinition& tool) {
    swml::Service::define_tool(tool);
    return *this;
}

AgentBase& AgentBase::define_tool(const std::string& name, const std::string& description,
                                   const json& parameters, swaig::ToolHandler handler,
                                   bool secure) {
    swml::Service::define_tool(name, description, parameters, std::move(handler), secure);
    return *this;
}

AgentBase& AgentBase::register_swaig_function(const json& func_def) {
    // AgentBase additionally tracks datamap functions for SWML rendering.
    // Service handles registry/order tracking.
    datamap_functions_.push_back(func_def);
    swml::Service::register_swaig_function(func_def);
    return *this;
}

swaig::FunctionResult AgentBase::on_function_call(const std::string& name,
                                                    const json& args,
                                                    const json& raw_data) {
    auto it = tools_.find(name);
    if (it == tools_.end()) {
        return swaig::FunctionResult("Unknown function: " + name);
    }
    if (!it->second.handler) {
        return swaig::FunctionResult("No handler for function: " + name);
    }
    // Per-tool secure-token validation runs in the dispatcher
    // (handle_swaig_request) before this function is reached: it checks
    // ToolDefinition.secure, reads meta_data_token + call_id from the SWAIG
    // body, and calls session_manager_.validate_token before invoking the
    // handler. on_function_call is the post-validation dispatch hook —
    // overrides should keep this contract.
    return it->second.handler(args, raw_data);
}

// has_tool is inherited from Service.

std::vector<std::string> AgentBase::list_tools() const {
    return tool_order_;
}

std::string AgentBase::create_tool_token(const std::string& tool_name,
                                          const std::string& call_id) const {
    try {
        return session_manager_.create_token(tool_name, call_id);
    } catch (...) {
        return "";
    }
}

bool AgentBase::validate_tool_token(const std::string& function_name,
                                     const std::string& token,
                                     const std::string& call_id) const {
    if (!has_function(function_name)) {
        return false;
    }
    try {
        return session_manager_.validate_token(token, function_name, call_id);
    } catch (...) {
        return false;
    }
}

// ============================================================================
// AI Config Methods
// ============================================================================

AgentBase& AgentBase::add_hint(const std::string& hint) {
    hints_.push_back(hint);
    return *this;
}

AgentBase& AgentBase::add_hints(const std::vector<std::string>& hints) {
    for (const auto& h : hints) hints_.push_back(h);
    return *this;
}

AgentBase& AgentBase::add_pattern_hint(const std::string& pattern) {
    // Pattern hints are treated the same as regular hints
    hints_.push_back(pattern);
    return *this;
}

AgentBase& AgentBase::add_language(const LanguageConfig& lang) {
    languages_.push_back(lang);
    return *this;
}

AgentBase& AgentBase::set_languages(const std::vector<LanguageConfig>& langs) {
    languages_ = langs;
    return *this;
}

AgentBase& AgentBase::add_pronunciation(const std::string& replace_val,
                                         const std::string& with_val,
                                         bool ignore_case) {
    pronunciations_.push_back({replace_val, with_val, ignore_case});
    return *this;
}

AgentBase& AgentBase::set_pronunciations(const std::vector<Pronunciation>& pronuns) {
    pronunciations_ = pronuns;
    return *this;
}

AgentBase& AgentBase::set_param(const std::string& key, const json& value) {
    ai_params_[key] = value;
    return *this;
}

AgentBase& AgentBase::set_params(const json& params) {
    for (auto& [k, v] : params.items()) {
        ai_params_[k] = v;
    }
    return *this;
}

AgentBase& AgentBase::set_global_data(const json& data) {
    global_data_ = data;
    return *this;
}

AgentBase& AgentBase::update_global_data(const json& data) {
    if (global_data_.is_null()) global_data_ = json::object();
    for (auto& [k, v] : data.items()) {
        global_data_[k] = v;
    }
    return *this;
}

AgentBase& AgentBase::set_native_functions(const std::vector<std::string>& funcs) {
    native_functions_ = funcs;
    return *this;
}

const std::set<std::string>& AgentBase::supported_internal_filler_names() {
    static const std::set<std::string> kSupported{
        "hangup",                   // AI is hanging up the call
        "check_time",               // AI is checking the time
        "wait_for_user",            // AI is waiting for user input
        "wait_seconds",             // deliberate pause / wait period
        "adjust_response_latency",  // AI is adjusting response timing
        "next_step",                // transitioning between steps in prompt.contexts
        "change_context",           // switching between contexts in prompt.contexts
        "get_visual_input",         // processing visual input (enable_vision)
        "get_ideal_strategy",       // thinking (enable_thinking)
    };
    return kSupported;
}

static std::string sorted_list_str(const std::set<std::string>& s) {
    std::vector<std::string> v(s.begin(), s.end());
    std::sort(v.begin(), v.end());
    std::string out = "[";
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (i > 0) out += ", ";
        out += "'" + v[i] + "'";
    }
    out += "]";
    return out;
}

AgentBase& AgentBase::set_internal_fillers(const json& fillers) {
    if (fillers.is_object()) {
        const auto& supported = supported_internal_filler_names();
        std::vector<std::string> unknown;
        for (auto it = fillers.begin(); it != fillers.end(); ++it) {
            if (supported.count(it.key()) == 0) {
                unknown.push_back(it.key());
            }
        }
        if (!unknown.empty()) {
            std::sort(unknown.begin(), unknown.end());
            std::string unknown_str = "[";
            for (std::size_t i = 0; i < unknown.size(); ++i) {
                if (i > 0) unknown_str += ", ";
                unknown_str += "'" + unknown[i] + "'";
            }
            unknown_str += "]";
            get_logger().warn(
                "unknown_internal_filler_names: " + unknown_str +
                ". set_internal_fillers received names that the SWML "
                "schema does not recognize. Those entries will be "
                "ignored by the runtime. Supported names: " +
                sorted_list_str(supported) + ".");
        }
    }
    internal_fillers_ = fillers;
    return *this;
}

AgentBase& AgentBase::add_internal_filler(const std::string& lang,
                                           const std::vector<std::string>& fillers) {
    if (internal_fillers_.is_null()) internal_fillers_ = json::object();
    internal_fillers_[lang] = fillers;
    return *this;
}

AgentBase& AgentBase::add_internal_filler(const std::string& function_name,
                                           const std::string& language_code,
                                           const std::vector<std::string>& fillers) {
    const auto& supported = supported_internal_filler_names();
    if (supported.count(function_name) == 0) {
        get_logger().warn(
            "unknown_internal_filler_name: '" + function_name +
            "'. add_internal_filler received a function name the SWML "
            "schema does not recognize. The entry will be stored but "
            "the runtime will not play these fillers. Supported "
            "names: " + sorted_list_str(supported) + ".");
    }
    if (internal_fillers_.is_null()) internal_fillers_ = json::object();
    if (!internal_fillers_.contains(function_name) ||
        !internal_fillers_[function_name].is_object()) {
        internal_fillers_[function_name] = json::object();
    }
    internal_fillers_[function_name][language_code] = fillers;
    return *this;
}

AgentBase& AgentBase::enable_debug_events(bool enable) {
    debug_events_ = enable;
    return *this;
}

AgentBase& AgentBase::add_function_include(const json& include) {
    function_includes_.push_back(include);
    return *this;
}

AgentBase& AgentBase::set_function_includes(const std::vector<json>& includes) {
    function_includes_ = includes;
    return *this;
}

AgentBase& AgentBase::set_prompt_llm_params(const json& params) {
    prompt_llm_params_ = params;
    return *this;
}

AgentBase& AgentBase::set_post_prompt_llm_params(const json& params) {
    post_prompt_llm_params_ = params;
    return *this;
}

// ============================================================================
// Verb Methods (5-phase pipeline)
// ============================================================================

AgentBase& AgentBase::add_pre_answer_verb(const std::string& verb_name, const json& params) {
    pre_answer_verbs_.emplace_back(verb_name, params);
    return *this;
}

AgentBase& AgentBase::add_answer_verb(const std::string& verb_name, const json& params) {
    answer_verbs_.emplace_back(verb_name, params);
    return *this;
}

AgentBase& AgentBase::add_post_answer_verb(const std::string& verb_name, const json& params) {
    post_answer_verbs_.emplace_back(verb_name, params);
    return *this;
}

AgentBase& AgentBase::add_post_ai_verb(const std::string& verb_name, const json& params) {
    post_ai_verbs_.emplace_back(verb_name, params);
    return *this;
}

AgentBase& AgentBase::clear_pre_answer_verbs() { pre_answer_verbs_.clear(); return *this; }
AgentBase& AgentBase::clear_post_answer_verbs() { post_answer_verbs_.clear(); return *this; }
AgentBase& AgentBase::clear_post_ai_verbs() { post_ai_verbs_.clear(); return *this; }

// ============================================================================
// Context Methods
// ============================================================================

contexts::ContextBuilder& AgentBase::define_contexts() {
    if (!context_builder_) {
        context_builder_ = contexts::ContextBuilder();
        // Attach a tool-name supplier so validate() can check
        // user-defined tool names against reserved native tool names
        // (next_step, change_context, gather_submit).
        context_builder_->attach_tool_name_supplier([this]() {
            return this->list_tools();
        });
    }
    return *context_builder_;
}

contexts::Context& AgentBase::add_context(const std::string& name) {
    return define_contexts().add_context(name);
}

bool AgentBase::has_contexts() const {
    return context_builder_ && context_builder_->has_contexts();
}

AgentBase& AgentBase::reset_contexts() {
    if (context_builder_) {
        context_builder_->reset();
    }
    return *this;
}

// ============================================================================
// Skills Methods
// ============================================================================

AgentBase& AgentBase::add_skill(const std::string& skill_name, const json& params) {
    loaded_skills_.push_back(skill_name);
    skill_configs_[skill_name] = params;

    // Resolve the skill from the registry and register its tools/prompts
    skills::ensure_builtin_skills_registered();
    auto& reg = skills::SkillRegistry::instance();
    auto skill = reg.create(skill_name);
    if (!skill) return *this;
    if (!skill->setup(params)) return *this;

    // Register tools from the skill
    for (auto& tool : skill->register_tools()) {
        define_tool(tool);
    }

    // Register DataMap functions
    for (const auto& dm_fn : skill->get_datamap_functions()) {
        register_swaig_function(dm_fn);
    }

    // Add prompt sections
    for (const auto& section : skill->get_prompt_sections()) {
        std::vector<std::string> bullets;
        for (const auto& b : section.bullets) bullets.push_back(b);
        prompt_add_section(section.title, section.body, bullets);
    }

    // Add hints
    for (const auto& h : skill->get_hints()) {
        add_hints({h});
    }

    return *this;
}

AgentBase& AgentBase::remove_skill(const std::string& skill_name) {
    loaded_skills_.erase(std::remove(loaded_skills_.begin(), loaded_skills_.end(), skill_name),
                         loaded_skills_.end());
    skill_configs_.erase(skill_name);
    return *this;
}

bool AgentBase::has_skill(const std::string& skill_name) const {
    return std::find(loaded_skills_.begin(), loaded_skills_.end(), skill_name) != loaded_skills_.end();
}

std::vector<std::string> AgentBase::list_skills() const {
    return loaded_skills_;
}

// ============================================================================
// Web / Config Methods
// ============================================================================

AgentBase& AgentBase::set_dynamic_config_callback(DynamicConfigCallback cb) {
    dynamic_config_callback_ = std::move(cb);
    return *this;
}

AgentBase& AgentBase::manual_set_proxy_url(const std::string& url) {
    proxy_url_ = url;
    return *this;
}

AgentBase& AgentBase::set_webhook_url(const std::string& url) {
    webhook_url_ = url;
    return *this;
}

AgentBase& AgentBase::set_post_prompt_url_direct(const std::string& url) {
    post_prompt_url_ = url;
    return *this;
}

AgentBase& AgentBase::add_swaig_query_param(const std::string& key, const std::string& value) {
    swaig_query_params_.push_back({key, value});
    return *this;
}

AgentBase& AgentBase::clear_swaig_query_params() {
    swaig_query_params_.clear();
    return *this;
}

AgentBase& AgentBase::enable_debug_routes(bool enable) {
    debug_routes_ = enable;
    return *this;
}

// ============================================================================
// MCP Integration
// ============================================================================

AgentBase& AgentBase::add_mcp_server(const std::string& url,
                                      const std::map<std::string, std::string>& headers,
                                      bool resources,
                                      const std::map<std::string, std::string>& resource_vars) {
    json server;
    server["url"] = url;
    if (!headers.empty()) {
        json h = json::object();
        for (const auto& [k, v] : headers) h[k] = v;
        server["headers"] = h;
    }
    if (resources) {
        server["resources"] = true;
    }
    if (!resource_vars.empty()) {
        json rv = json::object();
        for (const auto& [k, v] : resource_vars) rv[k] = v;
        server["resource_vars"] = rv;
    }
    mcp_servers_.push_back(server);
    return *this;
}

AgentBase& AgentBase::enable_mcp_server(bool enable) {
    mcp_server_enabled_ = enable;
    return *this;
}

std::vector<json> AgentBase::build_mcp_tool_list() const {
    std::vector<json> tools;
    for (const auto& name : tool_order_) {
        auto it = tools_.find(name);
        if (it != tools_.end()) {
            json tool;
            tool["name"] = it->second.name;
            tool["description"] = it->second.description.empty() ? it->second.name : it->second.description;
            if (!it->second.parameters.is_null() && !it->second.parameters.empty()) {
                tool["inputSchema"] = it->second.parameters;
            } else {
                tool["inputSchema"] = json::object({{"type", "object"}, {"properties", json::object()}});
            }
            tools.push_back(tool);
        }
    }
    return tools;
}

json AgentBase::handle_mcp_request(const json& body) {
    std::string jsonrpc = body.value("jsonrpc", "");
    std::string method = body.value("method", "");
    auto req_id = body.contains("id") ? body["id"] : json(nullptr);
    json params = body.value("params", json::object());

    auto mcp_error = [&](int code, const std::string& message) -> json {
        return json::object({
            {"jsonrpc", "2.0"},
            {"id", req_id},
            {"error", json::object({{"code", code}, {"message", message}})}
        });
    };

    if (jsonrpc != "2.0") {
        return mcp_error(-32600, "Invalid JSON-RPC version");
    }

    // Initialize handshake
    if (method == "initialize") {
        return json::object({
            {"jsonrpc", "2.0"},
            {"id", req_id},
            {"result", json::object({
                {"protocolVersion", "2025-06-18"},
                {"capabilities", json::object({{"tools", json::object()}})},
                {"serverInfo", json::object({{"name", name_}, {"version", "1.0.0"}})}
            })}
        });
    }

    // Initialized notification
    if (method == "notifications/initialized") {
        return json::object({{"jsonrpc", "2.0"}, {"id", req_id}, {"result", json::object()}});
    }

    // List tools
    if (method == "tools/list") {
        return json::object({
            {"jsonrpc", "2.0"},
            {"id", req_id},
            {"result", json::object({{"tools", build_mcp_tool_list()}})}
        });
    }

    // Call tool
    if (method == "tools/call") {
        std::string tool_name = params.value("name", "");
        json arguments = params.value("arguments", json::object());

        auto it = tools_.find(tool_name);
        if (it == tools_.end()) {
            return mcp_error(-32602, "Unknown tool: " + tool_name);
        }

        try {
            json raw_data = json::object({
                {"function", tool_name},
                {"argument", json::object({{"parsed", json::array({arguments})}})}
            });

            swaig::FunctionResult fn_result = on_function_call(tool_name, arguments, raw_data);
            std::string response_text;
            json result_json = fn_result.to_json();
            if (result_json.contains("response") && result_json["response"].is_string()) {
                response_text = result_json["response"].get<std::string>();
            }

            return json::object({
                {"jsonrpc", "2.0"},
                {"id", req_id},
                {"result", json::object({
                    {"content", json::array({json::object({{"type", "text"}, {"text", response_text}})})},
                    {"isError", false}
                })}
            });
        } catch (const std::exception& e) {
            get_logger().error(std::string("MCP tool call error: ") + tool_name + ": " + e.what());
            return json::object({
                {"jsonrpc", "2.0"},
                {"id", req_id},
                {"result", json::object({
                    {"content", json::array({json::object({{"type", "text"}, {"text", std::string("Error: ") + e.what()}})})},
                    {"isError", true}
                })}
            });
        }
    }

    // Ping
    if (method == "ping") {
        return json::object({{"jsonrpc", "2.0"}, {"id", req_id}, {"result", json::object()}});
    }

    return mcp_error(-32601, "Method not found: " + method);
}

// ============================================================================
// SIP Methods
// ============================================================================

AgentBase& AgentBase::enable_sip_routing(bool enable) {
    sip_routing_enabled_ = enable;
    return *this;
}

AgentBase& AgentBase::register_sip_username(const std::string& username) {
    // Validate SIP username format
    static const std::regex valid_sip_re("^[a-zA-Z0-9._-]{1,64}$");
    if (!std::regex_match(username, valid_sip_re)) {
        get_logger().warn("Invalid SIP username format: " + username);
        return *this;
    }
    sip_usernames_.push_back(username);
    return *this;
}

AgentBase& AgentBase::auto_map_sip_usernames(bool enable) {
    auto_map_sip_ = enable;
    return *this;
}

// ============================================================================
// Auth
// ============================================================================

AgentBase& AgentBase::set_auth(const std::string& username, const std::string& password) {
    auth_user_ = username;
    auth_pass_ = password;
    auth_initialized_ = true;
    return *this;
}

// ============================================================================
// Webhook Signature Validation (porting-sdk/webhooks.md)
// ============================================================================

AgentBase& AgentBase::set_signing_key(const std::string& key) {
    if (key.empty()) {
        signing_key_.reset();
    } else {
        signing_key_ = key;
    }
    // Reset the one-shot warning so a later serve() will reflect the new
    // configuration (e.g. user calls set_signing_key("") after a key was
    // previously set).
    signing_key_warning_emitted_ = false;
    return *this;
}

std::optional<std::string> AgentBase::signing_key() const {
    return signing_key_;
}

AgentBase& AgentBase::trust_proxy_for_signature(bool trust) {
    trust_proxy_for_signature_ = trust;
    return *this;
}

void AgentBase::init_auth() {
    if (auth_initialized_) return;

    std::string env_user = get_env("SWML_BASIC_AUTH_USER");
    std::string env_pass = get_env("SWML_BASIC_AUTH_PASSWORD");

    auth_user_ = env_user.empty() ? name_ : env_user;
    bool password_auto_generated = false;
    if (!env_pass.empty()) {
        auth_pass_ = env_pass;
    } else {
        // Generate random password
        unsigned char buf[16];
        if (RAND_bytes(buf, 16) != 1) {
            throw std::runtime_error("Failed to generate random password");
        }
        std::string hex;
        hex.reserve(32);
        for (auto b : buf) {
            char h[3];
            std::snprintf(h, sizeof(h), "%02x", b);
            hex += h;
        }
        auth_pass_ = hex;
        password_auto_generated = true;
    }
    auth_initialized_ = true;
    get_logger().info("Auth configured for user: " + auth_user_);

    // Warn loudly if the password was auto-generated. This is the silent
    // cause of every external caller hitting HTTP 401 when .env wasn't
    // loaded — the password lives only in this process and changes on
    // every restart.
    if (password_auto_generated) {
        get_logger().warn(
            "basic_auth_password_autogenerated: username=\"" + auth_user_ +
            "\". No SWML_BASIC_AUTH_PASSWORD found in environment and no "
            "password passed via set_auth(). The SDK generated a random "
            "password that exists only in this process; external callers "
            "will get HTTP 401 unless they read the value from this "
            "process's env. To fix, set SWML_BASIC_AUTH_USER and "
            "SWML_BASIC_AUTH_PASSWORD in your environment, or call "
            "agent.set_auth(user, pass) before serving.");
    }
}

// ============================================================================
// Callbacks
// ============================================================================

AgentBase& AgentBase::on_summary(SummaryCallback cb) {
    summary_callback_ = std::move(cb);
    return *this;
}

AgentBase& AgentBase::on_debug_event(DebugEventCallback cb) {
    debug_event_callback_ = std::move(cb);
    return *this;
}

// ============================================================================
// SWML Rendering
// ============================================================================

json AgentBase::build_prompt() const {
    json prompt;
    if (raw_prompt_text_ && !use_pom_) {
        prompt["text"] = *raw_prompt_text_;
    } else if (use_pom_ && !pom_sections_.empty()) {
        json pom = json::array();
        for (const auto& section : pom_sections_) {
            pom.push_back(section.to_json());
        }
        prompt["pom"] = pom;
    } else if (raw_prompt_text_) {
        prompt["text"] = *raw_prompt_text_;
    } else {
        prompt["text"] = "";
    }

    // Add LLM params to prompt
    if (!prompt_llm_params_.is_null() && !prompt_llm_params_.empty()) {
        for (auto& [k, v] : prompt_llm_params_.items()) {
            prompt[k] = v;
        }
    }

    return prompt;
}

std::string AgentBase::build_webhook_url(const std::string& base_url) const {
    if (webhook_url_) return *webhook_url_;

    std::string url = base_url;
    // Add auth credentials to URL
    if (auth_initialized_ && !auth_user_.empty()) {
        // Insert credentials into the URL
        auto scheme_end = url.find("://");
        if (scheme_end != std::string::npos) {
            std::string scheme = url.substr(0, scheme_end + 3);
            std::string rest = url.substr(scheme_end + 3);
            url = scheme + signalwire::url_encode(auth_user_) + ":" +
                  signalwire::url_encode(auth_pass_) + "@" + rest;
        }
    }

    url += route_;
    if (url.back() != '/') url += "/";
    url += "swaig";

    // Add query params
    if (!swaig_query_params_.empty()) {
        url += "?";
        bool first = true;
        for (const auto& p : swaig_query_params_) {
            if (!first) url += "&";
            url += signalwire::url_encode(p.key) + "=" + signalwire::url_encode(p.value);
            first = false;
        }
    }

    return url;
}

std::string AgentBase::detect_proxy_url(const std::map<std::string, std::string>& headers) const {
    if (proxy_url_) return *proxy_url_;

    std::string env_proxy = get_env("SWML_PROXY_URL_BASE");
    if (!env_proxy.empty()) return env_proxy;

    // Check forwarded headers
    auto fwd_proto = headers.find("x-forwarded-proto");
    auto fwd_host = headers.find("x-forwarded-host");
    if (fwd_proto != headers.end() && fwd_host != headers.end()) {
        return fwd_proto->second + "://" + fwd_host->second;
    }

    auto orig_url = headers.find("x-original-url");
    if (orig_url != headers.end()) {
        return orig_url->second;
    }

    // Fallback to local
    return "http://" + host_ + ":" + std::to_string(port_);
}

json AgentBase::build_swaig_functions(const std::string& webhook_url) const {
    json functions = json::array();

    for (const auto& name : tool_order_) {
        auto it = tools_.find(name);
        if (it != tools_.end()) {
            json func = it->second.to_swaig_json(webhook_url);
            if (it->second.secure) {
                func["secure"] = true;
            }
            functions.push_back(func);
        }
    }

    // Add DataMap functions
    for (const auto& dm : datamap_functions_) {
        functions.push_back(dm);
    }

    return functions;
}

json AgentBase::build_ai_verb(const std::string& webhook_url) const {
    json ai;

    // Prompt
    ai["prompt"] = build_prompt();

    // Post-prompt
    if (post_prompt_text_) {
        json pp;
        pp["text"] = *post_prompt_text_;
        if (!post_prompt_llm_params_.is_null()) {
            for (auto& [k, v] : post_prompt_llm_params_.items()) {
                pp[k] = v;
            }
        }
        ai["post_prompt"] = pp;
    }

    // Post-prompt URL
    if (post_prompt_url_) {
        ai["post_prompt_url"] = *post_prompt_url_;
    } else {
        // Build post_prompt_url from webhook base
        std::string pp_url = webhook_url;
        // Replace /swaig with /post_prompt
        auto swaig_pos = pp_url.rfind("/swaig");
        if (swaig_pos != std::string::npos) {
            pp_url = pp_url.substr(0, swaig_pos) + "/post_prompt";
        }
        ai["post_prompt_url"] = pp_url;
    }

    // AI params
    if (!ai_params_.is_null() && !ai_params_.empty()) {
        ai["params"] = ai_params_;
    }

    // Hints
    if (!hints_.empty()) {
        ai["hints"] = hints_;
    }

    // Languages
    if (!languages_.empty()) {
        json langs = json::array();
        for (const auto& l : languages_) {
            langs.push_back(l.to_json());
        }
        ai["languages"] = langs;
    }

    // Pronunciations
    if (!pronunciations_.empty()) {
        json pronuns = json::array();
        for (const auto& p : pronunciations_) {
            pronuns.push_back(p.to_json());
        }
        ai["pronounce"] = pronuns;
    }

    // SWAIG
    json swaig_section;
    json functions = build_swaig_functions(webhook_url);
    if (!functions.empty()) {
        swaig_section["functions"] = functions;
    }
    if (!function_includes_.empty()) {
        swaig_section["includes"] = function_includes_;
    }
    if (!native_functions_.empty()) {
        swaig_section["native_functions"] = native_functions_;
    }
    // MCP servers
    if (!mcp_servers_.empty()) {
        swaig_section["mcp_servers"] = mcp_servers_;
    }

    if (!swaig_section.empty()) {
        ai["SWAIG"] = swaig_section;
    }

    // Global data
    if (!global_data_.is_null() && !global_data_.empty()) {
        ai["global_data"] = global_data_;
    }

    // Contexts
    if (context_builder_ && context_builder_->has_contexts()) {
        ai["contexts"] = context_builder_->to_json();
    }

    // Debug events
    if (debug_events_) {
        ai["debug_events"] = true;
    }

    // Internal fillers
    if (!internal_fillers_.is_null() && !internal_fillers_.empty()) {
        ai["fillers"] = internal_fillers_;
    }

    return ai;
}

json AgentBase::render_swml() const {
    std::map<std::string, std::string> empty_headers;
    return render_swml_for_request({}, json::object(), empty_headers);
}

json AgentBase::render_swml_for_request(
    const std::map<std::string, std::string>& query_params,
    const json& body_params,
    const std::map<std::string, std::string>& headers) const {

    // If dynamic config callback is set, use cloned agent
    if (dynamic_config_callback_) {
        auto copy = clone();
        dynamic_config_callback_(query_params, body_params, headers, *copy);
        return copy->render_swml_internal(headers);
    }

    return render_swml_internal(headers);
}

// Private helper to actually render
json AgentBase::render_swml_internal(const std::map<std::string, std::string>& headers) const {
    std::string base_url = detect_proxy_url(headers);
    std::string webhook_url = build_webhook_url(base_url);

    swml::Document doc;

    // Phase 1: Pre-answer verbs
    for (const auto& v : pre_answer_verbs_) {
        doc.main().add_verb(v);
    }

    // Phase 2: Answer verb
    if (answer_verbs_.empty()) {
        doc.main().add_verb("answer", json::object({{"max_duration", 3600}}));
    } else {
        for (const auto& v : answer_verbs_) {
            doc.main().add_verb(v);
        }
    }

    // Phase 3: Post-answer verbs
    for (const auto& v : post_answer_verbs_) {
        doc.main().add_verb(v);
    }

    // Phase 4: AI verb
    json ai_verb = build_ai_verb(webhook_url);
    doc.main().add_verb("ai", ai_verb);

    // Phase 5: Post-AI verbs
    for (const auto& v : post_ai_verbs_) {
        doc.main().add_verb(v);
    }

    return doc.to_json();
}

std::unique_ptr<AgentBase> AgentBase::clone() const {
    return std::make_unique<AgentBase>(*this);
}

// ============================================================================
// HTTP Handlers
// ============================================================================

void AgentBase::add_security_headers(httplib::Response& res) {
    res.set_header("X-Content-Type-Options", "nosniff");
    res.set_header("X-Frame-Options", "DENY");
    res.set_header("Cache-Control", "no-store");
}

bool AgentBase::validate_auth(const httplib::Request& req, httplib::Response& res) const {
    auto auth_it = req.headers.find("Authorization");
    if (auth_it == req.headers.end()) {
        res.status = 401;
        res.set_header("WWW-Authenticate", "Basic realm=\"SignalWire Agent\"");
        res.set_content("{\"error\":\"unauthorized\"}", "application/json");
        return false;
    }

    std::string auth_header = auth_it->second;
    if (auth_header.size() < 7 || auth_header.substr(0, 6) != "Basic ") {
        res.status = 401;
        res.set_content("{\"error\":\"invalid auth scheme\"}", "application/json");
        return false;
    }

    std::string decoded = signalwire::base64_decode(auth_header.substr(6));
    auto colon = decoded.find(':');
    if (colon == std::string::npos) {
        res.status = 401;
        res.set_content("{\"error\":\"malformed credentials\"}", "application/json");
        return false;
    }

    std::string user = decoded.substr(0, colon);
    std::string pass = decoded.substr(colon + 1);

    if (!signalwire::timing_safe_compare(user, auth_user_) ||
        !signalwire::timing_safe_compare(pass, auth_pass_)) {
        res.status = 401;
        res.set_content("{\"error\":\"invalid credentials\"}", "application/json");
        return false;
    }

    return true;
}

void AgentBase::handle_swml_request(const httplib::Request& req, httplib::Response& res) {
    add_security_headers(res);
    if (!validate_auth(req, res)) return;

    // Extract query params
    std::map<std::string, std::string> query_params;
    for (const auto& p : req.params) {
        query_params[p.first] = p.second;
    }

    // Extract headers
    std::map<std::string, std::string> headers;
    for (const auto& h : req.headers) {
        std::string lower_key = h.first;
        std::transform(lower_key.begin(), lower_key.end(), lower_key.begin(), ::tolower);
        headers[lower_key] = h.second;
    }

    // Parse body
    json body_params = json::object();
    if (!req.body.empty()) {
        try { body_params = json::parse(req.body); } catch (...) {}
    }

    json swml = render_swml_for_request(query_params, body_params, headers);
    res.set_content(swml.dump(), "application/json");
}

void AgentBase::handle_swaig_request(const httplib::Request& req, httplib::Response& res) {
    add_security_headers(res);
    if (!validate_auth(req, res)) return;

    if (req.body.empty()) {
        res.status = 400;
        res.set_content("{\"error\":\"empty body\"}", "application/json");
        return;
    }

    json body;
    try {
        body = json::parse(req.body);
    } catch (const json::parse_error&) {
        res.status = 400;
        res.set_content("{\"error\":\"invalid JSON\"}", "application/json");
        return;
    }

    std::string func_name;
    if (body.contains("function")) {
        func_name = body["function"].get<std::string>();
    } else {
        res.status = 400;
        res.set_content("{\"error\":\"missing function name\"}", "application/json");
        return;
    }

    // Validate function exists
    if (!has_tool(func_name)) {
        res.status = 404;
        res.set_content("{\"error\":\"unknown function: " + func_name + "\"}", "application/json");
        return;
    }

    // Extract args from argument.parsed[0]
    json args = json::object();
    if (body.contains("argument") && body["argument"].contains("parsed") &&
        body["argument"]["parsed"].is_array() && !body["argument"]["parsed"].empty()) {
        args = body["argument"]["parsed"][0];
    }

    // Check secure token if tool is secure
    auto tool_it = tools_.find(func_name);
    if (tool_it != tools_.end() && tool_it->second.secure) {
        std::string token = body.value("meta_data_token", "");
        std::string call_id = body.value("call_id", "");
        if (!session_manager_.validate_token(token, func_name, call_id)) {
            res.status = 403;
            res.set_content("{\"error\":\"invalid or expired token\"}", "application/json");
            return;
        }
    }

    swaig::FunctionResult result = on_function_call(func_name, args, body);
    res.set_content(result.to_string(), "application/json");
}

void AgentBase::handle_post_prompt_request(const httplib::Request& req, httplib::Response& res) {
    add_security_headers(res);
    if (!validate_auth(req, res)) return;

    json body;
    try {
        body = json::parse(req.body);
    } catch (...) {
        res.status = 400;
        res.set_content("{\"error\":\"invalid JSON\"}", "application/json");
        return;
    }

    if (summary_callback_) {
        json summary = json();
        if (body.contains("post_prompt_data")) {
            auto& ppd = body["post_prompt_data"];
            if (ppd.contains("parsed") && !ppd["parsed"].is_null()) {
                summary = ppd["parsed"];
            }
        }
        try {
            summary_callback_(summary, body);
        } catch (const std::exception& e) {
            get_logger().error(std::string("Summary callback error: ") + e.what());
        }
    }

    res.set_content("{\"status\":\"ok\"}", "application/json");
}

// ============================================================================
// Route Setup
// ============================================================================

void AgentBase::setup_routes(httplib::Server& server) {
    std::string base = route_;
    if (base.empty()) base = "/";
    if (base.back() == '/' && base.size() > 1) base.pop_back();

    // Webhook signature validation (porting-sdk/webhooks.md):
    // when signing_key is set, wrap POST handlers with the validator;
    // when unset, log a one-time warning matching the Python reference
    // and let unsigned POSTs through.
    auto wrap_post = [this](security::HttpHandler downstream)
                         -> security::HttpHandler {
        if (!signing_key_ || signing_key_->empty()) {
            return downstream;
        }
        security::WebhookValidatorOptions opts;
        opts.trust_proxy = trust_proxy_for_signature_;
        return security::WrapWithSignatureValidation(*signing_key_,
                                                     std::move(downstream),
                                                     opts);
    };

    if (signing_key_ && !signing_key_->empty()) {
        if (!signing_key_warning_emitted_) {
            get_logger().info("webhook_signature_validation_enabled");
            signing_key_warning_emitted_ = true;
        }
    } else if (!signing_key_warning_emitted_) {
        get_logger().warn(
            "[signalwire] webhook signature validation is disabled — "
            "set signing_key or SIGNALWIRE_SIGNING_KEY to enable");
        signing_key_warning_emitted_ = true;
    }

    // Health (no auth)
    server.Get("/health", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("{\"status\":\"healthy\"}", "application/json");
    });

    // Ready (no auth)
    server.Get("/ready", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("{\"status\":\"ready\"}", "application/json");
    });

    // SWML endpoint — GET unsigned, POST signature-validated when key is set.
    security::HttpHandler swml_get = [this](const httplib::Request& req,
                                            httplib::Response& res) {
        handle_swml_request(req, res);
    };
    security::HttpHandler swml_post = wrap_post(
        [this](const httplib::Request& req, httplib::Response& res) {
            handle_swml_request(req, res);
        });
    server.Get(base.c_str(), swml_get);
    server.Post(base.c_str(), swml_post);

    // SWAIG endpoint — POST signature-validated when key is set.
    std::string swaig_path = base + (base.back() == '/' ? "" : "/") + "swaig";
    server.Post(swaig_path.c_str(), wrap_post(
        [this](const httplib::Request& req, httplib::Response& res) {
            handle_swaig_request(req, res);
        }));

    // Post-prompt endpoint — POST signature-validated when key is set.
    std::string pp_path = base + (base.back() == '/' ? "" : "/") + "post_prompt";
    server.Post(pp_path.c_str(), wrap_post(
        [this](const httplib::Request& req, httplib::Response& res) {
            handle_post_prompt_request(req, res);
        }));

    // MCP server endpoint (JSON-RPC 2.0)
    if (mcp_server_enabled_) {
        std::string mcp_path = base + (base.back() == '/' ? "" : "/") + "mcp";
        server.Post(mcp_path.c_str(), [this](const httplib::Request& req, httplib::Response& res) {
            add_security_headers(res);

            if (req.body.empty()) {
                json err = json::object({
                    {"jsonrpc", "2.0"}, {"id", nullptr},
                    {"error", json::object({{"code", -32700}, {"message", "Parse error"}})}
                });
                res.set_content(err.dump(), "application/json");
                return;
            }

            json body;
            try {
                body = json::parse(req.body);
            } catch (const json::parse_error& e) {
                json err = json::object({
                    {"jsonrpc", "2.0"}, {"id", nullptr},
                    {"error", json::object({{"code", -32700}, {"message", std::string("Parse error: ") + e.what()}})}
                });
                res.set_content(err.dump(), "application/json");
                return;
            }

            json response = handle_mcp_request(body);
            res.set_content(response.dump(), "application/json");
        });
    }

    // Debug routes
    if (debug_routes_) {
        std::string debug_path = base + (base.back() == '/' ? "" : "/") + "debug";
        server.Get(debug_path.c_str(), [this](const httplib::Request&, httplib::Response& res) {
            add_security_headers(res);
            json info;
            info["name"] = name_;
            info["route"] = route_;
            info["tools"] = list_tools();
            info["skills"] = list_skills();
            info["hints"] = hints_;
            res.set_content(info.dump(2), "application/json");
        });
    }
}

// ============================================================================
// Server Lifecycle
// ============================================================================

void AgentBase::serve() {
    init_auth();

    server_ = std::make_unique<httplib::Server>();
    server_->set_payload_max_length(1024 * 1024); // 1MB body limit

    setup_routes(*server_);

    get_logger().info("Starting agent '" + name_ + "' on " + host_ + ":" + std::to_string(port_) + route_);
    get_logger().info("Auth user: " + auth_user_);

    if (!server_->listen(host_, port_)) {
        get_logger().error("Failed to start server on " + host_ + ":" + std::to_string(port_) +
                           " -- is the port already in use?");
    }
}

void AgentBase::run() {
    serve();
}

void AgentBase::stop() {
    if (server_) {
        server_->stop();
        server_.reset();
    }
}

} // namespace agent
} // namespace signalwire
