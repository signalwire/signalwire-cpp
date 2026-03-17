#include "signalwire/swml/service.hpp"
#include "signalwire/common.hpp"
#include "httplib.h"
#include <openssl/crypto.h>
#include <openssl/rand.h>

namespace signalwire {
namespace swml {

Service::Service() {
    schema_.load_embedded();
}

Service::~Service() {
    stop();
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

void Service::setup_routes(httplib::Server& server) {
    // Health endpoint (no auth)
    server.Get("/health", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("{\"status\":\"healthy\"}", "application/json");
    });

    // Ready endpoint (no auth)
    server.Get("/ready", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("{\"status\":\"ready\"}", "application/json");
    });

    // Main SWML endpoint
    auto swml_handler = [this](const httplib::Request& req, httplib::Response& res) {
        add_security_headers(res);
        if (!validate_auth(req, res)) return;
        json swml = render_swml();
        res.set_content(swml.dump(), "application/json");
    };
    server.Get(route_.c_str(), swml_handler);
    server.Post(route_.c_str(), swml_handler);
}

void Service::serve() {
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
