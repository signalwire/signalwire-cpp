// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT

#include "signalwire/relay/action.hpp"
#include "signalwire/relay/client.hpp"
#include "signalwire/logging.hpp"

namespace signalwire {
namespace relay {

Action::Action()
    : state_(std::make_shared<SharedState>()) {}

Action::Action(const std::string& control_id)
    : state_(std::make_shared<SharedState>()) {
    state_->control_id = control_id;
}

Action::Action(const std::string& control_id, RelayClient* client,
               const std::string& call_id, const std::string& node_id)
    : state_(std::make_shared<SharedState>()) {
    state_->control_id = control_id;
    state_->call_id = call_id;
    state_->node_id = node_id;
    state_->client = client;
}

const std::string& Action::state() const {
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->current_state;
}

bool Action::completed() const {
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->is_completed;
}

const json& Action::result() const {
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->result_data;
}

bool Action::wait(int timeout_ms) {
    std::unique_lock<std::mutex> lock(state_->mutex);
    if (state_->is_completed) return true;

    if (timeout_ms > 0) {
        return state_->cv.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                                   [this] { return state_->is_completed; });
    }
    state_->cv.wait(lock, [this] { return state_->is_completed; });
    return true;
}

void Action::stop() {
    send_control_command("stop");
}

void Action::pause() {
    send_control_command("pause");
}

void Action::resume() {
    send_control_command("resume");
}

void Action::on_completed(CompletedCallback cb) {
    std::lock_guard<std::mutex> lock(state_->mutex);
    state_->completed_callback = std::move(cb);
    if (state_->is_completed && state_->completed_callback) {
        try {
            state_->completed_callback(*this);
        } catch (const std::exception& e) {
            get_logger().error(std::string("Action on_completed callback error: ") + e.what());
        } catch (...) {
            get_logger().error("Action on_completed callback threw unknown exception");
        }
    }
}

void Action::update_state(const std::string& new_state, const json& result) {
    CompletedCallback cb;
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        state_->current_state = new_state;
        state_->result_data = result;
        if (new_state == "finished" || new_state == "error" ||
            new_state == "no_input" || new_state == "no_match") {
            state_->is_completed = true;
            state_->cv.notify_all();
            cb = state_->completed_callback;
        }
    }
    if (cb) {
        try {
            cb(*this);
        } catch (const std::exception& e) {
            get_logger().error(std::string("Action on_completed callback error: ") + e.what());
        } catch (...) {
            get_logger().error("Action on_completed callback threw unknown exception");
        }
    }
}

void Action::resolve(const std::string& final_state, const json& result) {
    CompletedCallback cb;
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        state_->current_state = final_state;
        state_->result_data = result;
        state_->is_completed = true;
        state_->cv.notify_all();
        cb = state_->completed_callback;
    }
    if (cb) {
        try {
            cb(*this);
        } catch (const std::exception& e) {
            get_logger().error(std::string("Action resolve callback error: ") + e.what());
        } catch (...) {
            get_logger().error("Action resolve callback threw unknown exception");
        }
    }
}

void Action::send_control_command(const std::string& operation) {
    if (!state_->client) {
        get_logger().warn("Action::send_control_command called without client");
        return;
    }
    if (state_->call_id.empty() || state_->node_id.empty() || state_->control_id.empty()) {
        get_logger().warn("Action::send_control_command missing call_id/node_id/control_id");
        return;
    }

    json params;
    params["node_id"] = state_->node_id;
    params["call_id"] = state_->call_id;
    params["control_id"] = state_->control_id;

    try {
        std::string method = "calling.play." + operation;
        state_->client->execute(method, params);
    } catch (const std::exception& e) {
        get_logger().info(std::string("Action control command failed: ") + e.what());
    }
}

} // namespace relay
} // namespace signalwire
