// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT

#include "signalwire/relay/call.hpp"

#include <iomanip>
#include <random>
#include <sstream>

#include "signalwire/logging.hpp"
#include "signalwire/relay/client.hpp"

namespace signalwire {
namespace relay {

Call::Call() : s_(std::make_shared<SharedState>()) {}

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
      if (!code.empty() && code[0] == '2') {
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
  // Allow callers to pre-supply a control_id via `extra_params` —
  // the Python tests pin specific ids ("play-ctl-fin") so journal
  // assertions can match. Otherwise we synthesise a fresh UUID.
  std::string control_id;
  if (extra_params.is_object() && extra_params.contains("control_id")) {
    control_id = extra_params.value("control_id", "");
  }
  if (control_id.empty()) {
    control_id = generate_uuid();
  }

  json params = base_params();
  params["control_id"] = control_id;
  for (auto& [key, val] : extra_params.items()) {
    if (key == "control_id") {
      continue;  // already set
    }
    params[key] = val;
  }

  Action action(control_id, s_->client, s_->call_id, s_->node_id);
  // Tag the Action with the verb so its sub-command frames
  // (stop/pause/resume/volume) route under the right method prefix.
  action.set_method_prefix("calling." + method);
  register_action(control_id, &action);

  if (s_->client) {
    try {
      json result = s_->client->execute("calling." + method, params);
      std::string code = result.value("code", "");
      if (code == "404" || code == "410") {
        get_logger().info("Call gone during " + method + " (code " + code + ")");
        unregister_action(control_id);
        action.resolve("finished", json::object());
      } else if (!code.empty() && code[0] != '2') {
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
Action Call::answer() { return execute_simple("answer"); }

Action Call::hangup(const std::string& reason) {
  json p;
  if (reason != "hangup") {
    p["reason"] = reason;
  }
  return execute_simple("end", p);
}

Action Call::connect(const json& devices) {
  json p;
  p["devices"] = devices;
  return execute_simple("connect", p);
}

Action Call::disconnect() { return execute_simple("disconnect"); }

Action Call::hold() { return execute_simple("hold"); }

Action Call::unhold() { return execute_simple("unhold"); }

Action Call::transfer(const json& params) { return execute_simple("transfer", params); }

Action Call::live_transcribe(const json& params) {
  return execute_simple("live_transcribe", params);
}

Action Call::live_translate(const json& params) { return execute_simple("live_translate", params); }

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

// Action-based methods (with control_id tracking).
// Each accepts an optional explicit control_id; when blank a fresh UUID
// is generated by execute_action.
Action Call::play(const json& media, double volume, const std::string& control_id) {
  json p;
  p["play"] = media;
  if (volume != 0.0) {
    p["volume"] = volume;
  }
  if (!control_id.empty()) {
    p["control_id"] = control_id;
  }
  return execute_action("play", p);
}

// Typed play convenience wrappers. Each assembles the RELAY play-media array
// — [{"type": <kind>, "params": {...}}] — and delegates to play(). Optional
// fields are emitted only when the caller supplies a non-sentinel value, so
// the on-wire params object stays minimal (matching Python's only-provided
// keys). See RELAY_IMPLEMENTATION_GUIDE.md for the media shapes.
Action Call::play_tts(const std::string& text, const std::string& language,
                      const std::string& gender, const std::string& voice, double volume) {
  json tts;
  tts["text"] = text;
  if (!language.empty()) {
    tts["language"] = language;
  }
  if (!gender.empty()) {
    tts["gender"] = gender;
  }
  if (!voice.empty()) {
    tts["voice"] = voice;
  }
  json media = json::array({{{"type", "tts"}, {"params", tts}}});
  return play(media, volume);
}

// Typed-enum overload: normalize Gender -> its wire string and delegate to the
// std::string play_tts above, so the enum and the bare string emit the
// IDENTICAL TTS frame. See include/signalwire/relay/tts_gender.hpp.
Action Call::play_tts(const std::string& text, const std::string& language, Gender gender,
                      const std::string& voice, double volume) {
  return play_tts(text, language, tts_gender_value(gender), voice, volume);
}

Action Call::play_audio(const std::string& url, double volume) {
  json media = json::array({{{"type", "audio"}, {"params", {{"url", url}}}}});
  return play(media, volume);
}

Action Call::play_silence(double duration) {
  json media = json::array({{{"type", "silence"}, {"params", {{"duration", duration}}}}});
  return play(media);
}

Action Call::play_ringtone(const std::string& name, double duration, double volume) {
  json rt;
  rt["name"] = name;
  if (duration >= 0.0) {
    rt["duration"] = duration;
  }
  json media = json::array({{{"type", "ringtone"}, {"params", rt}}});
  return play(media, volume);
}

Action Call::record(const json& params, const std::string& control_id) {
  json p;
  if (!params.empty()) {
    p["record"] = params;
  }
  if (!control_id.empty()) {
    p["control_id"] = control_id;
  }
  return execute_action("record", p);
}

Action Call::record_call(const json& params) { return record(params); }

Action Call::prompt(const json& play_media, const json& collect_params,
                    const std::string& control_id) {
  return play_and_collect(play_media, collect_params, control_id);
}

Action Call::play_and_collect(const json& play_media, const json& collect_params,
                              const std::string& control_id) {
  json p;
  p["play"] = play_media;
  p["collect"] = collect_params;
  if (!control_id.empty()) {
    p["control_id"] = control_id;
  }
  Action a = execute_action("play_and_collect", p);
  // Per RELAY_IMPLEMENTATION_GUIDE: a play_and_collect resolves on the
  // calling.call.collect event ONLY. A calling.call.play(finished)
  // earlier in the timeline must NOT resolve the action — the audio is
  // playing while we're still collecting digits.
  a.set_event_type_filter({"calling.call.collect"});
  // Collect terminal signal is the `result` payload (digits/speech),
  // not a state(finished). See Python CollectAction.
  a.set_resolve_on_result(true);
  return a;
}

// Typed prompt convenience wrappers. Build the RELAY play-media array and
// delegate to play_and_collect() with the caller's collect descriptor. The
// returned Action inherits play_and_collect's collect-only resolution
// semantics (a play(finished) does not resolve it).
// Build one play_and_collect frame from the supplied media + collect with an
// optional top-level volume, issue it, and apply the collect-only resolution
// semantics. Shared by prompt_tts/prompt_audio so the wire frame carries
// volume in a SINGLE journaled calling.play_and_collect (not a second one).
Action Call::prompt_with_media(const json& media, const json& collect, double volume) {
  json p;
  p["play"] = media;
  p["collect"] = collect;
  if (volume != 0.0) {
    p["volume"] = volume;
  }
  Action a = execute_action("play_and_collect", p);
  // Same gotcha as play_and_collect(): resolve on the collect event only,
  // and on the result payload (not a play(finished)).
  a.set_event_type_filter({"calling.call.collect"});
  a.set_resolve_on_result(true);
  return a;
}

Action Call::prompt_tts(const std::string& text, const json& collect, const std::string& language,
                        const std::string& gender, const std::string& voice, double volume) {
  json tts;
  tts["text"] = text;
  if (!language.empty()) {
    tts["language"] = language;
  }
  if (!gender.empty()) {
    tts["gender"] = gender;
  }
  if (!voice.empty()) {
    tts["voice"] = voice;
  }
  json media = json::array({{{"type", "tts"}, {"params", tts}}});
  return prompt_with_media(media, collect, volume);
}

// Typed-enum overload — same normalization as play_tts(Gender): delegate to
// the std::string prompt_tts so both emit the IDENTICAL TTS frame.
Action Call::prompt_tts(const std::string& text, const json& collect, const std::string& language,
                        Gender gender, const std::string& voice, double volume) {
  return prompt_tts(text, collect, language, tts_gender_value(gender), voice, volume);
}

Action Call::prompt_audio(const std::string& url, const json& collect, double volume) {
  json media = json::array({{{"type", "audio"}, {"params", {{"url", url}}}}});
  return prompt_with_media(media, collect, volume);
}

Action Call::collect(const json& params, const std::string& control_id) {
  json p = params.is_object() ? params : json::object();
  if (!control_id.empty()) {
    p["control_id"] = control_id;
  }
  Action a = execute_action("collect", p);
  a.set_event_type_filter({"calling.call.collect"});
  a.set_resolve_on_result(true);
  return a;
}

Action Call::detect(const json& params, const std::string& control_id) {
  json p = params.is_object() ? params : json::object();
  if (!control_id.empty()) {
    p["control_id"] = control_id;
  }
  Action a = execute_action("detect", p);
  // Detect resolves on the first event carrying a `detect` payload —
  // a state(finished) without a detect payload must NOT resolve the
  // action. See Python DetectAction.
  a.set_resolve_on_detect(true);
  return a;
}

// Typed detect convenience wrappers. Each assembles the RELAY detect
// descriptor {"type": <kind>, "params": {...only-provided...}} and an optional
// top-level timeout, then delegates to detect(). The returned Action inherits
// detect()'s "resolve on first detect payload" semantics.
Action Call::detect_digit(const std::string& digits, double timeout) {
  json params = json::object();
  if (!digits.empty()) {
    params["digits"] = digits;
  }
  json p;
  p["detect"] = {{"type", "digit"}, {"params", params}};
  if (timeout >= 0.0) {
    p["timeout"] = timeout;
  }
  return detect(p);
}

Action Call::detect_answering_machine(const json& amd_params, double timeout) {
  // amd_params carries any of initial_timeout, end_silence_timeout,
  // machine_voice_threshold, machine_words_threshold, detect_interruptions,
  // detect_message_end — only the keys the caller supplied ride the wire.
  json params = amd_params.is_object() ? amd_params : json::object();
  json p;
  p["detect"] = {{"type", "machine"}, {"params", params}};
  if (timeout >= 0.0) {
    p["timeout"] = timeout;
  }
  return detect(p);
}

Action Call::detect_fax(const std::string& tone, double timeout) {
  json params = json::object();
  if (!tone.empty()) {
    params["tone"] = tone;
  }
  json p;
  p["detect"] = {{"type", "fax"}, {"params", params}};
  if (timeout >= 0.0) {
    p["timeout"] = timeout;
  }
  return detect(p);
}

Action Call::tap_audio(const json& params, const std::string& control_id) {
  return tap(params, control_id);
}

Action Call::tap(const json& params, const std::string& control_id) {
  json p = params.is_object() ? params : json::object();
  if (!control_id.empty()) {
    p["control_id"] = control_id;
  }
  return execute_action("tap", p);
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

Action Call::ai(const json& params, const std::string& control_id) {
  json p = params.is_object() ? params : json::object();
  if (!control_id.empty()) {
    p["control_id"] = control_id;
  }
  return execute_action("ai", p);
}

Action Call::pay(const json& params, const std::string& control_id) {
  json p = params.is_object() ? params : json::object();
  if (!control_id.empty()) {
    p["control_id"] = control_id;
  }
  return execute_action("pay", p);
}

Action Call::send_fax(const std::string& document_url, const std::string& header,
                      const std::string& identity, const std::string& control_id) {
  json p;
  p["document"] = document_url;
  if (!header.empty()) {
    p["header_info"] = header;
  }
  if (!identity.empty()) {
    p["identity"] = identity;
  }
  if (!control_id.empty()) {
    p["control_id"] = control_id;
  }
  return execute_action("send_fax", p);
}

Action Call::receive_fax(const std::string& control_id) {
  json p = json::object();
  if (!control_id.empty()) {
    p["control_id"] = control_id;
  }
  return execute_action("receive_fax", p);
}

Action Call::stream(const json& params, const std::string& control_id) {
  json p = params.is_object() ? params : json::object();
  if (!control_id.empty()) {
    p["control_id"] = control_id;
  }
  return execute_action("stream", p);
}

Action Call::transcribe(const json& params, const std::string& control_id) {
  json p = params.is_object() ? params : json::object();
  if (!control_id.empty()) {
    p["control_id"] = control_id;
  }
  return execute_action("transcribe", p);
}

// Event handling
void Call::on_event(CallEventHandler handler) { s_->event_handlers.push_back(std::move(handler)); }

bool Call::wait_for_ended(int timeout_ms) {
  std::unique_lock<std::mutex> lock(s_->ended_mutex);
  if (s_->state == CALL_STATE_ENDED) {
    return true;
  }

  if (timeout_ms > 0) {
    return s_->ended_cv.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                                 [this] { return s_->state == CALL_STATE_ENDED; });
  }
  s_->ended_cv.wait(lock, [this] { return s_->state == CALL_STATE_ENDED; });
  return true;
}

namespace {
// Lifecycle rank for the call state machine
// (created < ringing < answered < ending < ended). Unknown states rank -1
// so they never satisfy a wait. Mirrors Python's _wait_for_state ordering.
int call_state_rank(const std::string& s) {
  if (s == CALL_STATE_CREATED) {
    return 0;
  }
  if (s == CALL_STATE_RINGING) {
    return 1;
  }
  if (s == CALL_STATE_ANSWERED) {
    return 2;
  }
  if (s == CALL_STATE_ENDING) {
    return 3;
  }
  if (s == CALL_STATE_ENDED) {
    return 4;
  }
  return -1;
}
}  // namespace

bool Call::wait_for_state(const std::string& target, int timeout_ms) {
  const int target_rank = call_state_rank(target);
  std::unique_lock<std::mutex> lock(s_->ended_mutex);
  // Already at or past the target -> return immediately (matches Python's
  // _wait_for_state short-circuit when rank(state) >= rank(target)).
  auto reached = [this, target_rank] { return call_state_rank(s_->state) >= target_rank; };
  if (reached()) {
    return true;
  }
  if (timeout_ms > 0) {
    return s_->state_cv.wait_for(lock, std::chrono::milliseconds(timeout_ms), reached);
  }
  s_->state_cv.wait(lock, reached);
  return true;
}

bool Call::wait_for_answered(int timeout_ms) {
  return wait_for_state(CALL_STATE_ANSWERED, timeout_ms);
}

bool Call::wait_for_ringing(int timeout_ms) {
  return wait_for_state(CALL_STATE_RINGING, timeout_ms);
}

bool Call::wait_for_ending(int timeout_ms) { return wait_for_state(CALL_STATE_ENDING, timeout_ms); }

void Call::update_state(const std::string& new_state) {
  {
    std::lock_guard<std::mutex> lock(s_->ended_mutex);
    s_->state = new_state;
  }
  // Wake every state-waiter on each transition so wait_for_answered/
  // ringing/ending can observe intermediate states, not just "ended".
  s_->state_cv.notify_all();
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

}  // namespace relay
}  // namespace signalwire
