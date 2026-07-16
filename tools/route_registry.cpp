// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
//
// SPEC-PARITY route registry + REST-test PLAN producer for the generated REST
// client (Set B producer for SPEC-PARITY; plan producer for
// scripts/generate_rest_tests.py, item E).
//
// C++ has no runtime reflection, so we cannot walk the client's methods the
// way the Go/Python registries do. Instead we derive the route set from REAL
// dispatch: stand up a loopback httplib server that records every (method,
// path) it receives and answers 200 {}; point a RestClient (composing the
// GENERATED ResourceTree) at it; invoke every public namespace method once via
// the CALL() macro; then read back the routes the SDK actually sent. The route
// strings are produced by the generated resource classes' own path-building
// code — never hand-authored here — so they cannot silently drift from what the
// client really does.
//
// The CALL(via, expr) macro is the reflection-free substitute for method
// introspection: it (1) stringifies the exact C++ call `expr` (the literal
// re-emitted verbatim into the generated wire tests), (2) executes it against
// the capture server, and (3) correlates the newly-captured (method, path)
// with `via` + the source text. The emitted plan is therefore
//   {"routes":[{method, path_template, via, call}], "skipped":[...], "errors":[...]}
// SPEC-PARITY (diff_spec_implementation.py) reads only {method, path_template};
// generate_rest_tests.py reads {via, call, method, path_template} and joins
// against the spec operationId by (method, normalized-path).
//
// Path parameters are passed as the SENTINEL (one path segment, no '/'), which
// we normalise back to {id}; the porting-sdk spec matcher turns {id} -> X and
// matches against the canonical patterns.
//
// Completeness is enforced by the consumer, not by trust: a method we FORGET to
// invoke shows up as a phantom A-B not-implemented gap in
// diff_spec_implementation.py (its canonical route is never hit), and a method
// that dispatches a non-spec route shows up as a B-A divergence. Either fails
// the SPEC-PARITY gate.
//
// Run from the build dir: ./route_registry

#include <algorithm>
#include <chrono>
#include <iostream>
#include <map>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "httplib.h"
#include "signalwire/rest/http_client.hpp"
#include "signalwire/rest/rest_client.hpp"

using namespace signalwire::rest;
using json = nlohmann::json;

namespace {

// One path segment standing in for any path parameter (resource id, sid, e164,
// …). Normalised to {id} in the emitted template.
const std::string SENTINEL = "__ID__";

// A loopback HTTP server that records (method, path) for every request and
// answers 200 {} so the SDK's verb methods complete without throwing.
class CaptureServer {
 public:
  CaptureServer() {
    auto handler = [this](const std::string& method) {
      return [this, method](const httplib::Request& req, httplib::Response& res) {
        {
          std::lock_guard<std::mutex> lock(mutex_);
          captured_.emplace_back(method, req.path);
        }
        res.status = 200;
        res.set_content("{}", "application/json");
      };
    };
    server_.Get(".*", handler("GET"));
    server_.Post(".*", handler("POST"));
    server_.Put(".*", handler("PUT"));
    server_.Patch(".*", handler("PATCH"));
    server_.Delete(".*", handler("DELETE"));

    port_ = server_.bind_to_any_port("127.0.0.1");
    thread_ = std::thread([this]() { server_.listen_after_bind(); });
    for (int i = 0; i < 100 && !server_.is_running(); ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  }
  ~CaptureServer() {
    server_.stop();
    if (thread_.joinable()) thread_.join();
  }

  std::string base_url() const { return "http://127.0.0.1:" + std::to_string(port_); }

  // Number of requests captured so far (for CALL() correlation).
  size_t size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return captured_.size();
  }
  std::pair<std::string, std::string> at(size_t i) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return captured_.at(i);
  }

 private:
  httplib::Server server_;
  std::thread thread_;
  int port_ = 0;
  mutable std::mutex mutex_;
  std::vector<std::pair<std::string, std::string>> captured_;
};

// Replace any SENTINEL path segment with {id} and drop any query string so Set
// B templates line up with the canonical spec patterns.
std::string templatize(const std::string& path) {
  std::string p = path.substr(0, path.find('?'));
  std::string out;
  size_t start = 0;
  while (start <= p.size()) {
    size_t slash = p.find('/', start);
    std::string seg =
        (slash == std::string::npos) ? p.substr(start) : p.substr(start, slash - start);
    out += (seg == SENTINEL) ? "{id}" : seg;
    if (slash == std::string::npos) break;
    out += "/";
    start = slash + 1;
  }
  return out;
}

// One planned route: the via accessor chain, the exact C++ call source
// (re-emitted into the generated tests verbatim), and the (method, template)
// the call dispatched. Populated by CALL().
struct PlanEntry {
  std::string via;
  std::string call;
  std::string method;
  std::string path_template;
};

std::vector<PlanEntry> g_plan;
CaptureServer* g_srv = nullptr;

// Execute `expr`, correlate every (method, path) it dispatched with `via` and
// the source text of `expr`. A single accessor call almost always dispatches
// exactly one request; if it dispatches more than one (none do today) each is
// recorded as a separate plan entry sharing the via/call.
#define CALL(via_str, expr)                                                         \
  do {                                                                              \
    size_t before = g_srv->size();                                                  \
    (void)(expr);                                                                   \
    size_t after = g_srv->size();                                                   \
    for (size_t i = before; i < after; ++i) {                                       \
      auto mp = g_srv->at(i);                                                       \
      g_plan.push_back(PlanEntry{via_str, #expr, mp.first, templatize(mp.second)}); \
    }                                                                               \
  } while (0)

// Invoke every public REST method once through the GENERATED client surface so
// the capture server records its route and the plan captures via + call. This
// is the ONE place the methods are enumerated; it is cross-checked against the
// canonical spec by the SPEC-PARITY diff. Keep it exhaustive.
//
// Returns {key, reason} for methods that DISPATCH NOTHING by design (currently
// only fabric.cxml_applications.create, which throws and issues no request).
std::vector<std::pair<std::string, std::string>> invoke_all(RestClient& c) {
  const std::string id = SENTINEL;
  const json body = json::object();
  const std::map<std::string, std::string> q;
  std::vector<std::pair<std::string, std::string>> skipped;

  // ---- fabric tokens (BaseResource operation methods) ----
  CALL("fabric.tokens.create_subscriber_token",
       c.fabric().tokens.create_subscriber_token({.reference = SENTINEL}));
  CALL("fabric.tokens.refresh_subscriber_token",
       c.fabric().tokens.refresh_subscriber_token({.refresh_token = SENTINEL}));
  CALL("fabric.tokens.create_invite_token",
       c.fabric().tokens.create_invite_token({.address_id = SENTINEL}));
  CALL("fabric.tokens.create_guest_token",
       c.fabric().tokens.create_guest_token({.allowed_addresses = json::array()}));
  CALL("fabric.tokens.create_embed_token",
       c.fabric().tokens.create_embed_token({.token = SENTINEL}));

  // ---- fabric CRUD + list_addresses resources (base create(json); explicit
  // per-resource so CALL() captures a usable call literal, not a lambda arg) ----
  CALL("fabric.swml_scripts.list", c.fabric().swml_scripts.list(q));
  CALL("fabric.swml_scripts.create", c.fabric().swml_scripts.create(body));
  CALL("fabric.swml_scripts.get", c.fabric().swml_scripts.get(id));
  CALL("fabric.swml_scripts.update", c.fabric().swml_scripts.update(id, body));
  CALL("fabric.swml_scripts.delete_", c.fabric().swml_scripts.delete_(id));
  CALL("fabric.swml_scripts.list_addresses", c.fabric().swml_scripts.list_addresses(id, q));
  CALL("fabric.relay_applications.list", c.fabric().relay_applications.list(q));
  CALL("fabric.relay_applications.create", c.fabric().relay_applications.create(body));
  CALL("fabric.relay_applications.get", c.fabric().relay_applications.get(id));
  CALL("fabric.relay_applications.update", c.fabric().relay_applications.update(id, body));
  CALL("fabric.relay_applications.delete_", c.fabric().relay_applications.delete_(id));
  CALL("fabric.relay_applications.list_addresses",
       c.fabric().relay_applications.list_addresses(id, q));
  CALL("fabric.freeswitch_connectors.list", c.fabric().freeswitch_connectors.list(q));
  CALL("fabric.freeswitch_connectors.create", c.fabric().freeswitch_connectors.create(body));
  CALL("fabric.freeswitch_connectors.get", c.fabric().freeswitch_connectors.get(id));
  CALL("fabric.freeswitch_connectors.update", c.fabric().freeswitch_connectors.update(id, body));
  CALL("fabric.freeswitch_connectors.delete_", c.fabric().freeswitch_connectors.delete_(id));
  CALL("fabric.freeswitch_connectors.list_addresses",
       c.fabric().freeswitch_connectors.list_addresses(id, q));
  CALL("fabric.sip_endpoints.list", c.fabric().sip_endpoints.list(q));
  CALL("fabric.sip_endpoints.create", c.fabric().sip_endpoints.create(body));
  CALL("fabric.sip_endpoints.get", c.fabric().sip_endpoints.get(id));
  CALL("fabric.sip_endpoints.update", c.fabric().sip_endpoints.update(id, body));
  CALL("fabric.sip_endpoints.delete_", c.fabric().sip_endpoints.delete_(id));
  CALL("fabric.sip_endpoints.list_addresses", c.fabric().sip_endpoints.list_addresses(id, q));
  CALL("fabric.cxml_scripts.list", c.fabric().cxml_scripts.list(q));
  CALL("fabric.cxml_scripts.create", c.fabric().cxml_scripts.create(body));
  CALL("fabric.cxml_scripts.get", c.fabric().cxml_scripts.get(id));
  CALL("fabric.cxml_scripts.update", c.fabric().cxml_scripts.update(id, body));
  CALL("fabric.cxml_scripts.delete_", c.fabric().cxml_scripts.delete_(id));
  CALL("fabric.cxml_scripts.list_addresses", c.fabric().cxml_scripts.list_addresses(id, q));
  CALL("fabric.ai_agents.list", c.fabric().ai_agents.list(q));
  CALL("fabric.ai_agents.create", c.fabric().ai_agents.create(body));
  CALL("fabric.ai_agents.get", c.fabric().ai_agents.get(id));
  CALL("fabric.ai_agents.update", c.fabric().ai_agents.update(id, body));
  CALL("fabric.ai_agents.delete_", c.fabric().ai_agents.delete_(id));
  CALL("fabric.ai_agents.list_addresses", c.fabric().ai_agents.list_addresses(id, q));
  CALL("fabric.sip_gateways.list", c.fabric().sip_gateways.list(q));
  CALL("fabric.sip_gateways.create", c.fabric().sip_gateways.create(body));
  CALL("fabric.sip_gateways.get", c.fabric().sip_gateways.get(id));
  CALL("fabric.sip_gateways.update", c.fabric().sip_gateways.update(id, body));
  CALL("fabric.sip_gateways.delete_", c.fabric().sip_gateways.delete_(id));
  CALL("fabric.sip_gateways.list_addresses", c.fabric().sip_gateways.list_addresses(id, q));
  CALL("fabric.swml_webhooks.list", c.fabric().swml_webhooks.list(q));
  CALL("fabric.swml_webhooks.create", c.fabric().swml_webhooks.create(body));
  CALL("fabric.swml_webhooks.get", c.fabric().swml_webhooks.get(id));
  CALL("fabric.swml_webhooks.update", c.fabric().swml_webhooks.update(id, body));
  CALL("fabric.swml_webhooks.delete_", c.fabric().swml_webhooks.delete_(id));
  CALL("fabric.swml_webhooks.list_addresses", c.fabric().swml_webhooks.list_addresses(id, q));
  CALL("fabric.cxml_webhooks.list", c.fabric().cxml_webhooks.list(q));
  CALL("fabric.cxml_webhooks.create", c.fabric().cxml_webhooks.create(body));
  CALL("fabric.cxml_webhooks.get", c.fabric().cxml_webhooks.get(id));
  CALL("fabric.cxml_webhooks.update", c.fabric().cxml_webhooks.update(id, body));
  CALL("fabric.cxml_webhooks.delete_", c.fabric().cxml_webhooks.delete_(id));
  CALL("fabric.cxml_webhooks.list_addresses", c.fabric().cxml_webhooks.list_addresses(id, q));

  // conference_rooms: CRUD + a SINGULARISED sibling addresses path. The
  // generated resource overrides list_addresses to that singular path (hiding
  // the plural base member), so there is ONE canonical list_addresses route.
  CALL("fabric.conference_rooms.list", c.fabric().conference_rooms.list(q));
  CALL("fabric.conference_rooms.create", c.fabric().conference_rooms.create(body));
  CALL("fabric.conference_rooms.get", c.fabric().conference_rooms.get(id));
  CALL("fabric.conference_rooms.update", c.fabric().conference_rooms.update(id, body));
  CALL("fabric.conference_rooms.delete_", c.fabric().conference_rooms.delete_(id));
  CALL("fabric.conference_rooms.list_addresses", c.fabric().conference_rooms.list_addresses(id, q));

  // call_flows: CRUD + addresses + versions/deploy (singular sub-paths).
  CALL("fabric.call_flows.list", c.fabric().call_flows.list(q));
  CALL("fabric.call_flows.create", c.fabric().call_flows.create(body));
  CALL("fabric.call_flows.get", c.fabric().call_flows.get(id));
  CALL("fabric.call_flows.update", c.fabric().call_flows.update(id, body));
  CALL("fabric.call_flows.delete_", c.fabric().call_flows.delete_(id));
  CALL("fabric.call_flows.list_addresses", c.fabric().call_flows.list_addresses(id, q));
  CALL("fabric.call_flows.list_versions", c.fabric().call_flows.list_versions(id, q));
  CALL("fabric.call_flows.deploy_version", c.fabric().call_flows.deploy_version(id, body));

  // subscribers: CRUD + addresses + sip endpoint sub-resource.
  CALL("fabric.subscribers.list", c.fabric().subscribers.list(q));
  CALL("fabric.subscribers.create", c.fabric().subscribers.create(body));
  CALL("fabric.subscribers.get", c.fabric().subscribers.get(id));
  CALL("fabric.subscribers.update", c.fabric().subscribers.update(id, body));
  CALL("fabric.subscribers.delete_", c.fabric().subscribers.delete_(id));
  CALL("fabric.subscribers.list_addresses", c.fabric().subscribers.list_addresses(id, q));
  CALL("fabric.subscribers.list_sip_endpoints", c.fabric().subscribers.list_sip_endpoints(id, q));
  CALL(
      "fabric.subscribers.create_sip_endpoint",
      c.fabric().subscribers.create_sip_endpoint(id, {.username = SENTINEL, .password = SENTINEL}));
  CALL("fabric.subscribers.get_sip_endpoint", c.fabric().subscribers.get_sip_endpoint(id, id));
  CALL("fabric.subscribers.update_sip_endpoint",
       c.fabric().subscribers.update_sip_endpoint(id, id, {}));
  CALL("fabric.subscribers.delete_sip_endpoint",
       c.fabric().subscribers.delete_sip_endpoint(id, id));

  // cxml_applications: list/get/update/delete/list_addresses dispatch; create()
  // throws by design (cXML apps cannot be created via this API).
  CALL("fabric.cxml_applications.list", c.fabric().cxml_applications.list(q));
  CALL("fabric.cxml_applications.get", c.fabric().cxml_applications.get(id));
  CALL("fabric.cxml_applications.update", c.fabric().cxml_applications.update(id, {}));
  CALL("fabric.cxml_applications.delete_", c.fabric().cxml_applications.delete_(id));
  CALL("fabric.cxml_applications.list_addresses",
       c.fabric().cxml_applications.list_addresses(id, q));
  skipped.emplace_back("fabric.cxml_applications.create",
                       "no create route — cXML apps cannot be created via this API (BaseResource)");

  // resources(): generic read/delete + address-assignment routes.
  CALL("fabric.resources.list", c.fabric().resources.list(q));
  CALL("fabric.resources.get", c.fabric().resources.get(id));
  CALL("fabric.resources.delete_", c.fabric().resources.delete_(id));
  CALL("fabric.resources.list_addresses", c.fabric().resources.list_addresses(id, q));
  CALL("fabric.resources.assign_domain_application",
       c.fabric().resources.assign_domain_application(id, {.domain_application_id = SENTINEL}));
  CALL("fabric.resources.assign_phone_route",
       c.fabric().resources.assign_phone_route(id,
                                               {.phone_route_id = SENTINEL, .handler = SENTINEL}));

  // fabric addresses (list/get only).
  CALL("fabric.addresses.list", c.fabric().addresses.list(q));
  CALL("fabric.addresses.get", c.fabric().addresses.get(id));

  // ---- calling (command dispatch: all POST /api/calling/calls) ----
  CALL("calling.dial", c.calling().dial({.from = SENTINEL, .to = SENTINEL}));
  CALL("calling.update", c.calling().update({.id = SENTINEL}));
  CALL("calling.end", c.calling().end(id, {}));
  CALL("calling.transfer", c.calling().transfer(id, {.dest = json::object()}));
  CALL("calling.disconnect", c.calling().disconnect(id, {}));
  CALL("calling.play", c.calling().play(id, {.play = json::object()}));
  CALL("calling.play_pause", c.calling().play_pause(id, {.control_id = SENTINEL}));
  CALL("calling.play_resume", c.calling().play_resume(id, {.control_id = SENTINEL}));
  CALL("calling.play_stop", c.calling().play_stop(id, {.control_id = SENTINEL}));
  CALL("calling.play_volume", c.calling().play_volume(id, {.control_id = SENTINEL, .volume = 1.0}));
  CALL("calling.record", c.calling().record(id, {}));
  CALL("calling.record_pause", c.calling().record_pause(id, {.control_id = SENTINEL}));
  CALL("calling.record_resume", c.calling().record_resume(id, {.control_id = SENTINEL}));
  CALL("calling.record_stop", c.calling().record_stop(id, {.control_id = SENTINEL}));
  CALL("calling.collect", c.calling().collect(id, {}));
  CALL("calling.collect_stop", c.calling().collect_stop(id, {.control_id = SENTINEL}));
  CALL("calling.collect_start_input_timers",
       c.calling().collect_start_input_timers(id, {.control_id = SENTINEL}));
  CALL("calling.detect", c.calling().detect(id, {.detect = json::object()}));
  CALL("calling.detect_stop", c.calling().detect_stop(id, {.control_id = SENTINEL}));
  CALL("calling.tap", c.calling().tap(id, {.tap = json::object(), .device = json::object()}));
  CALL("calling.tap_stop", c.calling().tap_stop(id, {.control_id = SENTINEL}));
  CALL("calling.stream", c.calling().stream(id, {.url = SENTINEL}));
  CALL("calling.stream_stop", c.calling().stream_stop(id, {.control_id = SENTINEL}));
  CALL("calling.denoise", c.calling().denoise(id, {}));
  CALL("calling.denoise_stop", c.calling().denoise_stop(id, {}));
  CALL("calling.transcribe", c.calling().transcribe(id, {}));
  CALL("calling.transcribe_stop", c.calling().transcribe_stop(id, {.control_id = SENTINEL}));
  CALL("calling.ai_message", c.calling().ai_message(id, {}));
  CALL("calling.ai_hold", c.calling().ai_hold(id, {}));
  CALL("calling.ai_unhold", c.calling().ai_unhold(id, {}));
  CALL("calling.ai_stop", c.calling().ai_stop(id, {.control_id = SENTINEL}));
  CALL("calling.live_transcribe", c.calling().live_transcribe(id, {.action = json::object()}));
  CALL("calling.live_translate", c.calling().live_translate(id, {.action = json::object()}));
  CALL("calling.send_fax_stop", c.calling().send_fax_stop(id, {.control_id = SENTINEL}));
  CALL("calling.receive_fax_stop", c.calling().receive_fax_stop(id, {.control_id = SENTINEL}));
  CALL("calling.refer", c.calling().refer(id, {.device = json::object()}));
  CALL("calling.user_event", c.calling().user_event(id, {.event = json::object()}));

  // ---- phone_numbers (CRUD + search + set_* handler wrappers) ----
  CALL("phone_numbers.list", c.phone_numbers().list(q));
  CALL("phone_numbers.create", c.phone_numbers().create(body));
  CALL("phone_numbers.get", c.phone_numbers().get(id));
  CALL("phone_numbers.update", c.phone_numbers().update(id, body));
  CALL("phone_numbers.delete_", c.phone_numbers().delete_(id));
  CALL("phone_numbers.search", c.phone_numbers().search(q));

  // ---- datasphere (documents) ----
  CALL("datasphere.documents.list", c.datasphere().documents.list(q));
  CALL("datasphere.documents.create", c.datasphere().documents.create(body));
  CALL("datasphere.documents.get", c.datasphere().documents.get(id));
  CALL("datasphere.documents.update", c.datasphere().documents.update(id, body));
  CALL("datasphere.documents.delete_", c.datasphere().documents.delete_(id));
  CALL("datasphere.documents.search", c.datasphere().documents.search({.query_string = SENTINEL}));
  CALL("datasphere.documents.list_chunks", c.datasphere().documents.list_chunks(id, q));
  CALL("datasphere.documents.get_chunk", c.datasphere().documents.get_chunk(id, id));
  CALL("datasphere.documents.delete_chunk", c.datasphere().documents.delete_chunk(id, id));

  // ---- video ----
  CALL("video.rooms.list", c.video().rooms.list(q));
  CALL("video.rooms.create", c.video().rooms.create(body));
  CALL("video.rooms.get", c.video().rooms.get(id));
  CALL("video.rooms.update", c.video().rooms.update(id, body));
  CALL("video.rooms.delete_", c.video().rooms.delete_(id));
  CALL("video.rooms.list_streams", c.video().rooms.list_streams(id, q));
  CALL("video.rooms.create_stream", c.video().rooms.create_stream(id, {.url = SENTINEL}));
  CALL("video.room_tokens.create", c.video().room_tokens.create({.room_name = SENTINEL}));
  CALL("video.room_sessions.list", c.video().room_sessions.list(q));
  CALL("video.room_sessions.get", c.video().room_sessions.get(id));
  CALL("video.room_sessions.list_members", c.video().room_sessions.list_members(id, q));
  CALL("video.room_sessions.list_recordings", c.video().room_sessions.list_recordings(id, q));
  CALL("video.room_sessions.list_events", c.video().room_sessions.list_events(id, q));
  CALL("video.room_recordings.list", c.video().room_recordings.list(q));
  CALL("video.room_recordings.get", c.video().room_recordings.get(id));
  CALL("video.room_recordings.delete_", c.video().room_recordings.delete_(id));
  CALL("video.room_recordings.list_events", c.video().room_recordings.list_events(id, q));
  CALL("video.conferences.list", c.video().conferences.list(q));
  CALL("video.conferences.create", c.video().conferences.create(body));
  CALL("video.conferences.get", c.video().conferences.get(id));
  CALL("video.conferences.update", c.video().conferences.update(id, body));
  CALL("video.conferences.delete_", c.video().conferences.delete_(id));
  CALL("video.conferences.list_conference_tokens",
       c.video().conferences.list_conference_tokens(id, q));
  CALL("video.conferences.list_streams", c.video().conferences.list_streams(id, q));
  CALL("video.conferences.create_stream",
       c.video().conferences.create_stream(id, {.url = SENTINEL}));
  CALL("video.conference_tokens.get", c.video().conference_tokens.get(id));
  CALL("video.conference_tokens.reset", c.video().conference_tokens.reset(id));
  CALL("video.streams.get", c.video().streams.get(id));
  CALL("video.streams.update", c.video().streams.update(id, {.url = SENTINEL}));
  CALL("video.streams.delete_", c.video().streams.delete_(id));

  // ---- queues ----
  CALL("queues.list", c.queues().list(q));
  CALL("queues.create", c.queues().create(body));
  CALL("queues.get", c.queues().get(id));
  CALL("queues.update", c.queues().update(id, body));
  CALL("queues.delete_", c.queues().delete_(id));
  CALL("queues.list_members", c.queues().list_members(id, q));
  CALL("queues.get_next_member", c.queues().get_next_member(id));
  CALL("queues.get_member", c.queues().get_member(id, id));

  // ---- number_groups ----
  CALL("number_groups.list", c.number_groups().list(q));
  CALL("number_groups.create", c.number_groups().create(body));
  CALL("number_groups.get", c.number_groups().get(id));
  CALL("number_groups.update", c.number_groups().update(id, body));
  CALL("number_groups.delete_", c.number_groups().delete_(id));
  CALL("number_groups.list_memberships", c.number_groups().list_memberships(id, q));
  CALL("number_groups.add_membership",
       c.number_groups().add_membership(id, {.phone_number_id = SENTINEL}));
  CALL("number_groups.get_membership", c.number_groups().get_membership(id));
  CALL("number_groups.delete_membership", c.number_groups().delete_membership(id));

  // ---- sip_profile (singleton) ----
  CALL("sip_profile.get", c.sip_profile().get());
  CALL("sip_profile.update", c.sip_profile().update({}));

  // ---- lookup (single GET) ----
  CALL("lookup.phone_number", c.lookup().phone_number(id));

  // ---- mfa ----
  CALL("mfa.sms", c.mfa().sms({.to = SENTINEL}));
  CALL("mfa.call", c.mfa().call({.to = SENTINEL}));
  CALL("mfa.verify", c.mfa().verify(id, {.token = SENTINEL}));

  // ---- registry (10DLC) ----
  CALL("registry.brands.list", c.registry().brands.list(q));
  CALL("registry.brands.create", c.registry().brands.create(body));
  CALL("registry.brands.get", c.registry().brands.get(id));
  CALL("registry.brands.list_campaigns", c.registry().brands.list_campaigns(id, q));
  CALL("registry.brands.create_campaign", c.registry().brands.create_campaign(id, body));
  CALL("registry.campaigns.get", c.registry().campaigns.get(id));
  CALL("registry.campaigns.update", c.registry().campaigns.update(id, {}));
  CALL("registry.campaigns.list_numbers", c.registry().campaigns.list_numbers(id, q));
  CALL("registry.campaigns.list_orders", c.registry().campaigns.list_orders(id, q));
  CALL("registry.campaigns.create_order", c.registry().campaigns.create_order(id, {}));
  CALL("registry.orders.get", c.registry().orders.get(id));
  CALL("registry.numbers.delete_", c.registry().numbers.delete_(id));

  // ---- logs ----
  CALL("logs.messages.list", c.logs().messages.list(q));
  CALL("logs.messages.get", c.logs().messages.get(id));
  CALL("logs.voice.list", c.logs().voice.list(q));
  CALL("logs.voice.get", c.logs().voice.get(id));
  CALL("logs.voice.list_events", c.logs().voice.list_events(id, q));
  CALL("logs.fax.list", c.logs().fax.list(q));
  CALL("logs.fax.get", c.logs().fax.get(id));
  CALL("logs.conferences.list", c.logs().conferences.list(q));

  // ---- project ----
  CALL("project.tokens.create",
       c.project().tokens.create({.name = SENTINEL, .permissions = json::array()}));
  CALL("project.tokens.update", c.project().tokens.update(id, {}));
  CALL("project.tokens.delete_", c.project().tokens.delete_(id));

  // ---- projects (/api/projects full CRUD + signing-key rotate) ----
  CALL("projects.list", c.projects().list(q));
  CALL("projects.create", c.projects().create(body));
  CALL("projects.get", c.projects().get(id));
  CALL("projects.update", c.projects().update(id, body));
  CALL("projects.delete_", c.projects().delete_(id));
  CALL("projects.rotate_signing_key", c.projects().rotate_signing_key(id));

  // ---- messages (/api/messaging/messages send + redact) ----
  CALL("messages.create", c.messages().create({.to = SENTINEL, .from = SENTINEL}));
  CALL("messages.update", c.messages().update(id, {.body = SENTINEL}));

  // ---- pubsub / chat (token-only) ----
  CALL("pubsub.create_token", c.pubsub().create_token({.ttl = 60, .channels = json::array()}));
  CALL("chat.create_token", c.chat().create_token({.ttl = 60, .channels = json::array()}));

  // ---- verified callers (CRUD + verification flow) ----
  CALL("verified_callers.list", c.verified_callers().list(q));
  CALL("verified_callers.create", c.verified_callers().create(body));
  CALL("verified_callers.get", c.verified_callers().get(id));
  CALL("verified_callers.update", c.verified_callers().update(id, body));
  CALL("verified_callers.delete_", c.verified_callers().delete_(id));
  CALL("verified_callers.redial_verification", c.verified_callers().redial_verification(id));
  CALL("verified_callers.submit_verification",
       c.verified_callers().submit_verification(id, {.verification_code = SENTINEL}));

  // ---- top-level narrow resources ----
  CALL("addresses.list", c.addresses().list(q));
  CALL("addresses.create", c.addresses().create({.label = SENTINEL,
                                                 .country = SENTINEL,
                                                 .first_name = SENTINEL,
                                                 .last_name = SENTINEL,
                                                 .street_number = SENTINEL,
                                                 .street_name = SENTINEL,
                                                 .city = SENTINEL,
                                                 .state = SENTINEL,
                                                 .postal_code = SENTINEL}));
  CALL("addresses.get", c.addresses().get(id));
  CALL("addresses.delete_", c.addresses().delete_(id));
  CALL("recordings.list", c.recordings().list(q));
  CALL("recordings.get", c.recordings().get(id));
  CALL("recordings.delete_", c.recordings().delete_(id));
  CALL("short_codes.list", c.short_codes().list(q));
  CALL("short_codes.get", c.short_codes().get(id));
  CALL("short_codes.update",
       c.short_codes().update(id, {.name = SENTINEL, .message_handler = SENTINEL}));
  CALL("imported_numbers.create",
       c.imported_numbers().create({.number = SENTINEL, .number_type = SENTINEL}));

  return skipped;
}

}  // namespace

int main() {
  CaptureServer srv;
  g_srv = &srv;
  RestClient client = RestClient::with_base_url(srv.base_url(), SENTINEL, "tok");

  std::vector<std::pair<std::string, std::string>> skipped;
  json errors = json::array();
  try {
    skipped = invoke_all(client);
  } catch (const std::exception& e) {
    errors.push_back(std::string("invoke_all threw: ") + e.what());
  }

  // Dedupe plan entries by (method, template); keep the first via/call for each.
  json route_recs = json::array();
  std::map<std::pair<std::string, std::string>, PlanEntry> uniq;
  std::vector<std::pair<std::string, std::string>> order;
  for (const auto& pe : g_plan) {
    auto key = std::make_pair(pe.method, pe.path_template);
    if (uniq.find(key) == uniq.end()) {
      uniq[key] = pe;
      order.push_back(key);
    }
  }
  std::sort(order.begin(), order.end());
  for (const auto& key : order) {
    const auto& pe = uniq[key];
    route_recs.push_back({{"method", pe.method},
                          {"path_template", pe.path_template},
                          {"via", pe.via},
                          {"call", pe.call}});
  }

  json skipped_recs = json::array();
  for (const auto& kr : skipped) {
    skipped_recs.push_back({{"key", kr.first}, {"reason", kr.second}});
  }

  json out = {{"routes", route_recs}, {"skipped", skipped_recs}, {"errors", errors}};
  std::cout << out.dump(2) << "\n";
  return 0;
}
