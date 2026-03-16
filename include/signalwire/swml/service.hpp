#pragma once

#include <string>
#include <functional>
#include <optional>
#include <map>
#include <mutex>
#include <nlohmann/json.hpp>
#include "signalwire/swml/document.hpp"
#include "signalwire/swml/schema.hpp"
#include "signalwire/logging.hpp"

// Forward declare httplib types to avoid including the massive header here
namespace httplib {
    class Server;
    class SSLServer;
    class Request;
    class Response;
}

namespace signalwire {
namespace swml {

using json = nlohmann::json;

/// Base SWML service providing HTTP server, auth, and verb methods
class Service {
public:
    Service();
    virtual ~Service();

    // ========================================================================
    // Configuration
    // ========================================================================

    /// Set the route path for this service (default: "/")
    Service& set_route(const std::string& route);
    const std::string& route() const { return route_; }

    /// Set the host to bind to
    Service& set_host(const std::string& host);

    /// Set the port to listen on
    Service& set_port(int port);

    /// Set basic auth credentials (auto-generated if not set)
    Service& set_auth(const std::string& username, const std::string& password);

    /// Get the username for basic auth
    const std::string& auth_username() const { return auth_user_; }

    /// Get the password for basic auth
    const std::string& auth_password() const { return auth_pass_; }

    // ========================================================================
    // All 38 SWML Verb Methods
    // ========================================================================

    Service& answer(const json& params = json::object());
    Service& ai(const json& params = json::object());
    Service& amazon_bedrock(const json& params = json::object());
    Service& cond(const json& params = json::array());
    Service& connect(const json& params = json::object());
    Service& denoise(const json& params = json::object());
    Service& detect_machine(const json& params = json::object());
    Service& enter_queue(const json& params = json::object());
    Service& execute(const json& params = json::object());
    Service& goto_section(const json& params = json::object());
    Service& hangup(const json& params = json::object());
    Service& join_conference(const json& params = json::object());
    Service& join_room(const json& params = json::object());
    Service& label(const json& params = json::object());
    Service& live_transcribe(const json& params = json::object());
    Service& live_translate(const json& params = json::object());
    Service& pay(const json& params = json::object());
    Service& play(const json& params = json::object());
    Service& prompt(const json& params = json::object());
    Service& receive_fax(const json& params = json::object());
    Service& record(const json& params = json::object());
    Service& record_call(const json& params = json::object());
    Service& request(const json& params = json::object());
    Service& return_section(const json& params = json::object());
    Service& send_digits(const json& params = json::object());
    Service& send_fax(const json& params = json::object());
    Service& send_sms(const json& params = json::object());
    Service& set(const json& params = json::object());
    Service& sleep(int milliseconds);
    Service& sip_refer(const json& params = json::object());
    Service& stop_denoise(const json& params = json::object());
    Service& stop_record_call(const json& params = json::object());
    Service& stop_tap(const json& params = json::object());
    Service& switch_section(const json& params = json::object());
    Service& tap(const json& params = json::object());
    Service& transfer(const json& params = json::object());
    Service& unset(const json& params = json::object());
    Service& user_event(const json& params = json::object());

    // ========================================================================
    // Verb helpers — add to a specific section
    // ========================================================================

    Service& add_verb(const std::string& section, const std::string& verb_name, const json& params);

    // ========================================================================
    // Document access
    // ========================================================================

    /// Get the underlying SWML document
    Document& document() { return document_; }
    const Document& document() const { return document_; }

    /// Render the SWML document to JSON
    json render_swml() const;

    // ========================================================================
    // HTTP Server
    // ========================================================================

    /// Start the HTTP server (blocking)
    void serve();

    /// Stop the HTTP server
    void stop();

    /// Get the effective port
    int port() const { return port_; }

    /// Timing-safe string comparison using CRYPTO_memcmp
    static bool timing_safe_compare(const std::string& a, const std::string& b);

    /// Generate a random hex string of given byte length
    static std::string generate_random_hex(size_t bytes);

protected:
    /// Override to customize SWML rendering
    virtual json on_render_swml() const;

    /// Add security headers to response
    static void add_security_headers(httplib::Response& res);

    /// Validate basic auth from a request; returns true if valid
    bool validate_auth(const httplib::Request& req, httplib::Response& res) const;

    Document document_;
    std::string route_ = "/";
    std::string host_ = "0.0.0.0";
    int port_ = 3000;

    std::string auth_user_;
    std::string auth_pass_;
    bool auth_initialized_ = false;

    Schema schema_;

private:
    void init_auth();
    void setup_routes(httplib::Server& server);

    std::unique_ptr<httplib::Server> server_;
};

} // namespace swml
} // namespace signalwire
