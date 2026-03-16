// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT

#include "signalwire/relay/message.hpp"
#include "signalwire/logging.hpp"

namespace signalwire {
namespace relay {

Message::Message()
    : sync_(std::make_shared<SyncState>()) {}

Message Message::from_params(const json& params) {
    Message msg;
    msg.message_id = params.value("message_id", "");
    msg.state = params.value("message_state", params.value("state", ""));
    msg.from = params.value("from_number", "");
    msg.to = params.value("to_number", "");
    msg.body = params.value("body", "");
    msg.direction = params.value("direction", "");
    msg.region = params.value("region", "");
    if (params.contains("media") && params["media"].is_array()) {
        for (const auto& m : params["media"]) {
            msg.media.push_back(m.get<std::string>());
        }
    }
    if (params.contains("tags") && params["tags"].is_array()) {
        for (const auto& t : params["tags"]) {
            msg.tags.push_back(t.get<std::string>());
        }
    }
    return msg;
}

void Message::update_state(const std::string& new_state) {
    state = new_state;
    if (is_terminal()) {
        CompletedCallback cb;
        {
            std::lock_guard<std::mutex> lock(sync_->mutex);
            sync_->completed = true;
            sync_->cv.notify_all();
            cb = sync_->callback;
        }
        if (cb) {
            try {
                cb(*this);
            } catch (const std::exception& e) {
                get_logger().error(std::string("Message callback error: ") + e.what());
            } catch (...) {
                get_logger().error("Message callback threw unknown exception");
            }
        }
    }
}

bool Message::wait(int timeout_ms) {
    std::unique_lock<std::mutex> lock(sync_->mutex);
    if (sync_->completed) return true;
    if (timeout_ms > 0) {
        return sync_->cv.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                                  [this] { return sync_->completed; });
    }
    sync_->cv.wait(lock, [this] { return sync_->completed; });
    return true;
}

void Message::on_completed(CompletedCallback cb) {
    std::lock_guard<std::mutex> lock(sync_->mutex);
    sync_->callback = std::move(cb);
    if (sync_->completed && sync_->callback) {
        try {
            sync_->callback(*this);
        } catch (const std::exception& e) {
            get_logger().error(std::string("Message callback error: ") + e.what());
        } catch (...) {
            get_logger().error("Message callback threw unknown exception");
        }
    }
}

} // namespace relay
} // namespace signalwire
