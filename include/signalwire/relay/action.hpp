// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#pragma once

#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace signalwire {
namespace relay {

using json = nlohmann::json;

class RelayClient;

/// Represents a controllable in-progress operation (play, record, collect, etc.)
/// Uses shared internal state so the object can be copied/moved freely while
/// maintaining a single underlying condition_variable for synchronization.
class Action {
 public:
  using CompletedCallback = std::function<void(const Action&)>;

  Action();
  explicit Action(const std::string& control_id);
  Action(const std::string& control_id, RelayClient* client, const std::string& call_id,
         const std::string& node_id);

  const std::string& control_id() const { return state_->control_id; }
  const std::string& state() const;
  [[nodiscard]] bool completed() const;
  const json& result() const;
  const std::string& call_id() const { return state_->call_id; }
  const std::string& node_id() const { return state_->node_id; }

  /// Method prefix used for sub-command frames (stop/pause/resume/...).
  /// Defaults to "calling.play"; set explicitly when an Action is built
  /// for a different verb (record, collect, detect, ...). The
  /// Call::execute_action factory plumbs this through.
  const std::string& method_prefix() const { return state_->method_prefix; }
  void set_method_prefix(const std::string& prefix) { state_->method_prefix = prefix; }

  /// Set the wire-event types the Action should accept state updates
  /// from. Empty (the default) means "match any component event whose
  /// control_id matches this Action". Used by play_and_collect to
  /// listen on calling.call.collect only — a calling.call.play(finished)
  /// must NOT resolve a play_and_collect action.
  void set_event_type_filter(const std::vector<std::string>& types) {
    state_->event_type_filter = types;
  }
  const std::vector<std::string>& event_type_filter() const { return state_->event_type_filter; }
  bool event_type_matches(const std::string& event_type) const {
    if (state_->event_type_filter.empty()) { return true;
}
    for (const auto& t : state_->event_type_filter) {
      if (t == event_type) { return true;
}
    }
    return false;
  }

  /// Detect actions resolve on the first event carrying a `detect`
  /// payload, not on a state(finished) — see Python's DetectAction.
  /// When this flag is set the action's update_state path resolves
  /// only when `params.detect` is present.
  void set_resolve_on_detect(bool flag) { state_->resolve_on_detect = flag; }
  bool resolve_on_detect() const { return state_->resolve_on_detect; }

  /// Collect actions resolve when an event carries a `result` payload.
  /// A play(finished) earlier in the timeline does NOT resolve a
  /// CollectAction — see Python's CollectAction terminal-event logic.
  void set_resolve_on_result(bool flag) { state_->resolve_on_result = flag; }
  bool resolve_on_result() const { return state_->resolve_on_result; }

  /// Block until the action completes or times out.
  /// [[nodiscard]]: the bool tells you whether the action actually
  /// completed vs. timed out — dropping it (then treating the action as
  /// done) is a bug.
  [[nodiscard]] bool wait(int timeout_ms = 0);

  /// Request the server to stop this action. Routes to
  /// `<method_prefix>.stop` so an Action returned by `record()` sends
  /// `calling.record.stop` rather than `calling.play.stop`.
  void stop();

  /// Request the server to pause this action.
  /// `extra_params` may carry e.g. `{"behavior": "continuous"}` for
  /// record-side pause.
  void pause(const json& extra_params = json::object());

  /// Request the server to resume this action.
  void resume();

  /// Adjust playback volume (play only). The frame body carries the
  /// supplied amount in dB; positive boosts, negative attenuates.
  void volume(double amount);

  /// Start the inter-digit / final-digit timers on a collect. The
  /// matching Python method is StandaloneCollectAction.start_input_timers.
  void start_input_timers();

  /// Set a callback to fire when the action completes.
  void on_completed(CompletedCallback cb);

  /// Update internal state (called by Call/Client when events arrive).
  void update_state(const std::string& new_state, const json& result = json::object());

  /// Resolve the action immediately (used for call-gone scenarios).
  void resolve(const std::string& final_state = "finished", const json& result = json::object());

 private:
  void send_control_command(const std::string& operation,
                            const json& extra_params = json::object());

  struct SharedState {
    std::string control_id;
    std::string call_id;
    std::string node_id;
    std::string method_prefix = "calling.play";
    std::vector<std::string> event_type_filter;
    bool resolve_on_detect = false;
    bool resolve_on_result = false;
    RelayClient* client = nullptr;

    mutable std::mutex mutex;
    std::condition_variable cv;
    std::string current_state;
    bool is_completed = false;
    json result_data;
    CompletedCallback completed_callback;
  };

  std::shared_ptr<SharedState> state_;
};

}  // namespace relay
}  // namespace signalwire
