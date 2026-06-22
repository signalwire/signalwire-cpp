// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
//
// SPEC-PARITY route registry for the REST client (Set B producer).
//
// C++ has no runtime reflection, so we cannot walk the client's methods the
// way the Go/Python registries do. Instead we derive Set B from REAL dispatch:
// stand up a loopback httplib server that records every (method, path) it
// receives and answers 200 {}; point a RestClient at it; invoke every public
// namespace method once; then read back the routes the SDK actually sent. The
// route strings are produced by the SDK's own path-building code in
// rest_client.hpp — never hand-authored here — so they cannot silently drift
// from what the client really does. (Same Option-2 approach as Rust's
// src/bin/route_registry.rs; the recording transport is the LocalCaptureServer
// pattern already used by tests/test_rest_phone_binding.cpp.)
//
// Path parameters are passed as the SENTINEL (one path segment, no '/'), which
// we normalise back to {id}; the porting-sdk spec matcher turns {id} -> X and
// matches against the canonical patterns. The project id the client is built
// with also becomes a path segment (compat's {AccountSid}); we pass the same
// sentinel so it too normalises to {id}.
//
// Completeness is enforced by the consumer, not by trust: a method we FORGET to
// invoke shows up as a phantom A-B not-implemented gap in
// diff_spec_implementation.py (its canonical route is never hit), and a method
// that dispatches a non-spec route shows up as a B-A divergence. Either fails
// the SPEC-PARITY gate. The invocation list below is therefore cross-checked
// against the canonical spec on every gate run.
//
// Output: JSON {"routes":[{"method","path_template","via"}],"skipped":[...],
// "errors":[...]} on stdout.
//
// Run from the build dir: ./route_registry

#include <algorithm>
#include <chrono>
#include <iostream>
#include <map>
#include <mutex>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <thread>
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

  std::vector<std::pair<std::string, std::string>> captured() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return captured_;
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

// Invoke every public REST method once so the capture server records its route.
// This is the ONE place the methods are enumerated; it is cross-checked against
// the canonical spec by the SPEC-PARITY diff. Keep it exhaustive.
//
// Returns the list of {key, reason} for methods that DISPATCH NOTHING by design
// (currently only cxml_applications.create, which throws and issues no request).
std::vector<std::pair<std::string, std::string>> invoke_all(RestClient& c) {
  const std::string id = SENTINEL;
  const json p = json::object();
  const std::map<std::string, std::string> q;
  std::vector<std::pair<std::string, std::string>> skipped;

  // --- fabric ---
  auto& f = c.fabric();
  (void)f.tokens.create_subscriber_token(p);
  (void)f.tokens.refresh_subscriber_token(p);
  (void)f.tokens.create_invite_token(p);
  (void)f.tokens.create_guest_token(p);
  (void)f.tokens.create_embed_token(p);

  // PUT-update + PATCH-update fabric resources sharing the CRUD + list_addresses
  // surface. Each goes through list/create/get/update/delete/list_addresses.
  auto crud_fabric = [&](auto& r) {
    (void)r.list(q);
    (void)r.create(p);
    (void)r.get(id);
    (void)r.update(id, p);
    (void)r.del(id);
    (void)r.list_addresses(id, q);
  };
  crud_fabric(f.swml_scripts);
  crud_fabric(f.relay_applications);
  crud_fabric(f.freeswitch_connectors);
  crud_fabric(f.sip_endpoints);
  crud_fabric(f.cxml_scripts);
  crud_fabric(f.ai_agents);
  crud_fabric(f.sip_gateways);
  crud_fabric(f.swml_webhooks);
  crud_fabric(f.cxml_webhooks);
  crud_fabric(f.conference_rooms);

  // call_flows: CRUD + addresses + versions/deploy (singular sub-paths).
  (void)f.call_flows.list(q);
  (void)f.call_flows.create(p);
  (void)f.call_flows.get(id);
  (void)f.call_flows.update(id, p);
  (void)f.call_flows.del(id);
  (void)f.call_flows.list_addresses(id, q);
  (void)f.call_flows.list_versions(id, q);
  (void)f.call_flows.deploy_version(id, p);

  // subscribers: CRUD + addresses + sip endpoint sub-resource.
  (void)f.subscribers.list(q);
  (void)f.subscribers.create(p);
  (void)f.subscribers.get(id);
  (void)f.subscribers.update(id, p);
  (void)f.subscribers.del(id);
  (void)f.subscribers.list_addresses(id, q);
  (void)f.subscribers.list_sip_endpoints(id, q);
  (void)f.subscribers.create_sip_endpoint(id, p);
  (void)f.subscribers.get_sip_endpoint(id, id);
  (void)f.subscribers.update_sip_endpoint(id, id, p);
  (void)f.subscribers.delete_sip_endpoint(id, id);

  // cxml_applications: list/get/update/delete/list_addresses dispatch; create()
  // throws by design (cXML apps cannot be created via this API) and issues no
  // request — recorded in `skipped`, mirrors python's skip.
  (void)f.cxml_applications.list(q);
  (void)f.cxml_applications.get(id);
  (void)f.cxml_applications.update(id, p);
  (void)f.cxml_applications.del(id);
  (void)f.cxml_applications.list_addresses(id, q);
  try {
    (void)f.cxml_applications.create(p);
  } catch (const std::exception&) {
    // expected: no POST /cxml_applications canonical route.
  }
  skipped.emplace_back("fabric.cxml_applications.create",
                       "no create route — throws by design (cXML apps cannot be created)");

  // resources(): generic read/delete + address-assignment routes.
  (void)f.resources.list(q);
  (void)f.resources.get(id);
  (void)f.resources.delete_(id);
  (void)f.resources.list_addresses(id, q);
  (void)f.resources.assign_domain_application(id, p);
  (void)f.resources.assign_phone_route(id, p);

  // fabric addresses (list/get only).
  (void)f.addresses.list(q);
  (void)f.addresses.get(id);

  // --- calling (command dispatch: all POST /api/calling/calls) ---
  auto& cl = c.calling();
  (void)cl.dial(p);
  (void)cl.update(p);
  (void)cl.end(id, p);
  (void)cl.transfer(id, p);
  (void)cl.disconnect(id, p);
  (void)cl.play(id, p);
  (void)cl.play_pause(id, p);
  (void)cl.play_resume(id, p);
  (void)cl.play_stop(id, p);
  (void)cl.play_volume(id, p);
  (void)cl.record(id, p);
  (void)cl.record_pause(id, p);
  (void)cl.record_resume(id, p);
  (void)cl.record_stop(id, p);
  (void)cl.collect(id, p);
  (void)cl.collect_stop(id, p);
  (void)cl.collect_start_input_timers(id, p);
  (void)cl.detect(id, p);
  (void)cl.detect_stop(id, p);
  (void)cl.tap(id, p);
  (void)cl.tap_stop(id, p);
  (void)cl.stream(id, p);
  (void)cl.stream_stop(id, p);
  (void)cl.denoise(id, p);
  (void)cl.denoise_stop(id, p);
  (void)cl.transcribe(id, p);
  (void)cl.transcribe_stop(id, p);
  (void)cl.ai_message(id, p);
  (void)cl.ai_hold(id, p);
  (void)cl.ai_unhold(id, p);
  (void)cl.ai_stop(id, p);
  (void)cl.live_transcribe(id, p);
  (void)cl.live_translate(id, p);
  (void)cl.send_fax_stop(id, p);
  (void)cl.receive_fax_stop(id, p);
  (void)cl.refer(id, p);
  (void)cl.user_event(id, p);

  // --- phone_numbers ---
  auto& pn = c.phone_numbers();
  (void)pn.list(q);
  (void)pn.create(p);
  (void)pn.get(id);
  (void)pn.update(id, p);
  (void)pn.del(id);
  (void)pn.search(q);

  // --- datasphere (documents) ---
  auto& d = c.datasphere().documents;
  (void)d.list(q);
  (void)d.create(p);
  (void)d.get(id);
  (void)d.update(id, p);
  (void)d.del(id);
  (void)d.search(p);
  (void)d.list_chunks(id, q);
  (void)d.get_chunk(id, id);
  (void)d.delete_chunk(id, id);

  // --- video ---
  auto& v = c.video();
  (void)v.rooms.list(q);
  (void)v.rooms.create(p);
  (void)v.rooms.get(id);
  (void)v.rooms.update(id, p);
  (void)v.rooms.del(id);
  (void)v.rooms.list_streams(id, q);
  (void)v.rooms.create_stream(id, p);
  (void)v.room_tokens.create(p);
  (void)v.room_sessions.list(q);
  (void)v.room_sessions.get(id);
  (void)v.room_sessions.list_members(id, q);
  (void)v.room_sessions.list_recordings(id, q);
  (void)v.room_sessions.list_events(id, q);
  (void)v.room_recordings.list(q);
  (void)v.room_recordings.get(id);
  (void)v.room_recordings.delete_(id);
  (void)v.room_recordings.list_events(id, q);
  (void)v.conferences.list(q);
  (void)v.conferences.create(p);
  (void)v.conferences.get(id);
  (void)v.conferences.update(id, p);
  (void)v.conferences.del(id);
  (void)v.conferences.list_conference_tokens(id, q);
  (void)v.conferences.list_streams(id, q);
  (void)v.conferences.create_stream(id, p);
  (void)v.conference_tokens.get(id);
  (void)v.conference_tokens.reset(id);
  (void)v.streams.get(id);
  (void)v.streams.update(id, p);
  (void)v.streams.delete_(id);

  // --- compat (account-scoped LAML) ---
  auto& cm = c.compat();
  (void)cm.accounts.list(q);
  (void)cm.accounts.create(p);
  (void)cm.accounts.get(id);
  (void)cm.accounts.update(id, p);
  (void)cm.calls.list(q);
  (void)cm.calls.create(p);
  (void)cm.calls.get(id);
  (void)cm.calls.update(id, p);
  (void)cm.calls.del(id);
  (void)cm.calls.start_recording(id, p);
  (void)cm.calls.update_recording(id, id, p);
  (void)cm.calls.start_stream(id, p);
  (void)cm.calls.stop_stream(id, id, p);
  (void)cm.messages.list(q);
  (void)cm.messages.create(p);
  (void)cm.messages.get(id);
  (void)cm.messages.update(id, p);
  (void)cm.messages.del(id);
  (void)cm.messages.list_media(id, q);
  (void)cm.messages.get_media(id, id);
  (void)cm.messages.delete_media(id, id);
  (void)cm.faxes.list(q);
  (void)cm.faxes.create(p);
  (void)cm.faxes.get(id);
  (void)cm.faxes.update(id, p);
  (void)cm.faxes.del(id);
  (void)cm.faxes.list_media(id, q);
  (void)cm.faxes.get_media(id, id);
  (void)cm.faxes.delete_media(id, id);
  (void)cm.conferences.list(q);
  (void)cm.conferences.get(id);
  (void)cm.conferences.update(id, p);
  (void)cm.conferences.list_participants(id, q);
  (void)cm.conferences.get_participant(id, id);
  (void)cm.conferences.update_participant(id, id, p);
  (void)cm.conferences.remove_participant(id, id);
  (void)cm.conferences.list_recordings(id, q);
  (void)cm.conferences.get_recording(id, id);
  (void)cm.conferences.update_recording(id, id, p);
  (void)cm.conferences.delete_recording(id, id);
  (void)cm.conferences.start_stream(id, p);
  (void)cm.conferences.stop_stream(id, id, p);
  (void)cm.phone_numbers.list(q);
  (void)cm.phone_numbers.purchase(p);
  (void)cm.phone_numbers.get(id);
  (void)cm.phone_numbers.update(id, p);
  (void)cm.phone_numbers.delete_(id);
  (void)cm.phone_numbers.import_number(p);
  (void)cm.phone_numbers.list_available_countries(q);
  (void)cm.phone_numbers.search_local(id, q);
  (void)cm.phone_numbers.search_toll_free(id, q);
  (void)cm.applications.list(q);
  (void)cm.applications.create(p);
  (void)cm.applications.get(id);
  (void)cm.applications.update(id, p);
  (void)cm.applications.del(id);
  (void)cm.laml_bins.list(q);
  (void)cm.laml_bins.create(p);
  (void)cm.laml_bins.get(id);
  (void)cm.laml_bins.update(id, p);
  (void)cm.laml_bins.del(id);
  (void)cm.queues.list(q);
  (void)cm.queues.create(p);
  (void)cm.queues.get(id);
  (void)cm.queues.update(id, p);
  (void)cm.queues.del(id);
  (void)cm.queues.list_members(id, q);
  (void)cm.queues.get_member(id, id);
  (void)cm.queues.dequeue_member(id, id, p);
  (void)cm.recordings.list(q);
  (void)cm.recordings.get(id);
  (void)cm.recordings.delete_(id);
  (void)cm.transcriptions.list(q);
  (void)cm.transcriptions.get(id);
  (void)cm.transcriptions.delete_(id);
  (void)cm.tokens.create(p);
  (void)cm.tokens.update(id, p);
  (void)cm.tokens.delete_(id);

  // --- queues ---
  auto& qn = c.queues();
  (void)qn.list(q);
  (void)qn.create(p);
  (void)qn.get(id);
  (void)qn.update(id, p);
  (void)qn.del(id);
  (void)qn.list_members(id, q);
  (void)qn.get_next_member(id);
  (void)qn.get_member(id, id);

  // --- number_groups ---
  auto& ng = c.number_groups();
  (void)ng.list(q);
  (void)ng.create(p);
  (void)ng.get(id);
  (void)ng.update(id, p);
  (void)ng.del(id);
  (void)ng.list_memberships(id, q);
  (void)ng.add_membership(id, p);
  (void)ng.get_membership(id);
  (void)ng.delete_membership(id);

  // --- sip_profile (singleton) ---
  auto& sp = c.sip_profile();
  (void)sp.get();
  (void)sp.update(p);

  // --- lookup (single GET) ---
  (void)c.lookup().lookup(id);

  // --- mfa ---
  auto& m = c.mfa();
  (void)m.sms(p);
  (void)m.call(p);
  (void)m.verify(id, p);

  // --- registry (10DLC) ---
  auto& reg = c.registry();
  (void)reg.brands.list(q);
  (void)reg.brands.create(p);
  (void)reg.brands.get(id);
  (void)reg.brands.list_campaigns(id, q);
  (void)reg.brands.create_campaign(id, p);
  (void)reg.campaigns.get(id);
  (void)reg.campaigns.update(id, p);
  (void)reg.campaigns.list_numbers(id, q);
  (void)reg.campaigns.list_orders(id, q);
  (void)reg.campaigns.create_order(id, p);
  (void)reg.orders.get(id);
  (void)reg.numbers.delete_(id);

  // --- logs ---
  auto& lg = c.logs();
  (void)lg.messages.list(q);
  (void)lg.messages.get(id);
  (void)lg.voice.list(q);
  (void)lg.voice.get(id);
  (void)lg.voice.list_events(id, q);
  (void)lg.fax.list(q);
  (void)lg.fax.get(id);
  (void)lg.conferences.list(q);

  // --- project ---
  auto& pt = c.project().tokens;
  (void)pt.create(p);
  (void)pt.update(id, p);
  (void)pt.delete_(id);

  // --- pubsub / chat (token-only) ---
  (void)c.pubsub().create_token(p);
  (void)c.chat().create_token(p);

  // --- verified callers (CRUD + verification flow) ---
  auto& vc = c.verified_callers();
  (void)vc.list(q);
  (void)vc.create(p);
  (void)vc.get(id);
  (void)vc.update(id, p);
  (void)vc.del(id);
  (void)vc.redial_verification(id);
  (void)vc.submit_verification(id, p);

  // --- top-level narrow resources (each mirrors python's verb set) ---
  auto& addr = c.addresses();
  (void)addr.list(q);
  (void)addr.create(p);
  (void)addr.get(id);
  (void)addr.delete_(id);
  auto& rec = c.recordings();
  (void)rec.list(q);
  (void)rec.get(id);
  (void)rec.delete_(id);
  auto& sc = c.short_codes();
  (void)sc.list(q);
  (void)sc.get(id);
  (void)sc.update(id, p);
  (void)c.imported_numbers().create(p);

  return skipped;
}

}  // namespace

int main() {
  CaptureServer srv;
  RestClient client = RestClient::with_base_url(srv.base_url(), SENTINEL, "tok");

  std::vector<std::pair<std::string, std::string>> skipped;
  json errors = json::array();
  try {
    skipped = invoke_all(client);
  } catch (const std::exception& e) {
    errors.push_back(std::string("invoke_all threw: ") + e.what());
  }

  // Harvest every dispatched (method, path) and dedupe by template.
  std::set<std::pair<std::string, std::string>> routes;
  for (const auto& mr : srv.captured()) {
    routes.emplace(mr.first, templatize(mr.second));
  }

  json route_recs = json::array();
  for (const auto& mp : routes) {
    route_recs.push_back(
        {{"method", mp.first}, {"path_template", mp.second}, {"via", json::array()}});
  }
  json skipped_recs = json::array();
  for (const auto& kr : skipped) {
    skipped_recs.push_back({{"key", kr.first}, {"reason", kr.second}});
  }

  json out = {{"routes", route_recs}, {"skipped", skipped_recs}, {"errors", errors}};
  std::cout << out.dump(2) << "\n";
  return 0;
}
