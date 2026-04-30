#pragma once

#include <string>
#include <functional>
#include <optional>
#include <map>
#include <mutex>
#include <vector>
#include <nlohmann/json.hpp>
#include "signalwire/swml/document.hpp"
#include "signalwire/swml/schema.hpp"
#include "signalwire/utils/schema_utils.hpp"
#include "signalwire/swaig/tool_definition.hpp"
#include "signalwire/swaig/function_result.hpp"
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

/// Base SWML service providing HTTP server, auth, and verb methods.
/// Also hosts SWAIG functions: any Service (sidecar, non-agent verb host)
/// can register tools and serve them on /swaig without subclassing AgentBase.
class Service {
public:
    Service();
    virtual ~Service();

    // ========================================================================
    // Configuration
    // ========================================================================

    /// Set the service name (default: "service")
    Service& set_name(const std::string& name);
    const std::string& name() const { return name_; }

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
    // AuthMixin parity (Python: signalwire.core.mixins.auth_mixin)
    // ========================================================================

    /// Validate provided basic-auth credentials against the configured ones
    /// using a constant-time comparison.
    /// Python parity: ``AuthMixin.validate_basic_auth(username, password)``.
    bool validate_basic_auth(const std::string& username, const std::string& password) const;

    /// Get (user, password) — Python-canonical name.
    /// Python parity: ``AuthMixin.get_basic_auth_credentials``.
    std::pair<std::string, std::string> get_basic_auth_credentials() const;

    /// Get (user, password, source) where source is one of "provided",
    /// "environment", or "generated". Python parity:
    /// ``AuthMixin.get_basic_auth_credentials(include_source=True)``.
    std::tuple<std::string, std::string, std::string>
        get_basic_auth_credentials_with_source() const;

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

    /// SchemaUtils helper bound to this Service.  Mirrors Python's
    /// `self.schema_utils` instance attribute on `SWMLService`.  Built
    /// lazily on first access; the underlying schema is cached so the
    /// helper is cheap to build.
    signalwire::utils::SchemaUtils& schema_utils();
    const signalwire::utils::SchemaUtils& schema_utils() const;

    /// Render the SWML document to JSON
    json render_swml() const;

    // ========================================================================
    // SWAIG tool registry (lifted from AgentBase)
    // ========================================================================

    /// Define a SWAIG function the AI can call.
    Service& define_tool(const std::string& name, const std::string& description,
                          const json& parameters, swaig::ToolHandler handler,
                          bool secure = false);
    Service& define_tool(const swaig::ToolDefinition& tool);

    /// Register a raw SWAIG function definition (e.g. DataMap tools).
    Service& register_swaig_function(const json& func_def);

    /// Dispatch a function call to the registered handler.
    /// Returns a FunctionResult; if the function isn't registered, returns
    /// a FunctionResult with a "Function not found" response.
    virtual swaig::FunctionResult on_function_call(const std::string& name,
                                                    const json& args,
                                                    const json& raw_data);

    bool has_tool(const std::string& name) const;
    std::vector<std::string> list_tool_names() const;

    // ========================================================================
    // ToolRegistry parity (Python: signalwire.core.agent.tools.registry)
    // ========================================================================

    /// Whether a SWAIG function with the given name is registered.
    /// Python parity: ``ToolRegistry.has_function``.
    bool has_function(const std::string& name) const;

    /// Get a registered SWAIG function definition by name.
    /// Returns nullptr when no such function is registered.
    /// Python parity: ``ToolRegistry.get_function``.
    const swaig::ToolDefinition* get_function(const std::string& name) const;

    /// Snapshot of all registered SWAIG functions keyed by name.
    /// Returned by value so subsequent registrations don't mutate the
    /// snapshot. Python parity: ``ToolRegistry.get_all_functions``.
    std::map<std::string, swaig::ToolDefinition> get_all_functions() const;

    /// Remove a registered SWAIG function. Returns true when the
    /// function was found and removed; false when it wasn't registered.
    /// Python parity: ``ToolRegistry.remove_function``.
    bool remove_function(const std::string& name);

    /// Build the introspect payload for the registered tools as a JSON string
    /// shaped like `{"tools":[<each tool's SWAIG definition>]}`. Iterates
    /// `tool_order_` first, falling back to map order for entries registered
    /// only via `register_swaig_function`. Stable across SDKs so the
    /// `swaig-test --example` CLI can parse output uniformly. Used by the
    /// SWAIG_LIST_TOOLS env-var path; pulled out as a separate helper so
    /// tests can assert content without invoking exit().
    std::string build_tool_registry_json() const;
    /// Pure-string extractor: slice the JSON payload between
    /// `__SWAIG_TOOLS_BEGIN__` and `__SWAIG_TOOLS_END__` sentinels in a
    /// captured stdout. Returns empty string if either marker is missing or
    /// the order is wrong. Static so the swaig-test CLI / tests can reuse it.
    static std::string extract_introspect_payload(const std::string& stdout_capture);

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

    /// Customization hook called when SWML is requested. Default
    /// delegates to on_swml_request and returns its result. Subclasses
    /// typically override on_swml_request rather than this method.
    ///
    /// Returns std::nullopt to use the default SWML rendering, or a
    /// non-null JSON with modifications to merge into the rendered
    /// document.
    ///
    /// Python parity: WebMixin.on_request(request_data, callback_path).
    /// The Python third `request` argument is FastAPI-specific and
    /// intentionally not mirrored on the cross-language API.
    virtual std::optional<json> on_request(
        const std::optional<json>& request_data = std::nullopt,
        const std::optional<std::string>& callback_path = std::nullopt);

    /// Customization point for subclasses to modify SWML based on
    /// request data. Default returns std::nullopt (no modification).
    ///
    /// Python parity: WebMixin.on_swml_request(request_data, callback_path).
    virtual std::optional<json> on_swml_request(
        const std::optional<json>& request_data = std::nullopt,
        const std::optional<std::string>& callback_path = std::nullopt);

protected:
    /// Override to customize SWML rendering
    virtual json on_render_swml() const;

    /// Extension point: render the SWML document for the main path or
    /// for GET /swaig. Default returns the currently-built Document.
    /// AgentBase overrides to emit prompt + AI verb at request time.
    virtual json render_main_swml(const httplib::Request& req) const;

    /// Extension point: invoked between argument parsing and function
    /// dispatch on POST /swaig. Returns a target Service* (defaults to
    /// `this`) and an optional short-circuit JSON. If short_circuit is
    /// non-null, it's returned as the SWAIG response without calling
    /// on_function_call. AgentBase overrides for token validation.
    virtual std::pair<Service*, std::optional<json>>
        swaig_pre_dispatch(const json& request_data, const std::string& func_name);

    /// Extension point: register additional HTTP routes. AgentBase uses
    /// this to add /post_prompt, /mcp, etc.
    virtual void register_additional_routes(httplib::Server& server);

    /// Add security headers to response
    static void add_security_headers(httplib::Response& res);

    /// Validate basic auth from a request; returns true if valid
    bool validate_auth(const httplib::Request& req, httplib::Response& res) const;

    /// Handle GET/POST /swaig (lifted from AgentBase).
    void handle_swaig_endpoint(const httplib::Request& req, httplib::Response& res);

    Document document_;
    std::string name_ = "service";
    std::string route_ = "/";
    std::string host_ = "0.0.0.0";
    int port_ = 3000;

    std::string auth_user_;
    std::string auth_pass_;
    bool auth_initialized_ = false;

    Schema schema_;
    /// SchemaUtils helper exposed via schema_utils(). Mutable so the
    /// const accessor can lazy-build it.
    mutable std::unique_ptr<signalwire::utils::SchemaUtils> schema_utils_;

    // SWAIG tool registry — protected so subclasses can read/write directly
    // when needed (e.g. AgentBase's per-tool secure flag, BuildSwaigBlock).
    std::map<std::string, swaig::ToolDefinition> tools_;
    std::vector<std::string> tool_order_;
    std::vector<json> registered_swaig_functions_;

    void setup_routes(httplib::Server& server);

private:
    void init_auth();

    std::unique_ptr<httplib::Server> server_;
};

} // namespace swml
} // namespace signalwire
