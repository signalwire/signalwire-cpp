// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT

#include "signalwire/relay/call.hpp"
#include "signalwire/relay/client.hpp"
#include "signalwire/logging.hpp"

#include <random>
#include <sstream>
#include <iomanip>

namespace signalwire {
namespace relay {

Call::Call()
    : s_(std::make_shared<SharedState>()) {}

Call::Call(const std::string& call_id, const std::string& node_id)
    : s_(std::make_shared<SharedState>()) {
    s_->call_id = call_id;
    s_->node_id = node_id;
    s_->state = CALL_STATE_CREATED;
}

Call::Call(const std::string& call_id, const std::string& node_id, RelayClient* client)
    : s_(std::make_shared<SharedState>()) {
    s_->call_id = call_id;
    s_->node_id = node_id;
    s_->state = CALL_STATE_CREATED;
    s_->client = client;
}

std::string Call::generate_uuid() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<uint32_t> dist(0, 0xFFFFFFFF);

    uint32_t a = dist(gen), b = dist(gen), c = dist(gen), d = dist(gen);
    b = (b & 0xFFFF0FFF) | 0x00004000;
    c = (c & 0x3FFFFFFF) | 0x80000000;

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

json Call::base_params() const {
    json p;
    p["node_id"] = s_->node_id;
    p["call_id"] = s_->call_id;
    return p;
}

Action Call::execute_simple(const std::string& method, const json& extra_params) {
    json params = base_params();
    for (auto& [key, val] : extra_params.items()) {
        params[key] = val;
    }

    Action action(method);
    if (s_->client) {
        try {
            json result = s_->client->execute("calling." + method, params);
            std::string code = result.value("code", "");
            if (code.size() >= 1 && code[0] == '2') {
                action.resolve("finished", result);
            } else if (code == "404" || code == "410") {
                get_logger().info("Call gone during " + method + " (code " + code + ")");
                action.resolve("finished", json::object());
            } else {
                action.resolve("error", result);
            }
        } catch (const std::exception& e) {
            get_logger().info(std::string("Call execute_simple failed: ") + e.what());
            action.resolve("error", json::object());
        }
    } else {
        action.resolve("finished", json::object());
    }
    return action;
}

Action Call::execute_action(const std::string& method, const json& extra_params) {
    std::string control_id = generate_uuid();
    json params = base_params();
    params["control_id"] = control_id;
    for (auto& [key, val] : extra_params.items()) {
        params[key] = val;
    }

    Action action(control_id, s_->client, s_->call_id, s_->node_id);
    register_action(control_id, &action);

    if (s_->client) {
        try {
            json result = s_->client->execute("calling." + method, params);
            std::string code = result.value("code", "");
            if (code == "404" || code == "410") {
                get_logger().info("Call gone during " + method + " (code " + code + ")");
                unregister_action(control_id);
                action.resolve("finished", json::object());
            } else if (code.size() >= 1 && code[0] != '2') {
                unregister_action(control_id);
                action.resolve("error", result);
            }
        } catch (const std::exception& e) {
            get_logger().info(std::string("Call execute_action failed: ") + e.what());
            unregister_action(control_id);
            action.resolve("error", json::object());
        }
    } else {
        unregister_action(control_id);
        action.resolve("finished", json::object());
    }
    return action;
}

// Simple fire-and-response methods
Action Call::answer() {
    return execute_simple("answer");
}

Action Call::hangup(const std::string& reason) {
    json p;
    if (reason != "hangup") p["reason"] = reason;
    return execute_simple("end", p);
}

Action Call::connect(const json& devices) {
    json p;
    p["devices"] = devices;
    return execute_simple("connect", p);
}

Action Call::disconnect() {
    return execute_simple("disconnect");
}

Action Call::hold() {
    return execute_simple("hold");
}

Action Call::unhold() {
    return execute_simple("unhold");
}

Action Call::transfer(const json& params) {
    return execute_simple("transfer", params);
}

Action Call::live_transcribe(const json& params) {
    return execute_simple("live_transcribe", params);
}

Action Call::live_translate(const json& params) {
    return execute_simple("live_translate", params);
}

Action Call::sip_refer(const std::string& to_uri) {
    json p;
    p["device"] = {{"type", "sip"}, {"params", {{"to", to_uri}}}};
    return execute_simple("refer", p);
}

Action Call::join_conference(const std::string& name, const json& params) {
    json p = params;
    p["name"] = name;
    return execute_simple("join_conference", p);
}

Action Call::join_room(const std::string& name) {
    json p;
    p["name"] = name;
    return execute_simple("join_room", p);
}

Action Call::send_digits(const std::string& digits) {
    json p;
    std::string control_id = generate_uuid();
    p["control_id"] = control_id;
    p["digits"] = digits;
    return execute_simple("send_digits", p);
}

Action Call::execute_swml(const json& swml) {
    json p;
    p["swml"] = swml;
    return execute_simple("transfer", p);
}

// Action-based methods (with control_id tracking)
Action Call::play(const json& media, double volume) {
    json p;
    p["play"] = media;
    if (volume != 0.0) p["volume"] = volume;
    return execute_action("play", p);
}

Action Call::record(const json& params) {
    json p;
    if (!params.empty()) p["record"] = params;
    return execute_action("record", p);
}

Action Call::record_call(const json& params) {
    return record(params);
}

Action Call::prompt(const json& play_media, const json& collect_params) {
    json p;
    p["play"] = play_media;
    p["collect"] = collect_params;
    return execute_action("play_and_collect", p);
}

Action Call::collect(const json& params) {
    return execute_action("collect", params);
}

Action Call::detect(const json& params) {
    return execute_action("detect", params);
}

Action Call::tap_audio(const json& params) {
    return execute_action("tap", params);
}

Action Call::stop_tap(const std::string& control_id) {
    json params = base_params();
    params["control_id"] = control_id;

    Action action(control_id);
    if (s_->client) {
        try {
            s_->client->execute("calling.tap.stop", params);
        } catch (const std::exception& e) {
            get_logger().info(std::string("stop_tap failed: ") + e.what());
        }
    }
    action.resolve("finished");
    return action;
}

Action Call::ai(const json& params) {
    return execute_action("ai", params);
}

Action Call::send_fax(const std::string& document_url, const std::string& header) {
    json p;
    p["document"] = document_url;
    if (!header.empty()) p["header_info"] = header;
    return execute_action("send_fax", p);
}

Action Call::receive_fax() {
    return execute_action("receive_fax");
}

// Event handling
void Call::on_event(CallEventHandler handler) {
    s_->event_handlers.push_back(std::move(handler));
}

bool Call::wait_for_ended(int timeout_ms) {
    std::unique_lock<std::mutex> lock(s_->ended_mutex);
    if (s_->state == CALL_STATE_ENDED) return true;

    if (timeout_ms > 0) {
        return s_->ended_cv.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                                     [this] { return s_->state == CALL_STATE_ENDED; });
    }
    s_->ended_cv.wait(lock, [this] { return s_->state == CALL_STATE_ENDED; });
    return true;
}

void Call::update_state(const std::string& new_state) {
    {
        std::lock_guard<std::mutex> lock(s_->ended_mutex);
        s_->state = new_state;
    }
    if (new_state == CALL_STATE_ENDED) {
        s_->ended_cv.notify_all();
        resolve_all_actions("finished");
    }
}

void Call::dispatch_event(const CallEvent& ev) {
    for (auto& h : s_->event_handlers) {
        try {
            h(ev);
        } catch (const std::exception& e) {
            get_logger().error(std::string("Call event handler error: ") + e.what());
        }
    }
}

void Call::register_action(const std::string& control_id, Action* action) {
    std::lock_guard<std::mutex> lock(s_->actions_mutex);
    s_->actions[control_id] = action;
}

void Call::unregister_action(const std::string& control_id) {
    std::lock_guard<std::mutex> lock(s_->actions_mutex);
    s_->actions.erase(control_id);
}

Action* Call::find_action(const std::string& control_id) {
    std::lock_guard<std::mutex> lock(s_->actions_mutex);
    auto it = s_->actions.find(control_id);
    return it != s_->actions.end() ? it->second : nullptr;
}

void Call::resolve_all_actions(const std::string& final_state) {
    std::lock_guard<std::mutex> lock(s_->actions_mutex);
    for (auto& [id, action] : s_->actions) {
        if (action && !action->completed()) {
            action->resolve(final_state, json::object());
        }
    }
    s_->actions.clear();
}

} // namespace relay
} // namespace signalwire
