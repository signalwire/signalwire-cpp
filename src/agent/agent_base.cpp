// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#include "signalwire/agent/agent_base.hpp"
#include "signalwire/common.hpp"
#include "httplib.h"
#include <openssl/crypto.h>
#include <openssl/rand.h>
#include <algorithm>

namespace signalwire {
namespace agent {

// ============================================================================
// Constructor / Destructor
// ============================================================================

AgentBase::AgentBase(const std::string& name, const std::string& route,
                     const std::string& host, int port)
    : name_(name), route_(route), host_(host), port_(port) {
    if (!route_.empty() && route_.front() != '/') {
        route_ = "/" + route_;
    }
    // Set default port from env
    std::string env_port = get_env("PORT", "");
    if (!env_port.empty()) {
        try { port_ = std::stoi(env_port); } catch (...) {}
    }
}

AgentBase::~AgentBase() {
    stop();
}

AgentBase::AgentBase(const AgentBase& other)
    : name_(other.name_), route_(other.route_), host_(other.host_), port_(other.port_),
      auth_user_(other.auth_user_), auth_pass_(other.auth_pass_),
      auth_initialized_(other.auth_initialized_),
      raw_prompt_text_(other.raw_prompt_text_),
      post_prompt_text_(other.post_prompt_text_),
      post_prompt_url_(other.post_prompt_url_),
      pom_sections_(other.pom_sections_),
      use_pom_(other.use_pom_),
      tools_(other.tools_), tool_order_(other.tool_order_),
      datamap_functions_(other.datamap_functions_),
      function_includes_(other.function_includes_),
      hints_(other.hints_), languages_(other.languages_),
      pronunciations_(other.pronunciations_),
      ai_params_(other.ai_params_), global_data_(other.global_data_),
      native_functions_(other.native_functions_),
      internal_fillers_(other.internal_fillers_),
      debug_events_(other.debug_events_),
      prompt_llm_params_(other.prompt_llm_params_),
      post_prompt_llm_params_(other.post_prompt_llm_params_),
      pre_answer_verbs_(other.pre_answer_verbs_),
      answer_verbs_(other.answer_verbs_),
      post_answer_verbs_(other.post_answer_verbs_),
      post_ai_verbs_(other.post_ai_verbs_),
      context_builder_(other.context_builder_),
      loaded_skills_(other.loaded_skills_),
      skill_configs_(other.skill_configs_),
      proxy_url_(other.proxy_url_),
      webhook_url_(other.webhook_url_),
      swaig_query_params_(other.swaig_query_params_),
      debug_routes_(other.debug_routes_),
      sip_routing_enabled_(other.sip_routing_enabled_),
      sip_usernames_(other.sip_usernames_),
      auto_map_sip_(other.auto_map_sip_),
      summary_callback_(other.summary_callback_),
      debug_event_callback_(other.debug_event_callback_) {
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
        if (section.title == parent_title) {
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
        if (section.title == title) {
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
        if (s.title == title) return true;
    }
    return false;
}

std::string AgentBase::get_prompt() const {
    if (raw_prompt_text_) return *raw_prompt_text_;
    std::string result;
    for (const auto& s : pom_sections_) {
        result += "## " + s.title + "\n";
        if (!s.body.empty()) result += s.body + "\n";
        for (const auto& b : s.bullets) result += "- " + b + "\n";
        for (const auto& sub : s.subsections) {
            result += "### " + sub.title + "\n";
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

// ============================================================================
// Tool Methods
// ============================================================================

AgentBase& AgentBase::define_tool(const swaig::ToolDefinition& tool) {
    tools_[tool.name] = tool;
    if (std::find(tool_order_.begin(), tool_order_.end(), tool.name) == tool_order_.end()) {
        tool_order_.push_back(tool.name);
    }
    return *this;
}

AgentBase& AgentBase::define_tool(const std::string& name, const std::string& description,
                                   const json& parameters, swaig::ToolHandler handler,
                                   bool secure) {
    swaig::ToolDefinition tool;
    tool.name = name;
    tool.description = description;
    tool.parameters = parameters;
    tool.handler = std::move(handler);
    tool.secure = secure;
    return define_tool(tool);
}

AgentBase& AgentBase::register_swaig_function(const json& func_def) {
    datamap_functions_.push_back(func_def);
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

    return it->second.handler(args, raw_data);
}

bool AgentBase::has_tool(const std::string& name) const {
    return tools_.find(name) != tools_.end();
}

std::vector<std::string> AgentBase::list_tools() const {
    return tool_order_;
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

AgentBase& AgentBase::set_internal_fillers(const json& fillers) {
    internal_fillers_ = fillers;
    return *this;
}

AgentBase& AgentBase::add_internal_filler(const std::string& lang,
                                           const std::vector<std::string>& fillers) {
    if (internal_fillers_.is_null()) internal_fillers_ = json::object();
    internal_fillers_[lang] = fillers;
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
    }
    return *context_builder_;
}

contexts::Context& AgentBase::add_context(const std::string& name) {
    return define_contexts().add_context(name);
}

bool AgentBase::has_contexts() const {
    return context_builder_ && context_builder_->has_contexts();
}

// ============================================================================
// Skills Methods
// ============================================================================

AgentBase& AgentBase::add_skill(const std::string& skill_name, const json& params) {
    loaded_skills_.push_back(skill_name);
    skill_configs_[skill_name] = params;
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

void AgentBase::init_auth() {
    if (auth_initialized_) return;

    std::string env_user = get_env("SWML_BASIC_AUTH_USER");
    std::string env_pass = get_env("SWML_BASIC_AUTH_PASSWORD");

    auth_user_ = env_user.empty() ? name_ : env_user;
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
    }
    auth_initialized_ = true;
    get_logger().info("Auth configured for user: " + auth_user_);
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

    // Health (no auth)
    server.Get("/health", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("{\"status\":\"healthy\"}", "application/json");
    });

    // Ready (no auth)
    server.Get("/ready", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("{\"status\":\"ready\"}", "application/json");
    });

    // SWML endpoint
    auto swml_handler = [this](const httplib::Request& req, httplib::Response& res) {
        handle_swml_request(req, res);
    };
    server.Get(base.c_str(), swml_handler);
    server.Post(base.c_str(), swml_handler);

    // SWAIG endpoint
    std::string swaig_path = base + (base.back() == '/' ? "" : "/") + "swaig";
    server.Post(swaig_path.c_str(), [this](const httplib::Request& req, httplib::Response& res) {
        handle_swaig_request(req, res);
    });

    // Post-prompt endpoint
    std::string pp_path = base + (base.back() == '/' ? "" : "/") + "post_prompt";
    server.Post(pp_path.c_str(), [this](const httplib::Request& req, httplib::Response& res) {
        handle_post_prompt_request(req, res);
    });

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
