// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT

#include "signalwire/relay/client.hpp"
#include "signalwire/logging.hpp"

#include <random>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <thread>
#include <algorithm>

namespace signalwire {
namespace relay {

static constexpr const char* AGENT_STRING = "signalwire-agents-cpp/1.0";

// UUID v4 generation
std::string RelayClient::generate_uuid() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<uint32_t> dist(0, 0xFFFFFFFF);

    uint32_t a = dist(gen), b = dist(gen), c = dist(gen), d = dist(gen);
    b = (b & 0xFFFF0FFF) | 0x00004000; // version 4
    c = (c & 0x3FFFFFFF) | 0x80000000; // variant

    std::ostringstream ss;
    ss << std::hex << std::setfill('0');
    ss << std::setw(8) << a << "-";
    ss << std::setw(4) << ((b >> 16) & 0xFFFF) << "-";
    ss << std::setw(4) << (b & 0xFFFF) << "-";
    ss << std::setw(4) << ((c >> 16) & 0xFFFF) << "-";
    ss << std::setw(4) << (c & 0xFFFF);
    ss << std::setw(8) << d;
    return ss.str();
}

RelayClient::RelayClient(const RelayConfig& config)
    : config_(config) {}

RelayClient::RelayClient(const std::string& project,
                         const std::string& token,
                         const std::string& host,
                         const std::vector<std::string>& contexts) {
    config_.project = project;
    config_.token = token;
    config_.host = host;
    config_.contexts = contexts;
}

RelayClient::~RelayClient() {
    disconnect();
}

RelayClient RelayClient::from_env() {
    RelayConfig cfg;
    const char* pid = std::getenv("SIGNALWIRE_PROJECT_ID");
    if (pid) cfg.project = pid;
    const char* tok = std::getenv("SIGNALWIRE_API_TOKEN");
    if (tok) cfg.token = tok;
    const char* host = std::getenv("SIGNALWIRE_SPACE");
    if (host) cfg.host = host;
    return RelayClient(cfg);
}

bool RelayClient::connect() {
    if (connected_.load()) return true;

    ws_ = std::make_unique<WebSocketClient>();

    ws_->on_message([this](const std::string& msg) {
        on_ws_message(msg);
    });

    ws_->on_close([this](int code, const std::string& reason) {
        on_ws_close(code, reason);
    });

    ws_->on_error([](const std::string& err) {
        get_logger().error("WebSocket error: " + err);
    });

    if (!ws_->connect(config_.host, config_.port)) {
        get_logger().error("Failed to connect WebSocket to " + config_.host);
        return false;
    }

    // Authenticate via signalwire.connect
    if (!authenticate()) {
        ws_->close();
        ws_.reset();
        return false;
    }

    connected_.store(true);
    reconnect_delay_ms_ = RECONNECT_BASE_DELAY_MS;
    reconnect_attempts_ = 0;

    get_logger().info("RELAY client connected to " + config_.host);
    return true;
}

void RelayClient::disconnect() {
    running_.store(false);
    connected_.store(false);

    reject_all_pending();

    if (ws_) {
        ws_->close();
        ws_.reset();
    }
}

void RelayClient::run() {
    if (!connect()) {
        get_logger().error("Failed to connect, cannot run");
        return;
    }

    running_.store(true);

    // Block until disconnect or signal
    while (running_.load()) {
        if (!connected_.load()) {
            if (!reconnect()) {
                get_logger().error("Reconnection failed after " +
                    std::to_string(reconnect_attempts_) + " attempts");
                break;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

bool RelayClient::authenticate() {
    json params;
    params["version"] = {{"major", 2}, {"minor", 0}, {"revision", 0}};
    params["agent"] = AGENT_STRING;
    params["event_acks"] = true;
    params["authentication"] = {
        {"project", config_.project},
        {"token", config_.token}
    };

    if (!config_.contexts.empty()) {
        params["contexts"] = config_.contexts;
    }

    // Send protocol on reconnect for session resumption
    if (!protocol_.empty()) {
        params["protocol"] = protocol_;
    }
    if (!authorization_state_.empty()) {
        params["authorization_state"] = authorization_state_;
    }

    try {
        json result = send_request("signalwire.connect", params);
        if (result.contains("protocol")) {
            protocol_ = result["protocol"].get<std::string>();
        }
        if (result.contains("authorization_state")) {
            authorization_state_ = result["authorization_state"].get<std::string>();
        }
        return true;
    } catch (const std::exception& e) {
        get_logger().error(std::string("Authentication failed: ") + e.what());
        return false;
    }
}

json RelayClient::send_request(const std::string& method, const json& params) {
    std::string id = generate_uuid();

    json msg;
    msg["jsonrpc"] = "2.0";
    msg["id"] = id;
    msg["method"] = method;
    msg["params"] = params;

    // Register pending request before sending
    auto pending = std::make_shared<PendingRequest>();
    auto future = pending->promise.get_future();
    {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        pending_requests_[id] = pending;
    }

    // Send the message
    std::string payload = msg.dump();
    if (!ws_ || !ws_->send(payload)) {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        pending_requests_.erase(id);
        throw std::runtime_error("Failed to send WebSocket message");
    }

    // Wait for response with timeout (30 seconds)
    auto status = future.wait_for(std::chrono::seconds(30));
    {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        pending_requests_.erase(id);
    }

    if (status == std::future_status::timeout) {
        throw std::runtime_error("Request timed out: " + method);
    }

    return future.get();
}

void RelayClient::send_response(const std::string& id, const json& result) {
    json msg;
    msg["jsonrpc"] = "2.0";
    msg["id"] = id;
    msg["result"] = result;

    if (ws_) {
        ws_->send(msg.dump());
    }
}

json RelayClient::execute(const std::string& method, const json& params) {
    json full_params = params;
    if (!config_.project.empty()) {
        full_params["project_id"] = config_.project;
    }
    if (!protocol_.empty()) {
        full_params["protocol"] = protocol_;
    }
    return send_request(method, full_params);
}

void RelayClient::on_ws_message(const std::string& message) {
    json msg;
    try {
        msg = json::parse(message);
    } catch (const json::parse_error& e) {
        get_logger().error(std::string("Invalid JSON from server: ") + e.what());
        return;
    }

    // Check if this is a response to a pending request (has "result" or "error")
    if (msg.contains("id") && (msg.contains("result") || msg.contains("error"))) {
        std::string id = msg["id"].get<std::string>();
        std::shared_ptr<PendingRequest> pending;
        {
            std::lock_guard<std::mutex> lock(pending_mutex_);
            auto it = pending_requests_.find(id);
            if (it != pending_requests_.end()) {
                pending = it->second;
            }
        }
        if (pending) {
            if (msg.contains("error")) {
                json err = msg["error"];
                std::string code = std::to_string(err.value("code", 0));
                std::string errmsg = err.value("message", "Unknown error");
                // Resolve with error info in result format for uniform handling
                json error_result;
                error_result["code"] = code;
                error_result["message"] = errmsg;
                pending->promise.set_value(error_result);
            } else {
                pending->promise.set_value(msg["result"]);
            }
        }
        return;
    }

    // Check if this is a server-initiated request (has "method")
    if (msg.contains("method")) {
        std::string method = msg["method"].get<std::string>();
        std::string msg_id = msg.value("id", "");

        if (method == "signalwire.event") {
            // ACK the event immediately
            if (!msg_id.empty()) {
                send_response(msg_id);
            }
            route_event(msg);

        } else if (method == "signalwire.ping") {
            // Respond to ping
            if (!msg_id.empty()) {
                send_response(msg_id);
            }

        } else if (method == "signalwire.disconnect") {
            // Handle server-initiated disconnect
            if (!msg_id.empty()) {
                send_response(msg_id);
            }
            json params = msg.value("params", json::object());
            bool restart = params.value("restart", false);
            if (restart) {
                protocol_.clear();
                authorization_state_.clear();
            }
            // The server will close the socket; reconnect will be triggered by on_ws_close
        }
    }
}

void RelayClient::on_ws_close(int code, const std::string& reason) {
    get_logger().info("WebSocket closed: code=" + std::to_string(code) + " reason=" + reason);
    connected_.store(false);
    reject_all_pending();

    // If still running, trigger reconnect
    if (running_.load()) {
        get_logger().info("Will attempt reconnection...");
    }
}

void RelayClient::route_event(const json& msg) {
    json outer_params = msg.value("params", json::object());
    std::string event_type = outer_params.value("event_type", "");
    json inner_params = outer_params.value("params", json::object());

    RelayEvent ev;
    ev.event_type = event_type;
    ev.params = inner_params;

    // Handle authorization state updates
    if (event_type == "signalwire.authorization.state") {
        std::string auth_state = inner_params.value("authorization_state", "");
        if (!auth_state.empty()) {
            authorization_state_ = auth_state;
        }
        return;
    }

    // Inbound call
    if (event_type == "calling.call.receive") {
        handle_inbound_call(ev);
        return;
    }

    // Dial completion
    if (event_type == "calling.call.dial") {
        handle_dial_event(ev);
        return;
    }

    // Call state changes
    if (event_type == "calling.call.state") {
        handle_call_state(ev);
        return;
    }

    // Messaging events
    if (event_type == "messaging.receive" || event_type == "messaging.state") {
        handle_messaging_event(ev);
        return;
    }

    // Component events (play, record, collect, tap, detect, fax, etc.)
    if (event_type.find("calling.call.") == 0) {
        handle_component_event(ev);
        return;
    }
}

void RelayClient::handle_inbound_call(const RelayEvent& ev) {
    std::string call_id = ev.params.value("call_id", "");
    std::string node_id = ev.params.value("node_id", "");

    if (call_id.empty()) return;

    // Create a new Call object
    auto call = std::make_unique<Call>(call_id, node_id, this);
    call->set_direction("inbound");
    call->set_from(ev.params.value("device", json::object())
                   .value("params", json::object())
                   .value("from_number", ""));
    call->set_to(ev.params.value("device", json::object())
                 .value("params", json::object())
                 .value("to_number", ""));

    Call* call_ptr = call.get();
    register_call(call_id, call_ptr);

    {
        std::lock_guard<std::mutex> lock(owned_calls_mutex_);
        owned_calls_.push_back(std::move(call));
    }

    // Dispatch to handler
    InboundCallHandler handler;
    {
        std::lock_guard<std::mutex> lock(handler_mutex_);
        handler = call_handler_;
    }
    if (handler) {
        try {
            handler(*call_ptr);
        } catch (const std::exception& e) {
            get_logger().error(std::string("Inbound call handler error: ") + e.what());
        }
    }
}

void RelayClient::handle_dial_event(const RelayEvent& ev) {
    DialEvent de = DialEvent::from_relay_event(ev);

    std::shared_ptr<PendingDial> pending;
    {
        std::lock_guard<std::mutex> lock(dials_mutex_);
        auto it = pending_dials_.find(de.tag);
        if (it != pending_dials_.end()) {
            pending = it->second;
            pending_dials_.erase(it);
        }
    }

    if (!pending) return;

    if (de.dial_state == "answered") {
        std::string call_id = de.call_info.value("call_id", "");
        std::string node_id = de.call_info.value("node_id", "");

        // Find existing call or create new one
        Call* call_ptr = find_call(call_id);
        if (!call_ptr) {
            auto call = std::make_unique<Call>(call_id, node_id, this);
            call->set_direction("outbound");
            call->set_tag(de.tag);
            call->update_state(CALL_STATE_ANSWERED);
            call_ptr = call.get();
            register_call(call_id, call_ptr);
            std::lock_guard<std::mutex> lock(owned_calls_mutex_);
            owned_calls_.push_back(std::move(call));
        }
        pending->promise.set_value(call_ptr);
    } else if (de.dial_state == "failed") {
        pending->promise.set_value(nullptr);
    }
}

void RelayClient::handle_call_state(const RelayEvent& ev) {
    CallEvent ce = CallEvent::from_relay_event(ev);
    std::string call_id = ce.call_id;

    if (call_id.empty()) return;

    // During dial, create Call objects for dial legs
    if (!ce.tag.empty()) {
        std::lock_guard<std::mutex> lock(dials_mutex_);
        if (pending_dials_.find(ce.tag) != pending_dials_.end()) {
            // This is a dial leg - create/update call
            Call* existing = find_call(call_id);
            if (!existing) {
                auto call = std::make_unique<Call>(call_id, ce.node_id, this);
                call->set_direction("outbound");
                call->set_tag(ce.tag);
                Call* ptr = call.get();
                register_call(call_id, ptr);
                std::lock_guard<std::mutex> lock2(owned_calls_mutex_);
                owned_calls_.push_back(std::move(call));
            }
        }
    }

    // Route to the Call object
    Call* call = find_call(call_id);
    if (call) {
        call->update_state(ce.call_state);
        call->dispatch_event(ce);

        // Remove ended calls from registry
        if (ce.call_state == CALL_STATE_ENDED) {
            unregister_call(call_id);
        }
    }
}

void RelayClient::handle_component_event(const RelayEvent& ev) {
    ComponentEvent ce = ComponentEvent::from_relay_event(ev);
    std::string call_id = ce.call_id;
    std::string control_id = ce.control_id;

    if (call_id.empty()) return;

    Call* call = find_call(call_id);
    if (!call) return;

    // Route to the action by control_id
    if (!control_id.empty()) {
        Action* action = call->find_action(control_id);
        if (action) {
            action->update_state(ce.state, ev.params);
            if (action->completed()) {
                call->unregister_action(control_id);
            }
        }
    }

    // Also dispatch as a call event
    CallEvent call_ev = CallEvent::from_relay_event(ev);
    call->dispatch_event(call_ev);
}

void RelayClient::handle_messaging_event(const RelayEvent& ev) {
    if (ev.event_type == "messaging.receive") {
        Message msg = Message::from_params(ev.params);
        InboundMessageHandler handler;
        {
            std::lock_guard<std::mutex> lock(handler_mutex_);
            handler = message_handler_;
        }
        if (handler) {
            try {
                handler(msg);
            } catch (const std::exception& e) {
                get_logger().error(std::string("Message handler error: ") + e.what());
            }
        }
    } else if (ev.event_type == "messaging.state") {
        MessageEvent me = MessageEvent::from_relay_event(ev);
        std::lock_guard<std::mutex> lock(messages_mutex_);
        auto it = messages_.find(me.message_id);
        if (it != messages_.end()) {
            it->second->update_state(me.message_state);
        }
    }
}

void RelayClient::on_call(InboundCallHandler handler) {
    std::lock_guard<std::mutex> lock(handler_mutex_);
    call_handler_ = std::move(handler);
}

Call RelayClient::dial(const json& devices) {
    std::string tag = generate_uuid();

    // Register pending dial before sending RPC
    auto pending = std::make_shared<PendingDial>();
    auto future = pending->promise.get_future();
    {
        std::lock_guard<std::mutex> lock(dials_mutex_);
        pending_dials_[tag] = pending;
    }

    // Send the dial RPC
    json params;
    params["tag"] = tag;
    params["devices"] = devices;

    try {
        execute("calling.dial", params);
    } catch (const std::exception& e) {
        std::lock_guard<std::mutex> lock(dials_mutex_);
        pending_dials_.erase(tag);
        get_logger().error(std::string("Dial RPC failed: ") + e.what());
        return Call();
    }

    // Wait for the dial event (with timeout)
    auto status = future.wait_for(std::chrono::seconds(120));
    {
        std::lock_guard<std::mutex> lock(dials_mutex_);
        pending_dials_.erase(tag);
    }

    if (status == std::future_status::timeout) {
        get_logger().error("Dial timed out waiting for answer");
        return Call();
    }

    Call* call_ptr = future.get();
    if (call_ptr) {
        return *call_ptr;
    }
    return Call();
}

void RelayClient::on_message(InboundMessageHandler handler) {
    std::lock_guard<std::mutex> lock(handler_mutex_);
    message_handler_ = std::move(handler);
}

std::string RelayClient::send_message(const std::string& from, const std::string& to,
                                       const std::string& body,
                                       const std::vector<std::string>& media,
                                       const std::vector<std::string>& tags,
                                       const std::string& region) {
    json params;
    params["from_number"] = from;
    params["to_number"] = to;
    params["body"] = body;
    if (!media.empty()) params["media"] = media;
    if (!tags.empty()) params["tags"] = tags;
    if (!region.empty()) params["region"] = region;

    try {
        json result = execute("messaging.send", params);
        return result.value("message_id", "");
    } catch (const std::exception& e) {
        get_logger().error(std::string("send_message failed: ") + e.what());
        return "";
    }
}

void RelayClient::subscribe(const std::vector<std::string>& contexts) {
    for (const auto& c : contexts) {
        config_.contexts.push_back(c);
    }
    if (connected_.load()) {
        json params;
        params["contexts"] = contexts;
        try {
            execute("signalwire.receive", params);
        } catch (const std::exception& e) {
            get_logger().error(std::string("Subscribe failed: ") + e.what());
        }
    }
}

void RelayClient::unsubscribe(const std::vector<std::string>& contexts) {
    // Remove from local list
    for (const auto& ctx : contexts) {
        config_.contexts.erase(
            std::remove(config_.contexts.begin(), config_.contexts.end(), ctx),
            config_.contexts.end());
    }
    if (connected_.load()) {
        json params;
        params["contexts"] = contexts;
        try {
            execute("signalwire.unreceive", params);
        } catch (const std::exception& e) {
            get_logger().error(std::string("Unsubscribe failed: ") + e.what());
        }
    }
}

void RelayClient::register_call(const std::string& call_id, Call* call) {
    std::lock_guard<std::mutex> lock(calls_mutex_);
    calls_[call_id] = call;
}

void RelayClient::unregister_call(const std::string& call_id) {
    std::lock_guard<std::mutex> lock(calls_mutex_);
    calls_.erase(call_id);
}

Call* RelayClient::find_call(const std::string& call_id) {
    std::lock_guard<std::mutex> lock(calls_mutex_);
    auto it = calls_.find(call_id);
    return it != calls_.end() ? it->second : nullptr;
}

bool RelayClient::reconnect() {
    while (running_.load() && reconnect_attempts_ < MAX_RECONNECT_ATTEMPTS) {
        reconnect_attempts_++;

        get_logger().info("Reconnect attempt " + std::to_string(reconnect_attempts_) +
                         " in " + std::to_string(reconnect_delay_ms_) + "ms");

        std::this_thread::sleep_for(std::chrono::milliseconds(reconnect_delay_ms_));

        if (!running_.load()) return false;

        // Create new WebSocket
        ws_ = std::make_unique<WebSocketClient>();
        ws_->on_message([this](const std::string& msg) { on_ws_message(msg); });
        ws_->on_close([this](int code, const std::string& reason) { on_ws_close(code, reason); });
        ws_->on_error([](const std::string& err) {
            get_logger().error("WebSocket error: " + err);
        });

        if (ws_->connect(config_.host, config_.port)) {
            if (authenticate()) {
                connected_.store(true);
                reconnect_delay_ms_ = RECONNECT_BASE_DELAY_MS;
                reconnect_attempts_ = 0;
                get_logger().info("Reconnected successfully");
                return true;
            }
            ws_->close();
        }

        // Exponential backoff
        reconnect_delay_ms_ = std::min(
            static_cast<int>(reconnect_delay_ms_ * RECONNECT_BACKOFF_FACTOR),
            RECONNECT_MAX_DELAY_MS);
    }

    return false;
}

void RelayClient::reject_all_pending() {
    // Reject pending RPC requests
    {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        for (auto& [id, pending] : pending_requests_) {
            try {
                json error_result;
                error_result["code"] = "503";
                error_result["message"] = "Connection lost";
                pending->promise.set_value(error_result);
            } catch (...) {
                // Promise may already be satisfied
            }
        }
        pending_requests_.clear();
    }

    // Reject pending dials
    {
        std::lock_guard<std::mutex> lock(dials_mutex_);
        for (auto& [tag, pending] : pending_dials_) {
            try {
                pending->promise.set_value(nullptr);
            } catch (...) {
                // Promise may already be satisfied
            }
        }
        pending_dials_.clear();
    }
}

} // namespace relay
} // namespace signalwire
