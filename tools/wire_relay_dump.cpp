// wire_relay_dump.cpp — the C++ port's WIRE-RELAY dump program for the
// cross-port relay differ (porting-sdk/scripts/diff_port_wire_relay.py).
//
// It captures, for each wire_relay_corpus case, the observable RELAY artifact:
//   - verb   : the {method, params} JSON-RPC frame a Call verb (or an Action
//     control-op) hands to the wire.
//   - client : the {method, params} frame a RelayClient call (execute / dial /
//     send_message) sends.
//   - event  : the decoded fields a typed event decoder extracts from a payload.
//
// It prints ONE JSON object mapping case-id -> artifact to stdout; the differ
// canonicalizes both sides (normalizing the random control_id to a sentinel)
// and byte-compares against the python oracle. Only stdout carries JSON.
//
// Frame capture: verb/client verbs send over a real WebSocket, so this program
// stands up a tiny in-process mock RELAY WS server (IXWebSocket) on a loopback
// port (pointed to via the RelayClient host + SIGNALWIRE_RELAY_SCHEME=ws),
// completes the connect handshake, records each calling.*/messaging.* frame, and
// replies with a canned success. Event decoding is pure (no wire). Mirrors the
// Go reference dump signalwire-go/cmd/wire-relay-dump.

#include <arpa/inet.h>
#include <ixwebsocket/IXConnectionState.h>
#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXWebSocket.h>
#include <ixwebsocket/IXWebSocketServer.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <thread>

#include "signalwire/logging.hpp"
#include "signalwire/relay/action.hpp"
#include "signalwire/relay/call.hpp"
#include "signalwire/relay/client.hpp"
#include "signalwire/relay/typed_events.hpp"

using json = nlohmann::json;
namespace relay = signalwire::relay;

namespace {

const char* kNode = "node-abc";
const char* kCall = "call-xyz";
const char* kCid = "ctl-123";

// A minimal in-process RELAY mock. Completes the signalwire.connect handshake,
// records every calling.*/messaging.* frame's params, and replies with a canned
// success so the client's verbs proceed.
class MockRelay {
 public:
  json last_frame(const std::string& method) {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = frames_.find(method);
    return it != frames_.end() ? it->second : json(nullptr);
  }

  // Push a server-initiated signalwire.event to every connected client.
  void push_event(const std::string& event_type, const json& params) {
    std::lock_guard<std::mutex> lock(mu_);
    json frame = {{"jsonrpc", "2.0"},
                  {"id", "evt-" + event_type},
                  {"method", "signalwire.event"},
                  {"params", {{"event_type", event_type}, {"params", params}}}};
    for (auto& ws : clients_) {
      ws->sendText(frame.dump());
    }
  }

  void on_message(ix::WebSocket& ws, const std::string& raw) {
    json msg = json::parse(raw, nullptr, false);
    if (msg.is_discarded() || !msg.is_object()) {
      return;
    }
    std::string id = msg.value("id", "");
    // A response/ack to a server push (has result, no method) — ignore.
    if (!msg.contains("method")) {
      return;
    }
    std::string method = msg.value("method", "");
    json params = msg.contains("params") ? msg["params"] : json::object();

    if (method == "signalwire.connect") {
      reply(ws, id, {{"protocol", "default"}, {"sessionid", "sess-1"}});
      return;
    }
    if (method == "signalwire.receive" || method == "signalwire.subscribe") {
      reply(ws, id, {{"code", "200"}});
      return;
    }

    // Record the frame's params (a calling.* / messaging.* verb).
    {
      std::lock_guard<std::mutex> lock(mu_);
      frames_[method] = params;
    }
    if (method == "calling.dial") {
      reply(ws, id, {{"code", "200"}, {"message", "Dialing"}});
      // Resolve dial: push calling.call.dial(answered) for the tag.
      std::string tag = params.value("tag", "");
      if (!tag.empty()) {
        std::thread([this, tag] {
          std::this_thread::sleep_for(std::chrono::milliseconds(20));
          push_event("calling.call.dial", {{"tag", tag},
                                           {"dial_state", "answered"},
                                           {"call", {{"call_id", kCall}, {"node_id", kNode}}}});
        }).detach();
      }
    } else if (method == "messaging.send") {
      reply(ws, id, {{"code", "200"}, {"message_id", "msg-1"}});
    } else {
      reply(ws, id, {{"code", "200"}});
    }
  }

  void register_client(std::shared_ptr<ix::WebSocket> ws) {
    std::lock_guard<std::mutex> lock(mu_);
    clients_.push_back(std::move(ws));
  }

 private:
  static void reply(ix::WebSocket& ws, const std::string& id, const json& result) {
    json frame = {{"jsonrpc", "2.0"}, {"id", id}, {"result", result}};
    ws.sendText(frame.dump());
  }

  std::mutex mu_;
  std::map<std::string, json> frames_;
  std::vector<std::shared_ptr<ix::WebSocket>> clients_;
};

// Pick a free loopback TCP port: bind :0, read the OS-assigned port, release
// it, and hand it to the mock server (IXWebSocketServer's getPort() does not
// resolve an ephemeral :0 bind, so we must pick the concrete port ourselves —
// the porting-sdk free-port contract for mock-binding harnesses).
int pick_free_port() {
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) return 0;
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = 0;
  if (bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    close(sock);
    return 0;
  }
  socklen_t len = sizeof(addr);
  if (getsockname(sock, reinterpret_cast<sockaddr*>(&addr), &len) != 0) {
    close(sock);
    return 0;
  }
  int port = ntohs(addr.sin_port);
  close(sock);
  return port;
}

json frame(const std::string& method, const json& params) {
  return json{{"method", method}, {"params", params}};
}

void settle() { std::this_thread::sleep_for(std::chrono::milliseconds(80)); }

// ---- pure event decoders (no wire) ----
void decode_events(json& out) {
  {
    auto q = relay::events::QueueEvent::from_payload({{"event_type", "calling.call.queue"},
                                                      {"params",
                                                       {{"call_id", kCall},
                                                        {"control_id", kCid},
                                                        {"status", "waiting"},
                                                        {"id", "q-42"},
                                                        {"name", "support"},
                                                        {"position", 3},
                                                        {"size", 10}}}});
    out["relay_evt_queue"] = {{"control_id", q.control_id}, {"status", q.status},
                              {"queue_id", q.queue_id},     {"queue_name", q.queue_name},
                              {"position", q.position},     {"size", q.size}};
  }
  {
    auto rec = relay::events::RecordEvent::from_payload(
        {{"event_type", "calling.call.record"},
         {"params",
          {{"call_id", kCall},
           {"control_id", kCid},
           {"state", "finished"},
           {"record", {{"url", "https://x/rec.mp3"}, {"duration", 12.5}, {"size", 4096}}}}}});
    out["relay_evt_record"] = {{"control_id", rec.control_id},
                               {"state", rec.state},
                               {"url", rec.url},
                               {"duration", rec.duration},
                               {"size", rec.size}};
  }
  {
    auto obj = relay::events::parse_event({{"event_type", "calling.call.state"},
                                           {"params",
                                            {{"call_id", kCall},
                                             {"call_state", "answered"},
                                             {"direction", "inbound"},
                                             {"end_reason", ""}}}});
    // parse_event dispatches to CallStateEvent for calling.call.state.
    out["relay_evt_state_dispatch"] = {{"_class", "CallStateEvent"},
                                       {"call_id", obj.call_id},
                                       {"call_state", obj.params.value("call_state", "")},
                                       {"direction", obj.params.value("direction", "")}};
  }
  {
    auto col = relay::events::CollectEvent::from_payload(
        {{"event_type", "calling.call.collect"},
         {"params",
          {{"call_id", kCall},
           {"control_id", kCid},
           {"state", "finished"},
           {"result", {{"type", "digit"}, {"params", {{"digits", "1234"}}}}},
           {"final", true}}}});
    json final_val = col.final.has_value() ? json(*col.final) : json(nullptr);
    out["relay_evt_collect"] = {{"control_id", col.control_id},
                                {"state", col.state},
                                {"result", col.result},
                                {"final", final_val}};
  }
}

}  // namespace

int main() {
  if (!std::getenv("RELAY_DUMP_DEBUG")) {
    signalwire::Logger::instance().suppress();
  }
  ix::initNetSystem();

  auto mock = std::make_shared<MockRelay>();

  // Bind the mock WS server on a self-picked free loopback port.
  int port = pick_free_port();
  if (port == 0) {
    std::cerr << "wire-relay-dump: could not pick a free port\n";
    return 1;
  }
  ix::WebSocketServer server(port, "127.0.0.1");
  server.setOnClientMessageCallback(
      [mock, &server](const std::shared_ptr<ix::ConnectionState>& /*state*/, ix::WebSocket& ws,
                      const ix::WebSocketMessagePtr& msg) {
        if (msg->type == ix::WebSocketMessageType::Open) {
          if (std::getenv("RELAY_DUMP_DEBUG")) std::cerr << "[mock] client opened\n";
          // Track this client so the mock can push server-initiated events.
          for (auto& client : server.getClients()) {
            if (client.get() == &ws) {
              mock->register_client(client);
              break;
            }
          }
        } else if (msg->type == ix::WebSocketMessageType::Message) {
          if (std::getenv("RELAY_DUMP_DEBUG")) {
            std::cerr << "[mock] recv: " << msg->str.substr(0, 120) << "\n";
          }
          mock->on_message(ws, msg->str);
        } else if (msg->type == ix::WebSocketMessageType::Error) {
          if (std::getenv("RELAY_DUMP_DEBUG"))
            std::cerr << "[mock] error: " << msg->errorInfo.reason << "\n";
        }
      });

  server.disablePerMessageDeflate();
  auto res = server.listen();
  if (!res.first) {
    std::cerr << "wire-relay-dump: listen failed: " << res.second << "\n";
    return 1;
  }
  server.start();
  // Give the accept loop a moment to come up before the client dials in.
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  if (std::getenv("RELAY_DUMP_DEBUG")) std::cerr << "[mock] listening on port " << port << "\n";

  // Point the client at the mock over plain ws://.
  ::setenv("SIGNALWIRE_RELAY_SCHEME", "ws", 1);
  std::string host = std::string("127.0.0.1:") + std::to_string(port);

  json out = json::object();
  decode_events(out);

  relay::RelayClient client("proj-1", "tok-1", host, {"default"});
  if (!client.connect()) {
    std::cerr << "wire-relay-dump: client connect failed\n";
    server.stop();
    return 1;
  }

  // Build a Call bound to the client to drive the verb cases directly (the
  // corpus verbs are pure frame builders — an inbound call is not required to
  // observe the frame they send).
  relay::Call call(kCall, kNode, &client);

  // relay_play
  call.play(json::array({{{"type", "audio"}, {"params", {{"url", "https://x/a.mp3"}}}}}), 5.0,
            kCid);
  settle();
  out["relay_play"] = frame("calling.play", mock->last_frame("calling.play"));

  // relay_play_tts — play_tts(text, language, gender, voice, volume); the corpus
  // sets voice, so pass it as the 4th positional (gender stays empty).
  call.play_tts("Hello world", "", "", "en-US-Neural");
  settle();
  out["relay_play_tts"] = frame("calling.play", mock->last_frame("calling.play"));

  // relay_record
  call.record(json{{"audio", {{"format", "mp3"}, {"beep", true}}}}, kCid);
  settle();
  out["relay_record"] = frame("calling.record", mock->last_frame("calling.record"));

  // relay_connect
  call.connect(
      json::array(
          {json::array({{{"type", "phone"}, {"params", {{"to_number", "+15551112222"}}}}})}),
      json{{"ringback", json::array({{{"type", "ringtone"}, {"params", {{"name", "us"}}}}})},
           {"tag", "leg-1"},
           {"max_duration", 3600}});
  settle();
  out["relay_connect"] = frame("calling.connect", mock->last_frame("calling.connect"));

  // relay_collect
  call.collect(json{{"digits", {{"max", 4}, {"terminators", "#"}}},
                    {"speech", {{"language", "en-US"}}},
                    {"initial_timeout", 5.0},
                    {"partial_results", true}},
               kCid);
  settle();
  out["relay_collect"] = frame("calling.collect", mock->last_frame("calling.collect"));

  // relay_prompt (play_and_collect) — prompt_tts(text, collect, language, gender,
  // voice, volume); pass voice as the 5th positional (gender empty).
  call.prompt_tts("Enter your PIN", json{{"digits", {{"max", 4}}}}, "", "", "en-US-Neural");
  settle();
  out["relay_prompt"] =
      frame("calling.play_and_collect", mock->last_frame("calling.play_and_collect"));

  // relay_detect — detect(params, control_id) merges params into the frame; the
  // detect descriptor is nested under "detect" with a sibling top-level timeout.
  call.detect(json{{"detect", {{"type", "machine"}, {"params", {{"initial_timeout", 4.0}}}}},
                   {"timeout", 30.0}},
              kCid);
  settle();
  out["relay_detect"] = frame("calling.detect", mock->last_frame("calling.detect"));

  // relay_detect_amd
  call.detect_answering_machine(json{{"initial_timeout", 4.0}, {"machine_words_threshold", 6}},
                                30.0);
  settle();
  out["relay_detect_amd"] = frame("calling.detect", mock->last_frame("calling.detect"));

  // relay_tap
  call.tap(json{{"tap", {{"type", "audio"}, {"params", {{"direction", "both"}}}}},
                {"device", {{"type", "ws"}, {"params", {{"uri", "wss://x/tap"}}}}}},
           kCid);
  settle();
  out["relay_tap"] = frame("calling.tap", mock->last_frame("calling.tap"));

  // relay_send_fax
  call.send_fax("https://x/doc.pdf", "Hdr", "+15550001111", kCid);
  settle();
  out["relay_send_fax"] = frame("calling.send_fax", mock->last_frame("calling.send_fax"));

  // ---- control-ops (Action methods) ----
  {
    auto pa = call.play(
        json::array({{{"type", "audio"}, {"params", {{"url", "https://x/a.mp3"}}}}}), 0.0, kCid);
    settle();
    pa.stop();
    settle();
    out["relay_play_stop"] = frame("calling.play.stop", mock->last_frame("calling.play.stop"));
  }
  {
    auto pa = call.play(
        json::array({{{"type", "audio"}, {"params", {{"url", "https://x/a.mp3"}}}}}), 0.0, kCid);
    settle();
    pa.pause("silence");
    settle();
    out["relay_play_pause"] = frame("calling.play.pause", mock->last_frame("calling.play.pause"));
  }
  {
    auto ra = call.record(json{{"audio", {{"format", "mp3"}}}}, kCid);
    settle();
    ra.resume();
    settle();
    out["relay_record_resume"] =
        frame("calling.record.resume", mock->last_frame("calling.record.resume"));
  }
  {
    auto pa = call.play(
        json::array({{{"type", "audio"}, {"params", {{"url", "https://x/a.mp3"}}}}}), 0.0, kCid);
    settle();
    pa.volume(3.5);
    settle();
    out["relay_play_volume"] =
        frame("calling.play.volume", mock->last_frame("calling.play.volume"));
  }

  // ---- RelayClient-level frames ----
  // relay_client_execute
  client.execute("calling.answer", json{{"node_id", kNode}, {"call_id", kCall}});
  settle();
  out["relay_client_execute"] = frame("calling.answer", mock->last_frame("calling.answer"));

  // relay_send_message
  client.send_message("+15553334444", "+15551112222", "hi", {}, {"t1"});
  settle();
  out["relay_send_message"] = frame("messaging.send", mock->last_frame("messaging.send"));

  // relay_dial
  client.dial(json::array({json::array(
                  {{{"type", "phone"}, {"params", {{"to_number", "+15551112222"}}}}})}),
              "dial-1", 3000, 600);
  settle();
  out["relay_dial"] = frame("calling.dial", mock->last_frame("calling.dial"));

  client.disconnect();
  server.stop();
  ix::uninitNetSystem();

  std::cout << out.dump() << "\n";
  return 0;
}
