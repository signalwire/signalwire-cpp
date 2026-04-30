#include "signalwire/swml/service.hpp"
#include "signalwire/common.hpp"
#include "httplib.h"
#include <openssl/crypto.h>
#include <openssl/rand.h>
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <regex>

namespace signalwire {
namespace swml {

namespace {
const std::regex kSwaigFnName("^[a-zA-Z_][a-zA-Z0-9_]*$");
}

Service::Service() {
    schema_.load_embedded();
}

signalwire::utils::SchemaUtils& Service::schema_utils() {
    if (!schema_utils_) {
        schema_utils_ = std::make_unique<signalwire::utils::SchemaUtils>();
    }
    return *schema_utils_;
}

const signalwire::utils::SchemaUtils& Service::schema_utils() const {
    if (!schema_utils_) {
        schema_utils_ = std::make_unique<signalwire::utils::SchemaUtils>();
    }
    return *schema_utils_;
}

Service::~Service() {
    stop();
}

Service& Service::set_name(const std::string& name) {
    name_ = name;
    return *this;
}

Service& Service::set_route(const std::string& route) {
    route_ = route;
    if (!route_.empty() && route_.front() != '/') route_ = "/" + route_;
    return *this;
}

Service& Service::set_host(const std::string& host) {
    host_ = host;
    return *this;
}

Service& Service::set_port(int port) {
    port_ = port;
    return *this;
}

Service& Service::set_auth(const std::string& username, const std::string& password) {
    auth_user_ = username;
    auth_pass_ = password;
    auth_initialized_ = true;
    return *this;
}

void Service::init_auth() {
    if (auth_initialized_) return;

    std::string env_user = signalwire::get_env("SWML_BASIC_AUTH_USER");
    std::string env_pass = signalwire::get_env("SWML_BASIC_AUTH_PASSWORD");

    if (!env_user.empty()) auth_user_ = env_user;
    else auth_user_ = "agent";

    if (!env_pass.empty()) auth_pass_ = env_pass;
    else {
        auth_pass_ = generate_random_hex(16);
        if (auth_pass_.empty()) {
            throw std::runtime_error("Failed to generate random password - refusing to start with weak credentials");
        }
    }
    auth_initialized_ = true;
    get_logger().info("Auth configured for user: " + auth_user_);
}

bool Service::timing_safe_compare(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    return CRYPTO_memcmp(a.data(), b.data(), a.size()) == 0;
}

bool Service::validate_basic_auth(const std::string& username, const std::string& password) const {
    return timing_safe_compare(username, auth_user_)
        && timing_safe_compare(password, auth_pass_);
}

std::pair<std::string, std::string> Service::get_basic_auth_credentials() const {
    return {auth_user_, auth_pass_};
}

std::tuple<std::string, std::string, std::string>
Service::get_basic_auth_credentials_with_source() const {
    const char* env_user = std::getenv("SWML_BASIC_AUTH_USER");
    const char* env_pass = std::getenv("SWML_BASIC_AUTH_PASSWORD");
    std::string source;
    if (env_user && env_pass && std::string(env_user) == auth_user_
        && std::string(env_pass) == auth_pass_
        && !auth_user_.empty() && !auth_pass_.empty()) {
        source = "environment";
    } else if (auth_user_.rfind("user_", 0) == 0 && auth_pass_.size() > 20) {
        source = "generated";
    } else {
        source = "provided";
    }
    return std::make_tuple(auth_user_, auth_pass_, source);
}

std::string Service::generate_random_hex(size_t bytes) {
    std::vector<unsigned char> buf(bytes);
    if (RAND_bytes(buf.data(), static_cast<int>(bytes)) != 1) {
        return "";
    }
    std::string hex;
    hex.reserve(bytes * 2);
    for (auto b : buf) {
        char h[3];
        std::snprintf(h, sizeof(h), "%02x", b);
        hex += h;
    }
    return hex;
}

void Service::add_security_headers(httplib::Response& res) {
    res.set_header("X-Content-Type-Options", "nosniff");
    res.set_header("X-Frame-Options", "DENY");
    res.set_header("Cache-Control", "no-store");
}

bool Service::validate_auth(const httplib::Request& req, httplib::Response& res) const {
    auto it = req.headers.find("Authorization");
    if (it == req.headers.end()) {
        res.status = 401;
        res.set_header("WWW-Authenticate", "Basic realm=\"SignalWire Agent\"");
        res.set_content("{\"error\":\"unauthorized\"}", "application/json");
        return false;
    }

    std::string auth_header = it->second;
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

    if (!timing_safe_compare(user, auth_user_) || !timing_safe_compare(pass, auth_pass_)) {
        res.status = 401;
        res.set_content("{\"error\":\"invalid credentials\"}", "application/json");
        return false;
    }

    return true;
}

// ========================================================================
// Verb methods
// ========================================================================

Service& Service::add_verb(const std::string& section, const std::string& verb_name, const json& params) {
    document_.add_verb_to_section(section, verb_name, params);
    return *this;
}

#define VERB_METHOD(method_name, swml_name) \
    Service& Service::method_name(const json& params) { \
        document_.add_verb(swml_name, params); \
        return *this; \
    }

VERB_METHOD(answer, "answer")
VERB_METHOD(ai, "ai")
VERB_METHOD(amazon_bedrock, "amazon_bedrock")
VERB_METHOD(cond, "cond")
VERB_METHOD(connect, "connect")
VERB_METHOD(denoise, "denoise")
VERB_METHOD(detect_machine, "detect_machine")
VERB_METHOD(enter_queue, "enter_queue")
VERB_METHOD(execute, "execute")
VERB_METHOD(goto_section, "goto")
VERB_METHOD(hangup, "hangup")
VERB_METHOD(join_conference, "join_conference")
VERB_METHOD(join_room, "join_room")
VERB_METHOD(label, "label")
VERB_METHOD(live_transcribe, "live_transcribe")
VERB_METHOD(live_translate, "live_translate")
VERB_METHOD(pay, "pay")
VERB_METHOD(play, "play")
VERB_METHOD(prompt, "prompt")
VERB_METHOD(receive_fax, "receive_fax")
VERB_METHOD(record, "record")
VERB_METHOD(record_call, "record_call")
VERB_METHOD(request, "request")
VERB_METHOD(return_section, "return")
VERB_METHOD(send_digits, "send_digits")
VERB_METHOD(send_fax, "send_fax")
VERB_METHOD(send_sms, "send_sms")
VERB_METHOD(set, "set")
VERB_METHOD(sip_refer, "sip_refer")
VERB_METHOD(stop_denoise, "stop_denoise")
VERB_METHOD(stop_record_call, "stop_record_call")
VERB_METHOD(stop_tap, "stop_tap")
VERB_METHOD(switch_section, "switch")
VERB_METHOD(tap, "tap")
VERB_METHOD(transfer, "transfer")
VERB_METHOD(unset, "unset")
VERB_METHOD(user_event, "user_event")

#undef VERB_METHOD

Service& Service::sleep(int milliseconds) {
    document_.add_verb("sleep", json(milliseconds));
    return *this;
}

json Service::render_swml() const {
    return on_render_swml();
}

json Service::on_render_swml() const {
    return document_.to_json();
}

json Service::render_main_swml(const httplib::Request&) const {
    return on_render_swml();
}

std::optional<json> Service::on_request(
    const std::optional<json>& request_data,
    const std::optional<std::string>& callback_path) {
    return on_swml_request(request_data, callback_path);
}

std::optional<json> Service::on_swml_request(
    const std::optional<json>& /*request_data*/,
    const std::optional<std::string>& /*callback_path*/) {
    return std::nullopt;
}

std::pair<Service*, std::optional<json>>
Service::swaig_pre_dispatch(const json&, const std::string&) {
    return { this, std::nullopt };
}

void Service::register_additional_routes(httplib::Server&) {}

// ============================================================================
// SWAIG tool registry (lifted from AgentBase)
// ============================================================================

Service& Service::define_tool(const std::string& name, const std::string& description,
                                const json& parameters, swaig::ToolHandler handler,
                                bool secure) {
    swaig::ToolDefinition td;
    td.name = name;
    td.description = description;
    td.parameters = parameters;
    td.handler = std::move(handler);
    td.secure = secure;
    tools_[name] = std::move(td);
    if (std::find(tool_order_.begin(), tool_order_.end(), name) == tool_order_.end()) {
        tool_order_.push_back(name);
    }
    return *this;
}

Service& Service::define_tool(const swaig::ToolDefinition& tool) {
    tools_[tool.name] = tool;
    if (std::find(tool_order_.begin(), tool_order_.end(), tool.name) == tool_order_.end()) {
        tool_order_.push_back(tool.name);
    }
    return *this;
}

Service& Service::register_swaig_function(const json& func_def) {
    if (!func_def.contains("function")) return *this;
    std::string name = func_def["function"].get<std::string>();
    registered_swaig_functions_.push_back(func_def);
    if (std::find(tool_order_.begin(), tool_order_.end(), name) == tool_order_.end()) {
        tool_order_.push_back(name);
    }
    return *this;
}

swaig::FunctionResult Service::on_function_call(const std::string& name,
                                                  const json& args,
                                                  const json& raw_data) {
    auto it = tools_.find(name);
    if (it == tools_.end() || !it->second.handler) {
        swaig::FunctionResult fr;
        fr.set_response("Function '" + name + "' not found");
        return fr;
    }
    return it->second.handler(args, raw_data);
}

bool Service::has_tool(const std::string& name) const {
    return tools_.count(name) > 0;
}

std::vector<std::string> Service::list_tool_names() const {
    return tool_order_;
}

bool Service::has_function(const std::string& name) const {
    return tools_.count(name) > 0;
}

const swaig::ToolDefinition* Service::get_function(const std::string& name) const {
    auto it = tools_.find(name);
    if (it == tools_.end()) return nullptr;
    return &it->second;
}

std::map<std::string, swaig::ToolDefinition> Service::get_all_functions() const {
    return tools_;
}

bool Service::remove_function(const std::string& name) {
    auto it = tools_.find(name);
    if (it == tools_.end()) return false;
    tools_.erase(it);
    tool_order_.erase(
        std::remove(tool_order_.begin(), tool_order_.end(), name),
        tool_order_.end());
    return true;
}

std::string Service::build_tool_registry_json() const {
    // Build `{"tools":[...]}` capturing the runtime registry. Iterate
    // tool_order_ first (preserves registration order), then any
    // register_swaig_function entries whose name didn't surface there.
    json arr = json::array();
    std::vector<std::string> seen;
    for (const auto& name : tool_order_) {
        auto it = tools_.find(name);
        if (it != tools_.end()) {
            arr.push_back(it->second.to_swaig_json());
            seen.push_back(name);
            continue;
        }
        // Fall back to a raw entry registered via register_swaig_function.
        for (const auto& raw : registered_swaig_functions_) {
            if (raw.contains("function") &&
                raw["function"].is_string() &&
                raw["function"].get<std::string>() == name) {
                arr.push_back(raw);
                seen.push_back(name);
                break;
            }
        }
    }
    // Catch any define_tool entries somehow not in tool_order_.
    for (const auto& [name, td] : tools_) {
        if (std::find(seen.begin(), seen.end(), name) == seen.end()) {
            arr.push_back(td.to_swaig_json());
        }
    }
    json body;
    body["tools"] = arr;
    return body.dump();
}

std::string Service::extract_introspect_payload(const std::string& stdout_capture) {
    static const std::string kBegin = "__SWAIG_TOOLS_BEGIN__";
    static const std::string kEnd = "__SWAIG_TOOLS_END__";
    auto begin = stdout_capture.find(kBegin);
    if (begin == std::string::npos) return std::string();
    size_t after_begin = begin + kBegin.size();
    auto end = stdout_capture.find(kEnd, after_begin);
    if (end == std::string::npos) return std::string();
    std::string slice = stdout_capture.substr(after_begin, end - after_begin);
    // Trim leading/trailing whitespace (newlines).
    size_t s = slice.find_first_not_of(" \t\r\n");
    if (s == std::string::npos) return std::string();
    size_t e = slice.find_last_not_of(" \t\r\n");
    return slice.substr(s, e - s + 1);
}

void Service::handle_swaig_endpoint(const httplib::Request& req, httplib::Response& res) {
    add_security_headers(res);
    if (!validate_auth(req, res)) return;

    if (req.method == "GET") {
        json swml = render_main_swml(req);
        res.set_content(swml.dump(), "application/json");
        return;
    }

    json payload;
    try {
        payload = req.body.empty() ? json::object() : json::parse(req.body);
    } catch (const std::exception&) {
        res.status = 400;
        res.set_content("{\"error\":\"Invalid JSON\"}", "application/json");
        return;
    }
    if (!payload.is_object() || !payload.contains("function")) {
        res.status = 400;
        res.set_content("{\"error\":\"Missing function name\"}", "application/json");
        return;
    }
    std::string func_name = payload["function"].get<std::string>();
    if (func_name.empty()) {
        res.status = 400;
        res.set_content("{\"error\":\"Missing function name\"}", "application/json");
        return;
    }
    if (!std::regex_match(func_name, kSwaigFnName)) {
        res.status = 400;
        res.set_content(json{{"error", "Invalid function name format: '" + func_name + "'"}}.dump(),
                        "application/json");
        return;
    }

    // Argument extraction: nested {argument:{parsed:[...]}} OR flat {arguments}
    json args = json::object();
    if (payload.contains("argument") && payload["argument"].is_object()) {
        const auto& arg = payload["argument"];
        if (arg.contains("parsed") && arg["parsed"].is_array() && !arg["parsed"].empty()) {
            args = arg["parsed"][0];
        }
    } else if (payload.contains("arguments") && payload["arguments"].is_object()) {
        args = payload["arguments"];
    }

    auto [target, short_circuit] = swaig_pre_dispatch(payload, func_name);
    if (short_circuit.has_value()) {
        res.set_content(short_circuit->dump(), "application/json");
        return;
    }

    auto result = target->on_function_call(func_name, args, payload);
    res.set_content(result.to_json().dump(), "application/json");
}

void Service::setup_routes(httplib::Server& server) {
    // Health endpoint (no auth)
    server.Get("/health", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("{\"status\":\"healthy\"}", "application/json");
    });

    // Ready endpoint (no auth)
    server.Get("/ready", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("{\"status\":\"ready\"}", "application/json");
    });

    // SWAIG endpoint — GET returns SWML, POST dispatches a tool.
    std::string base_path = (route_ == "/") ? std::string() : route_;
    auto swaig_handler = [this](const httplib::Request& req, httplib::Response& res) {
        handle_swaig_endpoint(req, res);
    };
    server.Get((base_path + "/swaig").c_str(), swaig_handler);
    server.Post((base_path + "/swaig").c_str(), swaig_handler);

    // Subclass extension hook (AgentBase adds /post_prompt, /mcp).
    register_additional_routes(server);

    // Main SWML endpoint
    auto swml_handler = [this](const httplib::Request& req, httplib::Response& res) {
        add_security_headers(res);
        if (!validate_auth(req, res)) return;
        json swml = render_main_swml(req);
        res.set_content(swml.dump(), "application/json");
    };
    server.Get(route_.c_str(), swml_handler);
    server.Post(route_.c_str(), swml_handler);
}

void Service::serve() {
    // Introspect path: when SWAIG_LIST_TOOLS is set, dump the runtime tool
    // registry to stdout between sentinel markers and exit BEFORE binding
    // any port. This is how the swaig-test --example CLI lists tools on a
    // compiled SWMLService example without standing up an HTTP server.
    if (std::getenv("SWAIG_LIST_TOOLS")) {
        std::cout << "__SWAIG_TOOLS_BEGIN__\n"
                  << build_tool_registry_json() << "\n"
                  << "__SWAIG_TOOLS_END__\n";
        std::cout.flush();
        std::exit(0);
    }

    init_auth();

    const char* env_port = std::getenv("PORT");
    if (env_port) {
        try { port_ = std::stoi(env_port); } catch (...) {}
    }

    server_ = std::make_unique<httplib::Server>();
    server_->set_payload_max_length(1024 * 1024); // 1MB limit
    setup_routes(*server_);
    get_logger().info("Starting SWML service on " + host_ + ":" + std::to_string(port_) + route_);
    if (!server_->listen(host_, port_)) {
        get_logger().error("Failed to start server on " + host_ + ":" + std::to_string(port_) +
                           " -- is the port already in use?");
    }
}

void Service::stop() {
    if (server_) {
        server_->stop();
        server_.reset();
    }
}

} // namespace swml
} // namespace signalwire
