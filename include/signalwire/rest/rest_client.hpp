// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#pragma once

#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>

#include "signalwire/rest/http_client.hpp"
#include "signalwire/rest/phone_call_handler.hpp"

namespace signalwire {
namespace rest {

using json = nlohmann::json;

/// Top-level SignalWire REST client with all API namespaces
class RestClient {
 public:
  RestClient(const std::string& space, const std::string& project_id, const std::string& token);

  /// Initialize from environment variables
  [[nodiscard]] static RestClient from_env();

  /// Construct with an explicit pre-built base URL (`http://...` or
  /// `https://...`) instead of synthesizing one from the SignalWire
  /// space hostname. Used by audit harnesses pointing the client at
  /// loopback fixtures. The space-based constructor remains the
  /// production path.
  [[nodiscard]] static RestClient with_base_url(const std::string& base_url,
                                                const std::string& project_id,
                                                const std::string& token);

  /// Project ID accessor (read-only).
  const std::string& project_id() const { return project_id_; }

  // ========================================================================
  // API Namespaces (all 21)
  // ========================================================================

  // ---------------------------------------------------------------------
  // Fabric resource sub-types (Python parity).
  //
  // Most fabric resources sit under /api/fabric/resources/{type}, with
  // some using PUT for updates (FabricResourcePUT) and some PATCH
  // (FabricResource). The CallFlows / ConferenceRooms / Subscribers /
  // CxmlApplications classes layer on Python-parity sub-collection
  // helpers, including the singularised "/call_flow" / "/conference_room"
  // address sub-paths.
  //
  // FabricAddresses + FabricTokens + GenericResources are special: they
  // sit at non-/resources base paths (or under /api/fabric directly).
  // ---------------------------------------------------------------------
  struct FabricResource : public CrudResource {
    FabricResource(const HttpClient& c, const std::string& base) : CrudResource(c, base) {}
    // Python parity: list_addresses sub-collection.
    [[nodiscard]] json list_addresses(const std::string& resource_id,
                                      const std::map<std::string, std::string>& params = {}) const {
      return client_.get(base_path_ + "/" + resource_id + "/addresses", params);
    }
  };

  struct FabricResourcePUT : public FabricResource {
    FabricResourcePUT(const HttpClient& c, const std::string& base) : FabricResource(c, base) {}
    // Python: _update_method = "PUT".
    [[nodiscard]] json update(const std::string& resource_id, const json& data) const {
      return client_.put(base_path_ + "/" + resource_id, data);
    }
  };

  struct FabricCallFlows : public FabricResourcePUT {
    FabricCallFlows(const HttpClient& c)
        : FabricResourcePUT(c, "/api/fabric/resources/call_flows") {}

    // Python parity: API uses singular 'call_flow' for sub-resource paths.
    // Python rewrites /call_flows -> /call_flow at call time.
    std::string singular_base() const { return "/api/fabric/resources/call_flow"; }
    [[nodiscard]] json list_addresses(const std::string& flow_id,
                                      const std::map<std::string, std::string>& params = {}) const {
      return client_.get(singular_base() + "/" + flow_id + "/addresses", params);
    }
    [[nodiscard]] json list_versions(const std::string& flow_id,
                                     const std::map<std::string, std::string>& params = {}) const {
      return client_.get(singular_base() + "/" + flow_id + "/versions", params);
    }
    [[nodiscard]] json deploy_version(const std::string& flow_id, const json& data) const {
      return client_.post(singular_base() + "/" + flow_id + "/versions", data);
    }
  };

  struct FabricConferenceRooms : public FabricResourcePUT {
    FabricConferenceRooms(const HttpClient& c)
        : FabricResourcePUT(c, "/api/fabric/resources/conference_rooms") {}

    // Python parity: singular 'conference_room' for sub-resources.
    std::string singular_base() const { return "/api/fabric/resources/conference_room"; }
    [[nodiscard]] json list_addresses(const std::string& room_id,
                                      const std::map<std::string, std::string>& params = {}) const {
      return client_.get(singular_base() + "/" + room_id + "/addresses", params);
    }
  };

  struct FabricSubscribers : public FabricResourcePUT {
    FabricSubscribers(const HttpClient& c)
        : FabricResourcePUT(c, "/api/fabric/resources/subscribers") {}

    [[nodiscard]] json list_sip_endpoints(
        const std::string& subscriber_id,
        const std::map<std::string, std::string>& params = {}) const {
      return client_.get(base_path_ + "/" + subscriber_id + "/sip_endpoints", params);
    }
    [[nodiscard]] json create_sip_endpoint(const std::string& subscriber_id,
                                           const json& data) const {
      return client_.post(base_path_ + "/" + subscriber_id + "/sip_endpoints", data);
    }
    [[nodiscard]] json get_sip_endpoint(const std::string& subscriber_id,
                                        const std::string& endpoint_id) const {
      return client_.get(base_path_ + "/" + subscriber_id + "/sip_endpoints/" + endpoint_id);
    }
    // Python parity: PATCH for sub-resource update.
    [[nodiscard]] json update_sip_endpoint(const std::string& subscriber_id,
                                           const std::string& endpoint_id, const json& data) const {
      return client_.patch(base_path_ + "/" + subscriber_id + "/sip_endpoints/" + endpoint_id,
                           data);
    }
    [[nodiscard]] json delete_sip_endpoint(const std::string& subscriber_id,
                                           const std::string& endpoint_id) const {
      return client_.del(base_path_ + "/" + subscriber_id + "/sip_endpoints/" + endpoint_id);
    }
  };

  struct FabricCxmlApplications : public FabricResourcePUT {
    FabricCxmlApplications(const HttpClient& c)
        : FabricResourcePUT(c, "/api/fabric/resources/cxml_applications") {}
    // Python parity: deliberately refuses create.
    [[nodiscard]] json create(const json& /*data*/) const {
      throw std::runtime_error("cXML applications cannot be created via this API");
    }
  };

  struct FabricAddresses {
    const HttpClient& client;
    std::string base_path = "/api/fabric/addresses";
    FabricAddresses(const HttpClient& c) : client(c) {}
    [[nodiscard]] json list(const std::map<std::string, std::string>& params = {}) const {
      return client.get(base_path, params);
    }
    [[nodiscard]] json get(const std::string& address_id) const {
      return client.get(base_path + "/" + address_id);
    }
  };

  struct FabricGenericResources {
    const HttpClient& client;
    std::string base_path = "/api/fabric/resources";
    FabricGenericResources(const HttpClient& c) : client(c) {}
    [[nodiscard]] json list(const std::map<std::string, std::string>& params = {}) const {
      return client.get(base_path, params);
    }
    [[nodiscard]] json get(const std::string& resource_id) const {
      return client.get(base_path + "/" + resource_id);
    }
    [[nodiscard]] json delete_(const std::string& resource_id) const {
      return client.del(base_path + "/" + resource_id);
    }
    [[nodiscard]] json list_addresses(const std::string& resource_id,
                                      const std::map<std::string, std::string>& params = {}) const {
      return client.get(base_path + "/" + resource_id + "/addresses", params);
    }
    [[nodiscard]] json assign_domain_application(const std::string& resource_id,
                                                 const json& data) const {
      return client.post(base_path + "/" + resource_id + "/domain_applications", data);
    }
    // Deprecated for the common binding cases; see Python parity.
    [[nodiscard]] json assign_phone_route(const std::string& resource_id, const json& data) const {
      return client.post(base_path + "/" + resource_id + "/phone_routes", data);
    }
  };

  struct FabricTokens {
    const HttpClient& client;
    std::string base_path = "/api/fabric";
    FabricTokens(const HttpClient& c) : client(c) {}

    [[nodiscard]] json create_subscriber_token(const json& data) const {
      return client.post(base_path + "/subscribers/tokens", data);
    }
    [[nodiscard]] json refresh_subscriber_token(const json& data) const {
      return client.post(base_path + "/subscribers/tokens/refresh", data);
    }
    [[nodiscard]] json create_invite_token(const json& data) const {
      return client.post(base_path + "/subscriber/invites", data);
    }
    [[nodiscard]] json create_guest_token(const json& data) const {
      return client.post(base_path + "/guests/tokens", data);
    }
    [[nodiscard]] json create_embed_token(const json& data) const {
      return client.post(base_path + "/embeds/tokens", data);
    }
  };

  struct FabricNamespace {
    // Python-parity sub-resources (live under /api/fabric/resources).
    // PUT-update resources.
    FabricResourcePUT swml_scripts;
    FabricResourcePUT relay_applications;
    FabricCallFlows call_flows;
    FabricConferenceRooms conference_rooms;
    FabricResourcePUT freeswitch_connectors;
    FabricSubscribers subscribers;
    FabricResourcePUT sip_endpoints;
    FabricResourcePUT cxml_scripts;
    FabricCxmlApplications cxml_applications;
    // PATCH-update resources (default in CrudResource).
    FabricResource swml_webhooks;
    FabricResource ai_agents;
    FabricResource sip_gateways;
    FabricResource cxml_webhooks;
    // Top-level fabric resources (different bases).
    FabricGenericResources resources;
    FabricAddresses addresses;
    FabricTokens tokens;

    // ``conferences`` alias retained -- it's not in Python (Python
    // uses ``conference_rooms``) but legacy C++ examples reference
    // ``fabric().conferences.create(...)``. We point it at the same
    // /conference_rooms path so the wire is consistent.
    CrudResource conferences;

    // Legacy synonym fields kept for older test_rest_fabric.cpp accessor
    // checks. Their REST methods aren't expected to be called from any
    // test that ports a Python file (Python doesn't have these names);
    // they exist so ``(void)f.routing`` etc keeps compiling.
    CrudResource routing;
    CrudResource agents;
    CrudResource domains;
    CrudResource topics;
    CrudResource webhooks;

    FabricNamespace(const HttpClient& c)
        : swml_scripts(c, "/api/fabric/resources/swml_scripts"),
          relay_applications(c, "/api/fabric/resources/relay_applications"),
          call_flows(c),
          conference_rooms(c),
          freeswitch_connectors(c, "/api/fabric/resources/freeswitch_connectors"),
          subscribers(c),
          sip_endpoints(c, "/api/fabric/resources/sip_endpoints"),
          cxml_scripts(c, "/api/fabric/resources/cxml_scripts"),
          cxml_applications(c),
          swml_webhooks(c, "/api/fabric/resources/swml_webhooks"),
          ai_agents(c, "/api/fabric/resources/ai_agents"),
          sip_gateways(c, "/api/fabric/resources/sip_gateways"),
          cxml_webhooks(c, "/api/fabric/resources/cxml_webhooks"),
          resources(c),
          addresses(c),
          tokens(c),
          conferences(c, "/api/fabric/resources/conference_rooms"),
          routing(c, "/api/fabric/routing"),
          agents(c, "/api/fabric/agents"),
          domains(c, "/api/fabric/domains"),
          topics(c, "/api/fabric/topics"),
          webhooks(c, "/api/fabric/webhooks") {}
  };

  struct CallingNamespace {
    const HttpClient& client;

    CallingNamespace(const HttpClient& c) : client(c) {}

    // ----------------------------------------------------------------
    // Command-dispatch entry point (matches Python CallingNamespace).
    //
    // All 32 commands are POST /api/calling/calls with a body shaped
    // {"command": "<name>", "id": "<call_id>"?, "params": {...}}.
    // ``id`` is omitted for calls that target no specific call (e.g.
    // ``dial`` and ``update``).
    // ----------------------------------------------------------------
    [[nodiscard]] json execute(const std::string& command, const json& params,
                               const std::optional<std::string>& call_id = std::nullopt) const {
      json body = {{"command", command}, {"params", params}};
      if (call_id) {
        body["id"] = *call_id;
      }
      return client.post("/api/calling/calls", body);
    }

    // Lifecycle
    [[nodiscard]] json dial(const json& params) const { return execute("dial", params); }
    [[nodiscard]] json update(const json& params) const { return execute("update", params); }
    [[nodiscard]] json end(const std::string& call_id, const json& params = json::object()) const {
      return execute("calling.end", params, call_id);
    }
    [[nodiscard]] json transfer(const std::string& call_id, const json& params) const {
      return execute("calling.transfer", params, call_id);
    }
    [[nodiscard]] json disconnect(const std::string& call_id,
                                  const json& params = json::object()) const {
      return execute("calling.disconnect", params, call_id);
    }

    // Play
    [[nodiscard]] json play(const std::string& call_id, const json& params) const {
      return execute("calling.play", params, call_id);
    }
    [[nodiscard]] json play_pause(const std::string& call_id, const json& params) const {
      return execute("calling.play.pause", params, call_id);
    }
    [[nodiscard]] json play_resume(const std::string& call_id, const json& params) const {
      return execute("calling.play.resume", params, call_id);
    }
    [[nodiscard]] json play_stop(const std::string& call_id, const json& params) const {
      return execute("calling.play.stop", params, call_id);
    }
    [[nodiscard]] json play_volume(const std::string& call_id, const json& params) const {
      return execute("calling.play.volume", params, call_id);
    }

    // Record
    [[nodiscard]] json record(const std::string& call_id, const json& params) const {
      return execute("calling.record", params, call_id);
    }
    [[nodiscard]] json record_pause(const std::string& call_id, const json& params) const {
      return execute("calling.record.pause", params, call_id);
    }
    [[nodiscard]] json record_resume(const std::string& call_id, const json& params) const {
      return execute("calling.record.resume", params, call_id);
    }
    [[nodiscard]] json record_stop(const std::string& call_id, const json& params) const {
      return execute("calling.record.stop", params, call_id);
    }

    // Collect
    [[nodiscard]] json collect(const std::string& call_id, const json& params) const {
      return execute("calling.collect", params, call_id);
    }
    [[nodiscard]] json collect_stop(const std::string& call_id, const json& params) const {
      return execute("calling.collect.stop", params, call_id);
    }
    [[nodiscard]] json collect_start_input_timers(const std::string& call_id,
                                                  const json& params) const {
      return execute("calling.collect.start_input_timers", params, call_id);
    }

    // Detect
    [[nodiscard]] json detect(const std::string& call_id, const json& params) const {
      return execute("calling.detect", params, call_id);
    }
    [[nodiscard]] json detect_stop(const std::string& call_id, const json& params) const {
      return execute("calling.detect.stop", params, call_id);
    }

    // Tap
    [[nodiscard]] json tap(const std::string& call_id, const json& params) const {
      return execute("calling.tap", params, call_id);
    }
    [[nodiscard]] json tap_stop(const std::string& call_id, const json& params) const {
      return execute("calling.tap.stop", params, call_id);
    }

    // Stream
    [[nodiscard]] json stream(const std::string& call_id, const json& params) const {
      return execute("calling.stream", params, call_id);
    }
    [[nodiscard]] json stream_stop(const std::string& call_id, const json& params) const {
      return execute("calling.stream.stop", params, call_id);
    }

    // Denoise
    [[nodiscard]] json denoise(const std::string& call_id,
                               const json& params = json::object()) const {
      return execute("calling.denoise", params, call_id);
    }
    [[nodiscard]] json denoise_stop(const std::string& call_id, const json& params) const {
      return execute("calling.denoise.stop", params, call_id);
    }

    // Transcribe
    [[nodiscard]] json transcribe(const std::string& call_id, const json& params) const {
      return execute("calling.transcribe", params, call_id);
    }
    [[nodiscard]] json transcribe_stop(const std::string& call_id, const json& params) const {
      return execute("calling.transcribe.stop", params, call_id);
    }

    // AI
    [[nodiscard]] json ai_message(const std::string& call_id, const json& params) const {
      return execute("calling.ai_message", params, call_id);
    }
    [[nodiscard]] json ai_hold(const std::string& call_id,
                               const json& params = json::object()) const {
      return execute("calling.ai_hold", params, call_id);
    }
    [[nodiscard]] json ai_unhold(const std::string& call_id,
                                 const json& params = json::object()) const {
      return execute("calling.ai_unhold", params, call_id);
    }
    [[nodiscard]] json ai_stop(const std::string& call_id,
                               const json& params = json::object()) const {
      return execute("calling.ai.stop", params, call_id);
    }

    // Live transcribe / translate
    [[nodiscard]] json live_transcribe(const std::string& call_id, const json& params) const {
      return execute("calling.live_transcribe", params, call_id);
    }
    [[nodiscard]] json live_translate(const std::string& call_id, const json& params) const {
      return execute("calling.live_translate", params, call_id);
    }

    // Fax
    [[nodiscard]] json send_fax_stop(const std::string& call_id,
                                     const json& params = json::object()) const {
      return execute("calling.send_fax.stop", params, call_id);
    }
    [[nodiscard]] json receive_fax_stop(const std::string& call_id,
                                        const json& params = json::object()) const {
      return execute("calling.receive_fax.stop", params, call_id);
    }

    // SIP refer
    [[nodiscard]] json refer(const std::string& call_id, const json& params) const {
      return execute("calling.refer", params, call_id);
    }

    // Custom user event
    [[nodiscard]] json user_event(const std::string& call_id, const json& params) const {
      return execute("calling.user_event", params, call_id);
    }

    // ----------------------------------------------------------------
    // Legacy convenience accessors (kept for backwards compatibility).
    // These use direct REST paths rather than the command-dispatch
    // wire format and are retained so existing tests keep linking.
    // ----------------------------------------------------------------
    [[nodiscard]] json list_calls(const std::map<std::string, std::string>& p = {}) const {
      return client.get("/api/calling/calls", p);
    }
    [[nodiscard]] json get_call(const std::string& id) const {
      return client.get("/api/calling/calls/" + id);
    }
    [[nodiscard]] json update_call(const std::string& id, const json& data) const {
      return client.put("/api/calling/calls/" + id, data);
    }
    [[nodiscard]] json end_call(const std::string& id) const {
      return client.del("/api/calling/calls/" + id);
    }
    [[nodiscard]] json connect(const std::string& id, const json& data) const {
      return client.post("/api/calling/calls/" + id + "/connect", data);
    }
    [[nodiscard]] json send_digits(const std::string& id, const json& data) const {
      return client.post("/api/calling/calls/" + id + "/send_digits", data);
    }
    [[nodiscard]] json answer(const std::string& id) const {
      return client.post("/api/calling/calls/" + id + "/answer");
    }
    [[nodiscard]] json hangup(const std::string& id) const {
      return client.post("/api/calling/calls/" + id + "/hangup");
    }
    [[nodiscard]] json hold(const std::string& id) const {
      return client.post("/api/calling/calls/" + id + "/hold");
    }
    [[nodiscard]] json unhold(const std::string& id) const {
      return client.post("/api/calling/calls/" + id + "/unhold");
    }
  };

  struct PhoneNumbersNamespace : public CrudResource {
    PhoneNumbersNamespace(const HttpClient& c) : CrudResource(c, "/api/relay/rest/phone_numbers") {}
    [[nodiscard]] json search(const std::map<std::string, std::string>& p) const {
      return client_.get(base_path_ + "/search", p);
    }
    [[nodiscard]] json buy(const json& data) const {
      return client_.post(base_path_ + "/buy", data);
    }
    [[nodiscard]] json release(const std::string& id) const {
      return client_.del(base_path_ + "/" + id);
    }

    // ====================================================================
    // Typed binding helpers
    //
    // Each ``set_*`` helper wraps ``update`` with the right ``call_handler``
    // value and companion field. The server auto-materializes the matching
    // Fabric resource; you do NOT need to pre-create ``swml_webhooks``,
    // ``cxml_webhooks``, or an AI agent and you do NOT need to call
    // ``assign_phone_route``. See ``rest/docs/phone-binding.md``.
    //
    // The ``make_*_body`` static helpers expose the JSON body that the
    // helper would send — useful for tests or when you need to amend the
    // body before calling ``update`` directly.
    // ====================================================================

    /// Options for binding a phone number to a cXML (Twilio-compat) webhook.
    struct CxmlWebhookOptions {
      std::optional<std::string> fallback_url;
      std::optional<std::string> status_callback_url;
    };

    /// Options for binding a phone number to a call flow.
    struct CallFlowOptions {
      /// Accepts ``"working_copy"`` or ``"current_deployed"``
      /// (server default when omitted).
      std::optional<std::string> version;
    };

    /// Options for binding a phone number to a RELAY topic.
    struct RelayTopicOptions {
      std::optional<std::string> status_callback_url;
    };

    // -- Body builders (pure, no side effects — useful for tests) --------

    [[nodiscard]] static json make_swml_webhook_body(const std::string& url) {
      return {
          {"call_handler", to_wire_string(PhoneCallHandler::RelayScript)},
          {"call_relay_script_url", url},
      };
    }

    [[nodiscard]] static json make_cxml_webhook_body(const std::string& url,
                                                     const CxmlWebhookOptions& opts = {}) {
      json body = {
          {"call_handler", to_wire_string(PhoneCallHandler::LamlWebhooks)},
          {"call_request_url", url},
      };
      if (opts.fallback_url) {
        body["call_fallback_url"] = *opts.fallback_url;
      }
      if (opts.status_callback_url) {
        body["call_status_callback_url"] = *opts.status_callback_url;
      }
      return body;
    }

    [[nodiscard]] static json make_cxml_application_body(const std::string& application_id) {
      return {
          {"call_handler", to_wire_string(PhoneCallHandler::LamlApplication)},
          {"call_laml_application_id", application_id},
      };
    }

    [[nodiscard]] static json make_ai_agent_body(const std::string& agent_id) {
      return {
          {"call_handler", to_wire_string(PhoneCallHandler::AiAgent)},
          {"call_ai_agent_id", agent_id},
      };
    }

    [[nodiscard]] static json make_call_flow_body(const std::string& flow_id,
                                                  const CallFlowOptions& opts = {}) {
      json body = {
          {"call_handler", to_wire_string(PhoneCallHandler::CallFlow)},
          {"call_flow_id", flow_id},
      };
      if (opts.version) {
        body["call_flow_version"] = *opts.version;
      }
      return body;
    }

    [[nodiscard]] static json make_relay_application_body(const std::string& name) {
      return {
          {"call_handler", to_wire_string(PhoneCallHandler::RelayApplication)},
          {"call_relay_application", name},
      };
    }

    [[nodiscard]] static json make_relay_topic_body(const std::string& topic,
                                                    const RelayTopicOptions& opts = {}) {
      json body = {
          {"call_handler", to_wire_string(PhoneCallHandler::RelayTopic)},
          {"call_relay_topic", topic},
      };
      if (opts.status_callback_url) {
        body["call_relay_topic_status_callback_url"] = *opts.status_callback_url;
      }
      return body;
    }

    // -- Typed helpers ---------------------------------------------------

    /// Route inbound calls to an SWML webhook URL.
    /// Server auto-creates a ``swml_webhook`` Fabric resource keyed off
    /// this URL.
    [[nodiscard]] json set_swml_webhook(const std::string& resource_id,
                                        const std::string& url) const {
      return update(resource_id, make_swml_webhook_body(url));
    }

    /// Route inbound calls to a cXML (Twilio-compat / LAML) webhook.
    /// Despite the wire value ``laml_webhooks`` being plural, this creates
    /// a single ``cxml_webhook`` Fabric resource. Extra options populate
    /// fallback and status-callback fields.
    [[nodiscard]] json set_cxml_webhook(const std::string& resource_id, const std::string& url,
                                        const CxmlWebhookOptions& opts = {}) const {
      return update(resource_id, make_cxml_webhook_body(url, opts));
    }

    /// Route inbound calls to an existing cXML application by ID.
    [[nodiscard]] json set_cxml_application(const std::string& resource_id,
                                            const std::string& application_id) const {
      return update(resource_id, make_cxml_application_body(application_id));
    }

    /// Route inbound calls to an AI Agent Fabric resource by ID.
    [[nodiscard]] json set_ai_agent(const std::string& resource_id,
                                    const std::string& agent_id) const {
      return update(resource_id, make_ai_agent_body(agent_id));
    }

    /// Route inbound calls to a Call Flow by ID.
    /// ``opts.version`` accepts ``"working_copy"`` or ``"current_deployed"``.
    [[nodiscard]] json set_call_flow(const std::string& resource_id, const std::string& flow_id,
                                     const CallFlowOptions& opts = {}) const {
      return update(resource_id, make_call_flow_body(flow_id, opts));
    }

    /// Route inbound calls to a named RELAY application.
    [[nodiscard]] json set_relay_application(const std::string& resource_id,
                                             const std::string& name) const {
      return update(resource_id, make_relay_application_body(name));
    }

    /// Route inbound calls to a RELAY topic (client subscription).
    [[nodiscard]] json set_relay_topic(const std::string& resource_id, const std::string& topic,
                                       const RelayTopicOptions& opts = {}) const {
      return update(resource_id, make_relay_topic_body(topic, opts));
    }
  };

  // Datasphere: documents are a CRUD resource with chunk sub-operations.
  // We keep the existing CrudResource-typed ``documents`` field for any
  // callers using it directly; add a richer DatasphereDocuments wrapper
  // with the search + chunk methods on a parallel ``documents_ext``-style
  // accessor not needed because the CrudResource methods (list/get/etc.)
  // already cover the basics. The chunk methods live directly on the
  // namespace via ``DatasphereDocumentsExt``.
  struct DatasphereDocuments : public CrudResource {
    DatasphereDocuments(const HttpClient& c) : CrudResource(c, "/api/datasphere/documents") {}
    [[nodiscard]] json search(const json& data) const {
      return client_.post(base_path_ + "/search", data);
    }
    [[nodiscard]] json list_chunks(const std::string& document_id,
                                   const std::map<std::string, std::string>& params = {}) const {
      return client_.get(base_path_ + "/" + document_id + "/chunks", params);
    }
    [[nodiscard]] json get_chunk(const std::string& document_id,
                                 const std::string& chunk_id) const {
      return client_.get(base_path_ + "/" + document_id + "/chunks/" + chunk_id);
    }
    [[nodiscard]] json delete_chunk(const std::string& document_id,
                                    const std::string& chunk_id) const {
      return client_.del(base_path_ + "/" + document_id + "/chunks/" + chunk_id);
    }
  };

  struct DatasphereNamespace {
    DatasphereDocuments documents;
    const HttpClient& client;

    DatasphereNamespace(const HttpClient& c) : documents(c), client(c) {}

    // Legacy alias retained -- callers can still invoke
    // ``client.datasphere().search({...})`` instead of going through
    // ``documents.search``.
    [[nodiscard]] json search(const json& data) const {
      return client.post("/api/datasphere/documents/search", data);
    }
  };

  // ---------------------------------------------------------------------
  // Video sub-resources (Python parity).
  //
  // VideoRooms is a CrudResource that uses PUT for updates (not PATCH)
  // and adds /streams sub-collection helpers.
  //
  // VideoRoomSessions, VideoRoomRecordings, VideoConferenceTokens, and
  // VideoStreams are read-mostly resources without full CRUD.
  //
  // VideoConferences mirrors VideoRooms (PUT update) plus conference
  // tokens + streams sub-collections.
  // ---------------------------------------------------------------------
  struct VideoRooms : public CrudResource {
    VideoRooms(const HttpClient& c) : CrudResource(c, "/api/video/rooms") {}
    // Python parity: PUT (CrudResource default is PATCH).
    [[nodiscard]] json update(const std::string& room_id, const json& data) const {
      return client_.put(base_path_ + "/" + room_id, data);
    }
    [[nodiscard]] json list_streams(const std::string& room_id,
                                    const std::map<std::string, std::string>& params = {}) const {
      return client_.get(base_path_ + "/" + room_id + "/streams", params);
    }
    [[nodiscard]] json create_stream(const std::string& room_id, const json& data) const {
      return client_.post(base_path_ + "/" + room_id + "/streams", data);
    }
  };

  struct VideoRoomTokens {
    const HttpClient& client;
    std::string base_path = "/api/video/room_tokens";
    VideoRoomTokens(const HttpClient& c) : client(c) {}
    [[nodiscard]] json create(const json& data) const { return client.post(base_path, data); }
  };

  struct VideoRoomSessions {
    const HttpClient& client;
    std::string base_path = "/api/video/room_sessions";
    VideoRoomSessions(const HttpClient& c) : client(c) {}
    [[nodiscard]] json list(const std::map<std::string, std::string>& params = {}) const {
      return client.get(base_path, params);
    }
    [[nodiscard]] json get(const std::string& session_id) const {
      return client.get(base_path + "/" + session_id);
    }
    [[nodiscard]] json list_events(const std::string& session_id,
                                   const std::map<std::string, std::string>& params = {}) const {
      return client.get(base_path + "/" + session_id + "/events", params);
    }
    [[nodiscard]] json list_members(const std::string& session_id,
                                    const std::map<std::string, std::string>& params = {}) const {
      return client.get(base_path + "/" + session_id + "/members", params);
    }
    [[nodiscard]] json list_recordings(
        const std::string& session_id,
        const std::map<std::string, std::string>& params = {}) const {
      return client.get(base_path + "/" + session_id + "/recordings", params);
    }
  };

  struct VideoRoomRecordings {
    const HttpClient& client;
    std::string base_path = "/api/video/room_recordings";
    VideoRoomRecordings(const HttpClient& c) : client(c) {}
    [[nodiscard]] json list(const std::map<std::string, std::string>& params = {}) const {
      return client.get(base_path, params);
    }
    [[nodiscard]] json get(const std::string& recording_id) const {
      return client.get(base_path + "/" + recording_id);
    }
    [[nodiscard]] json delete_(const std::string& recording_id) const {
      return client.del(base_path + "/" + recording_id);
    }
    [[nodiscard]] json list_events(const std::string& recording_id,
                                   const std::map<std::string, std::string>& params = {}) const {
      return client.get(base_path + "/" + recording_id + "/events", params);
    }
  };

  struct VideoConferences : public CrudResource {
    VideoConferences(const HttpClient& c) : CrudResource(c, "/api/video/conferences") {}
    // Python parity: PUT.
    [[nodiscard]] json update(const std::string& conf_id, const json& data) const {
      return client_.put(base_path_ + "/" + conf_id, data);
    }
    [[nodiscard]] json list_conference_tokens(
        const std::string& conf_id, const std::map<std::string, std::string>& params = {}) const {
      return client_.get(base_path_ + "/" + conf_id + "/conference_tokens", params);
    }
    [[nodiscard]] json list_streams(const std::string& conf_id,
                                    const std::map<std::string, std::string>& params = {}) const {
      return client_.get(base_path_ + "/" + conf_id + "/streams", params);
    }
    [[nodiscard]] json create_stream(const std::string& conf_id, const json& data) const {
      return client_.post(base_path_ + "/" + conf_id + "/streams", data);
    }
  };

  struct VideoConferenceTokens {
    const HttpClient& client;
    std::string base_path = "/api/video/conference_tokens";
    VideoConferenceTokens(const HttpClient& c) : client(c) {}
    [[nodiscard]] json get(const std::string& token_id) const {
      return client.get(base_path + "/" + token_id);
    }
    [[nodiscard]] json reset(const std::string& token_id) const {
      return client.post(base_path + "/" + token_id + "/reset");
    }
  };

  struct VideoStreams {
    const HttpClient& client;
    std::string base_path = "/api/video/streams";
    VideoStreams(const HttpClient& c) : client(c) {}
    [[nodiscard]] json get(const std::string& stream_id) const {
      return client.get(base_path + "/" + stream_id);
    }
    // Python parity: PUT.
    [[nodiscard]] json update(const std::string& stream_id, const json& data) const {
      return client.put(base_path + "/" + stream_id, data);
    }
    [[nodiscard]] json delete_(const std::string& stream_id) const {
      return client.del(base_path + "/" + stream_id);
    }
  };

  struct VideoNamespace {
    VideoRooms rooms;
    VideoRoomTokens room_tokens;
    VideoRoomSessions room_sessions;
    VideoRoomRecordings room_recordings;
    VideoConferences conferences;
    VideoConferenceTokens conference_tokens;
    VideoStreams streams;

    // Legacy alias retained so the older test_rest_namespaces.cpp
    // (which references ``v.recordings``) keeps compiling. New code
    // should use ``room_recordings``.
    CrudResource recordings;

    VideoNamespace(const HttpClient& c)
        : rooms(c),
          room_tokens(c),
          room_sessions(c),
          room_recordings(c),
          conferences(c),
          conference_tokens(c),
          streams(c),
          recordings(c, "/api/video/room_recordings") {}
  };

  // ---------------------------------------------------------------------
  // Compat sub-resources (Twilio-compatible LAML API). These mirror the
  // Python ``CompatCalls`` / ``CompatMessages`` / ``CompatFaxes`` /
  // ``CompatPhoneNumbers`` classes. All paths are scoped by the project
  // (account_sid) at the LAML root ``/api/laml/2010-04-01/Accounts/{sid}``.
  // ---------------------------------------------------------------------
  struct CompatCalls : public CrudResource {
    CompatCalls(const HttpClient& c, const std::string& base) : CrudResource(c, base) {}

    // Override update -- compat uses POST not PUT for resource updates.
    [[nodiscard]] json update(const std::string& sid, const json& data) const {
      return client_.post(base_path_ + "/" + sid, data);
    }

    // Recording sub-resources.
    [[nodiscard]] json start_recording(const std::string& call_sid, const json& data) const {
      return client_.post(base_path_ + "/" + call_sid + "/Recordings", data);
    }
    [[nodiscard]] json update_recording(const std::string& call_sid,
                                        const std::string& recording_sid, const json& data) const {
      return client_.post(base_path_ + "/" + call_sid + "/Recordings/" + recording_sid, data);
    }

    // Stream sub-resources.
    [[nodiscard]] json start_stream(const std::string& call_sid, const json& data) const {
      return client_.post(base_path_ + "/" + call_sid + "/Streams", data);
    }
    [[nodiscard]] json stop_stream(const std::string& call_sid, const std::string& stream_sid,
                                   const json& data) const {
      return client_.post(base_path_ + "/" + call_sid + "/Streams/" + stream_sid, data);
    }
  };

  struct CompatMessages : public CrudResource {
    CompatMessages(const HttpClient& c, const std::string& base) : CrudResource(c, base) {}

    [[nodiscard]] json update(const std::string& sid, const json& data) const {
      return client_.post(base_path_ + "/" + sid, data);
    }

    [[nodiscard]] json list_media(const std::string& message_sid,
                                  const std::map<std::string, std::string>& params = {}) const {
      return client_.get(base_path_ + "/" + message_sid + "/Media", params);
    }
    [[nodiscard]] json get_media(const std::string& message_sid,
                                 const std::string& media_sid) const {
      return client_.get(base_path_ + "/" + message_sid + "/Media/" + media_sid);
    }
    [[nodiscard]] json delete_media(const std::string& message_sid,
                                    const std::string& media_sid) const {
      return client_.del(base_path_ + "/" + message_sid + "/Media/" + media_sid);
    }
  };

  struct CompatFaxes : public CrudResource {
    CompatFaxes(const HttpClient& c, const std::string& base) : CrudResource(c, base) {}

    [[nodiscard]] json update(const std::string& sid, const json& data) const {
      return client_.post(base_path_ + "/" + sid, data);
    }

    [[nodiscard]] json list_media(const std::string& fax_sid,
                                  const std::map<std::string, std::string>& params = {}) const {
      return client_.get(base_path_ + "/" + fax_sid + "/Media", params);
    }
    [[nodiscard]] json get_media(const std::string& fax_sid, const std::string& media_sid) const {
      return client_.get(base_path_ + "/" + fax_sid + "/Media/" + media_sid);
    }
    [[nodiscard]] json delete_media(const std::string& fax_sid,
                                    const std::string& media_sid) const {
      return client_.del(base_path_ + "/" + fax_sid + "/Media/" + media_sid);
    }
  };

  struct CompatPhoneNumbers {
    const HttpClient& client;
    std::string base_path;       // /api/laml/.../IncomingPhoneNumbers
    std::string available_base;  // /api/laml/.../AvailablePhoneNumbers
    std::string imported_base;   // /api/laml/.../ImportedPhoneNumbers

    CompatPhoneNumbers(const HttpClient& c, const std::string& account_base)
        : client(c),
          base_path(account_base + "/IncomingPhoneNumbers"),
          available_base(account_base + "/AvailablePhoneNumbers"),
          imported_base(account_base + "/ImportedPhoneNumbers") {}

    [[nodiscard]] json list(const std::map<std::string, std::string>& params = {}) const {
      return client.get(base_path, params);
    }
    [[nodiscard]] json get(const std::string& sid) const {
      return client.get(base_path + "/" + sid);
    }
    [[nodiscard]] json update(const std::string& sid, const json& data) const {
      return client.post(base_path + "/" + sid, data);
    }
    [[nodiscard]] json delete_(const std::string& sid) const {
      return client.del(base_path + "/" + sid);
    }
    [[nodiscard]] json purchase(const json& data) const { return client.post(base_path, data); }
    [[nodiscard]] json import_number(const json& data) const {
      return client.post(imported_base, data);
    }
    [[nodiscard]] json list_available_countries(
        const std::map<std::string, std::string>& params = {}) const {
      return client.get(available_base, params);
    }
    [[nodiscard]] json search_local(const std::string& country,
                                    const std::map<std::string, std::string>& params = {}) const {
      return client.get(available_base + "/" + country + "/Local", params);
    }
    [[nodiscard]] json search_toll_free(
        const std::string& country, const std::map<std::string, std::string>& params = {}) const {
      return client.get(available_base + "/" + country + "/TollFree", params);
    }
  };

  // ---------------------------------------------------------------------
  // Additional Compat sub-resources (Python parity).
  //
  // Most extend CrudResource and override update with POST (Twilio-compat
  // semantics). CompatTokens uses BaseResource semantics with PATCH update.
  // CompatAccounts lives at the unscoped /api/laml/.../Accounts path.
  // CompatConferences hangs participants/recordings/streams off itself.
  // ---------------------------------------------------------------------
  struct CompatAccounts {
    const HttpClient& client;
    std::string base_path = "/api/laml/2010-04-01/Accounts";
    CompatAccounts(const HttpClient& c) : client(c) {}
    [[nodiscard]] json list(const std::map<std::string, std::string>& params = {}) const {
      return client.get(base_path, params);
    }
    [[nodiscard]] json create(const json& data) const { return client.post(base_path, data); }
    [[nodiscard]] json get(const std::string& sid) const {
      return client.get(base_path + "/" + sid);
    }
    // Python: POST (Twilio-compat).
    [[nodiscard]] json update(const std::string& sid, const json& data) const {
      return client.post(base_path + "/" + sid, data);
    }
  };

  struct CompatApplications : public CrudResource {
    CompatApplications(const HttpClient& c, const std::string& base) : CrudResource(c, base) {}
    [[nodiscard]] json update(const std::string& sid, const json& data) const {
      return client_.post(base_path_ + "/" + sid, data);
    }
  };

  struct CompatLamlBins : public CrudResource {
    CompatLamlBins(const HttpClient& c, const std::string& base) : CrudResource(c, base) {}
    [[nodiscard]] json update(const std::string& sid, const json& data) const {
      return client_.post(base_path_ + "/" + sid, data);
    }
  };

  struct CompatConferences {
    const HttpClient& client;
    std::string base_path;
    CompatConferences(const HttpClient& c, const std::string& base) : client(c), base_path(base) {}

    [[nodiscard]] json list(const std::map<std::string, std::string>& params = {}) const {
      return client.get(base_path, params);
    }
    [[nodiscard]] json get(const std::string& sid) const {
      return client.get(base_path + "/" + sid);
    }
    [[nodiscard]] json update(const std::string& sid, const json& data) const {
      return client.post(base_path + "/" + sid, data);
    }

    // Participants
    [[nodiscard]] json list_participants(
        const std::string& conference_sid,
        const std::map<std::string, std::string>& params = {}) const {
      return client.get(base_path + "/" + conference_sid + "/Participants", params);
    }
    [[nodiscard]] json get_participant(const std::string& conference_sid,
                                       const std::string& call_sid) const {
      return client.get(base_path + "/" + conference_sid + "/Participants/" + call_sid);
    }
    [[nodiscard]] json update_participant(const std::string& conference_sid,
                                          const std::string& call_sid, const json& data) const {
      return client.post(base_path + "/" + conference_sid + "/Participants/" + call_sid, data);
    }
    [[nodiscard]] json remove_participant(const std::string& conference_sid,
                                          const std::string& call_sid) const {
      return client.del(base_path + "/" + conference_sid + "/Participants/" + call_sid);
    }

    // Conference recordings
    [[nodiscard]] json list_recordings(
        const std::string& conference_sid,
        const std::map<std::string, std::string>& params = {}) const {
      return client.get(base_path + "/" + conference_sid + "/Recordings", params);
    }
    [[nodiscard]] json get_recording(const std::string& conference_sid,
                                     const std::string& recording_sid) const {
      return client.get(base_path + "/" + conference_sid + "/Recordings/" + recording_sid);
    }
    [[nodiscard]] json update_recording(const std::string& conference_sid,
                                        const std::string& recording_sid, const json& data) const {
      return client.post(base_path + "/" + conference_sid + "/Recordings/" + recording_sid, data);
    }
    [[nodiscard]] json delete_recording(const std::string& conference_sid,
                                        const std::string& recording_sid) const {
      return client.del(base_path + "/" + conference_sid + "/Recordings/" + recording_sid);
    }

    // Conference streams
    [[nodiscard]] json start_stream(const std::string& conference_sid, const json& data) const {
      return client.post(base_path + "/" + conference_sid + "/Streams", data);
    }
    [[nodiscard]] json stop_stream(const std::string& conference_sid, const std::string& stream_sid,
                                   const json& data) const {
      return client.post(base_path + "/" + conference_sid + "/Streams/" + stream_sid, data);
    }
  };

  struct CompatQueues : public CrudResource {
    CompatQueues(const HttpClient& c, const std::string& base) : CrudResource(c, base) {}
    [[nodiscard]] json update(const std::string& sid, const json& data) const {
      return client_.post(base_path_ + "/" + sid, data);
    }
    [[nodiscard]] json list_members(const std::string& queue_sid,
                                    const std::map<std::string, std::string>& params = {}) const {
      return client_.get(base_path_ + "/" + queue_sid + "/Members", params);
    }
    [[nodiscard]] json get_member(const std::string& queue_sid, const std::string& call_sid) const {
      return client_.get(base_path_ + "/" + queue_sid + "/Members/" + call_sid);
    }
    [[nodiscard]] json dequeue_member(const std::string& queue_sid, const std::string& call_sid,
                                      const json& data) const {
      return client_.post(base_path_ + "/" + queue_sid + "/Members/" + call_sid, data);
    }
  };

  struct CompatRecordings {
    const HttpClient& client;
    std::string base_path;
    CompatRecordings(const HttpClient& c, const std::string& base) : client(c), base_path(base) {}
    [[nodiscard]] json list(const std::map<std::string, std::string>& params = {}) const {
      return client.get(base_path, params);
    }
    [[nodiscard]] json get(const std::string& sid) const {
      return client.get(base_path + "/" + sid);
    }
    [[nodiscard]] json delete_(const std::string& sid) const {
      return client.del(base_path + "/" + sid);
    }
  };

  struct CompatTranscriptions {
    const HttpClient& client;
    std::string base_path;
    CompatTranscriptions(const HttpClient& c, const std::string& base)
        : client(c), base_path(base) {}
    [[nodiscard]] json list(const std::map<std::string, std::string>& params = {}) const {
      return client.get(base_path, params);
    }
    [[nodiscard]] json get(const std::string& sid) const {
      return client.get(base_path + "/" + sid);
    }
    [[nodiscard]] json delete_(const std::string& sid) const {
      return client.del(base_path + "/" + sid);
    }
  };

  struct CompatTokens {
    const HttpClient& client;
    std::string base_path;
    CompatTokens(const HttpClient& c, const std::string& base) : client(c), base_path(base) {}
    [[nodiscard]] json create(const json& data) const { return client.post(base_path, data); }
    // Python parity: PATCH for update (BaseResource default).
    [[nodiscard]] json update(const std::string& token_id, const json& data) const {
      return client.patch(base_path + "/" + token_id, data);
    }
    [[nodiscard]] json delete_(const std::string& token_id) const {
      return client.del(base_path + "/" + token_id);
    }
  };

  struct CompatNamespace {
    const HttpClient& client;
    std::string account_base;

    // Sub-resources (Python parity).
    CompatAccounts accounts;
    CompatCalls calls;
    CompatMessages messages;
    CompatFaxes faxes;
    CompatConferences conferences;
    CompatPhoneNumbers phone_numbers;
    CompatApplications applications;
    CompatLamlBins laml_bins;
    CompatQueues queues;
    CompatRecordings recordings;
    CompatTranscriptions transcriptions;
    CompatTokens tokens;

    CompatNamespace(const HttpClient& c, const std::string& account_sid)
        : client(c),
          account_base("/api/laml/2010-04-01/Accounts/" + account_sid),
          accounts(c),
          calls(c, account_base + "/Calls"),
          messages(c, account_base + "/Messages"),
          faxes(c, account_base + "/Faxes"),
          conferences(c, account_base + "/Conferences"),
          phone_numbers(c, account_base),
          applications(c, account_base + "/Applications"),
          laml_bins(c, account_base + "/LamlBins"),
          queues(c, account_base + "/Queues"),
          recordings(c, account_base + "/Recordings"),
          transcriptions(c, account_base + "/Transcriptions"),
          tokens(c, account_base + "/tokens") {}

    // Legacy convenience methods retained for backwards compatibility
    // (the existing rest_compat_laml.cpp example references these).
    // The paths are intentionally not project-scoped to avoid changing
    // the wire contract any callers may rely on.
    [[nodiscard]] json create_call(const json& data) const {
      return client.post("/api/laml/2010-04-01/Accounts/calls", data);
    }
    [[nodiscard]] json send_message(const json& data) const {
      return client.post("/api/laml/2010-04-01/Accounts/messages", data);
    }
    [[nodiscard]] json list_calls(const std::map<std::string, std::string>& p = {}) const {
      return client.get("/api/laml/2010-04-01/Accounts/calls", p);
    }
    [[nodiscard]] json list_messages(const std::map<std::string, std::string>& p = {}) const {
      return client.get("/api/laml/2010-04-01/Accounts/messages", p);
    }
  };

  // ---------------------------------------------------------------------
  // Simple CRUD namespaces (Python parity additions). The existing CRUD
  // surface from CrudResource is preserved; we just augment with the
  // ``list / get / create / delete / update`` methods that Python exposes
  // with non-standard verbs (``addresses`` skips update; ``short_codes``
  // uses PUT; etc.) and add the sub-resource accessors required by the
  // mock-server tests.
  // ---------------------------------------------------------------------
  struct AddressesNamespace : public CrudResource {
    AddressesNamespace(const HttpClient& c) : CrudResource(c, "/api/relay/rest/addresses") {}
    // Python-parity ``delete`` alias (CrudResource exposes ``del``).
    [[nodiscard]] json delete_(const std::string& id) const {
      return client_.del(base_path_ + "/" + id);
    }
  };

  struct QueuesNamespace : public CrudResource {
    QueuesNamespace(const HttpClient& c) : CrudResource(c, "/api/relay/rest/queues") {}
    // Override update -- Python uses PUT, not PATCH (the CrudResource default).
    [[nodiscard]] json update(const std::string& id, const json& data) const {
      return client_.put(base_path_ + "/" + id, data);
    }
    [[nodiscard]] json list_members(const std::string& queue_id,
                                    const std::map<std::string, std::string>& params = {}) const {
      return client_.get(base_path_ + "/" + queue_id + "/members", params);
    }
    [[nodiscard]] json get_next_member(const std::string& queue_id) const {
      return client_.get(base_path_ + "/" + queue_id + "/members/next");
    }
    [[nodiscard]] json get_member(const std::string& queue_id, const std::string& member_id) const {
      return client_.get(base_path_ + "/" + queue_id + "/members/" + member_id);
    }
  };

  struct RecordingsNamespace : public CrudResource {
    RecordingsNamespace(const HttpClient& c) : CrudResource(c, "/api/relay/rest/recordings") {}
    // Python ``delete`` alias.
    [[nodiscard]] json delete_(const std::string& id) const {
      return client_.del(base_path_ + "/" + id);
    }
  };

  struct NumberGroupsNamespace : public CrudResource {
    NumberGroupsNamespace(const HttpClient& c) : CrudResource(c, "/api/relay/rest/number_groups") {}
    // Python NumberGroups uses PUT for update (overrides default PATCH).
    [[nodiscard]] json update(const std::string& id, const json& data) const {
      return client_.put(base_path_ + "/" + id, data);
    }
    // Membership operations.
    [[nodiscard]] json list_memberships(
        const std::string& group_id, const std::map<std::string, std::string>& params = {}) const {
      return client_.get(base_path_ + "/" + group_id + "/number_group_memberships", params);
    }
    [[nodiscard]] json add_membership(const std::string& group_id, const json& data) const {
      return client_.post(base_path_ + "/" + group_id + "/number_group_memberships", data);
    }
    [[nodiscard]] json get_membership(const std::string& membership_id) const {
      return client_.get("/api/relay/rest/number_group_memberships/" + membership_id);
    }
    [[nodiscard]] json delete_membership(const std::string& membership_id) const {
      return client_.del("/api/relay/rest/number_group_memberships/" + membership_id);
    }
  };

  struct VerifiedCallersNamespace : public CrudResource {
    VerifiedCallersNamespace(const HttpClient& c)
        : CrudResource(c, "/api/relay/rest/verified_callers") {}
  };

  // SipProfile is a singleton (no resource id). We expose ``get`` /
  // ``update`` directly on the namespace (PUT for update -- Python parity).
  struct SipProfileNamespace : public CrudResource {
    SipProfileNamespace(const HttpClient& c) : CrudResource(c, "/api/relay/rest/sip_profile") {}
    [[nodiscard]] json get() const { return client_.get(base_path_); }
    [[nodiscard]] json update(const json& data) const { return client_.put(base_path_, data); }
  };

  struct LookupNamespace {
    const HttpClient& client;
    LookupNamespace(const HttpClient& c) : client(c) {}
    [[nodiscard]] json lookup(const std::string& number) const {
      return client.get("/api/relay/rest/lookup/" + number);
    }
  };

  struct ShortCodesNamespace : public CrudResource {
    ShortCodesNamespace(const HttpClient& c) : CrudResource(c, "/api/relay/rest/short_codes") {}
    // Python ShortCodes.update uses PUT explicitly.
    [[nodiscard]] json update(const std::string& id, const json& data) const {
      return client_.put(base_path_ + "/" + id, data);
    }
  };

  // The Python imported_numbers namespace lives at
  // /api/relay/rest/imported_phone_numbers and only exposes ``create``.
  // We retain the legacy /api/relay/rest/imported_numbers path for callers
  // who hit it via list/get/update/delete and add the Python-parity
  // ``create`` that posts to the imported_phone_numbers endpoint.
  struct ImportedNumbersNamespace : public CrudResource {
    ImportedNumbersNamespace(const HttpClient& c)
        : CrudResource(c, "/api/relay/rest/imported_numbers") {}
    // Python parity: POST /api/relay/rest/imported_phone_numbers.
    [[nodiscard]] json create(const json& data) const {
      return client_.post("/api/relay/rest/imported_phone_numbers", data);
    }
  };

  struct MFANamespace {
    const HttpClient& client;
    MFANamespace(const HttpClient& c) : client(c) {}
    // Python parity:
    [[nodiscard]] json sms(const json& data) const {
      return client.post("/api/relay/rest/mfa/sms", data);
    }
    [[nodiscard]] json call(const json& data) const {
      return client.post("/api/relay/rest/mfa/call", data);
    }
    [[nodiscard]] json verify(const std::string& request_id, const json& data) const {
      return client.post("/api/relay/rest/mfa/" + request_id + "/verify", data);
    }
    // Legacy convenience accessors.
    [[nodiscard]] json request_code(const json& data) const {
      return client.post("/api/mfa/request", data);
    }
    [[nodiscard]] json verify_code(const json& data) const {
      return client.post("/api/mfa/verify", data);
    }
  };

  // ---------------------------------------------------------------------
  // Registry (10DLC) sub-resources -- Python parity.
  // All sit under /api/relay/rest/registry/beta.
  // ---------------------------------------------------------------------
  struct RegistryBrands {
    const HttpClient& client;
    std::string base_path = "/api/relay/rest/registry/beta/brands";
    RegistryBrands(const HttpClient& c) : client(c) {}
    [[nodiscard]] json list(const std::map<std::string, std::string>& params = {}) const {
      return client.get(base_path, params);
    }
    [[nodiscard]] json create(const json& data) const { return client.post(base_path, data); }
    [[nodiscard]] json get(const std::string& brand_id) const {
      return client.get(base_path + "/" + brand_id);
    }
    [[nodiscard]] json list_campaigns(const std::string& brand_id,
                                      const std::map<std::string, std::string>& params = {}) const {
      return client.get(base_path + "/" + brand_id + "/campaigns", params);
    }
    [[nodiscard]] json create_campaign(const std::string& brand_id, const json& data) const {
      return client.post(base_path + "/" + brand_id + "/campaigns", data);
    }
  };

  struct RegistryCampaigns {
    const HttpClient& client;
    std::string base_path = "/api/relay/rest/registry/beta/campaigns";
    RegistryCampaigns(const HttpClient& c) : client(c) {}
    [[nodiscard]] json get(const std::string& campaign_id) const {
      return client.get(base_path + "/" + campaign_id);
    }
    // Python parity: PUT (CrudResource default would be PATCH).
    [[nodiscard]] json update(const std::string& campaign_id, const json& data) const {
      return client.put(base_path + "/" + campaign_id, data);
    }
    [[nodiscard]] json list_numbers(const std::string& campaign_id,
                                    const std::map<std::string, std::string>& params = {}) const {
      return client.get(base_path + "/" + campaign_id + "/numbers", params);
    }
    [[nodiscard]] json list_orders(const std::string& campaign_id,
                                   const std::map<std::string, std::string>& params = {}) const {
      return client.get(base_path + "/" + campaign_id + "/orders", params);
    }
    [[nodiscard]] json create_order(const std::string& campaign_id, const json& data) const {
      return client.post(base_path + "/" + campaign_id + "/orders", data);
    }
  };

  struct RegistryOrders {
    const HttpClient& client;
    std::string base_path = "/api/relay/rest/registry/beta/orders";
    RegistryOrders(const HttpClient& c) : client(c) {}
    [[nodiscard]] json get(const std::string& order_id) const {
      return client.get(base_path + "/" + order_id);
    }
  };

  struct RegistryNumbers {
    const HttpClient& client;
    std::string base_path = "/api/relay/rest/registry/beta/numbers";
    RegistryNumbers(const HttpClient& c) : client(c) {}
    [[nodiscard]] json delete_(const std::string& number_id) const {
      return client.del(base_path + "/" + number_id);
    }
  };

  struct RegistryNamespace {
    RegistryBrands brands;
    RegistryCampaigns campaigns;
    RegistryOrders orders;
    RegistryNumbers numbers;
    RegistryNamespace(const HttpClient& c) : brands(c), campaigns(c), orders(c), numbers(c) {}
  };

  // ---------------------------------------------------------------------
  // Logs sub-resources -- Python parity.
  //
  // The four log kinds live at four different sub-APIs:
  //   /api/messaging/logs   /api/voice/logs
  //   /api/fax/logs         /api/logs/conferences
  // Each is a small read-only resource (list / get / optional events).
  // ---------------------------------------------------------------------
  struct LogsMessages {
    const HttpClient& client;
    std::string base_path = "/api/messaging/logs";
    LogsMessages(const HttpClient& c) : client(c) {}
    [[nodiscard]] json list(const std::map<std::string, std::string>& params = {}) const {
      return client.get(base_path, params);
    }
    [[nodiscard]] json get(const std::string& log_id) const {
      return client.get(base_path + "/" + log_id);
    }
  };

  struct LogsVoice {
    const HttpClient& client;
    std::string base_path = "/api/voice/logs";
    LogsVoice(const HttpClient& c) : client(c) {}
    [[nodiscard]] json list(const std::map<std::string, std::string>& params = {}) const {
      return client.get(base_path, params);
    }
    [[nodiscard]] json get(const std::string& log_id) const {
      return client.get(base_path + "/" + log_id);
    }
    [[nodiscard]] json list_events(const std::string& log_id,
                                   const std::map<std::string, std::string>& params = {}) const {
      return client.get(base_path + "/" + log_id + "/events", params);
    }
  };

  struct LogsFax {
    const HttpClient& client;
    std::string base_path = "/api/fax/logs";
    LogsFax(const HttpClient& c) : client(c) {}
    [[nodiscard]] json list(const std::map<std::string, std::string>& params = {}) const {
      return client.get(base_path, params);
    }
    [[nodiscard]] json get(const std::string& log_id) const {
      return client.get(base_path + "/" + log_id);
    }
  };

  struct LogsConferences {
    const HttpClient& client;
    std::string base_path = "/api/logs/conferences";
    LogsConferences(const HttpClient& c) : client(c) {}
    [[nodiscard]] json list(const std::map<std::string, std::string>& params = {}) const {
      return client.get(base_path, params);
    }
  };

  struct LogsNamespace {
    LogsMessages messages;
    LogsVoice voice;
    LogsFax fax;
    LogsConferences conferences;
    LogsNamespace(const HttpClient& c) : messages(c), voice(c), fax(c), conferences(c) {}
  };

  // Project tokens (Python: client.project.tokens.{create,update,delete}).
  struct ProjectTokens {
    const HttpClient& client;
    const std::string base_path = "/api/project/tokens";
    ProjectTokens(const HttpClient& c) : client(c) {}
    [[nodiscard]] json create(const json& data) const { return client.post(base_path, data); }
    [[nodiscard]] json update(const std::string& token_id, const json& data) const {
      return client.patch(base_path + "/" + token_id, data);
    }
    [[nodiscard]] json delete_(const std::string& token_id) const {
      return client.del(base_path + "/" + token_id);
    }
  };

  struct ProjectNamespace {
    const HttpClient& client;
    ProjectTokens tokens;
    ProjectNamespace(const HttpClient& c) : client(c), tokens(c) {}
    // Legacy direct accessors -- these don't have Python equivalents
    // and remain for backwards compatibility with existing tests.
    [[nodiscard]] json get_project() const { return client.get("/api/relay/rest/project"); }
    [[nodiscard]] json update_project(const json& data) const {
      return client.put("/api/relay/rest/project", data);
    }
  };

  struct PubSubNamespace {
    const HttpClient& client;
    PubSubNamespace(const HttpClient& c) : client(c) {}
    [[nodiscard]] json publish(const json& data) const {
      return client.post("/api/pubsub/publish", data);
    }
  };

  struct ChatNamespace {
    const HttpClient& client;
    ChatNamespace(const HttpClient& c) : client(c) {}
    [[nodiscard]] json send_message(const json& data) const {
      return client.post("/api/chat/messages", data);
    }
    [[nodiscard]] json list_messages(const std::map<std::string, std::string>& p = {}) const {
      return client.get("/api/chat/messages", p);
    }
  };

  // ========================================================================
  // Namespace accessors
  // ========================================================================

  FabricNamespace& fabric() { return *fabric_; }
  CallingNamespace& calling() { return *calling_; }
  PhoneNumbersNamespace& phone_numbers() { return *phone_numbers_; }
  DatasphereNamespace& datasphere() { return *datasphere_; }
  VideoNamespace& video() { return *video_; }
  CompatNamespace& compat() { return *compat_; }
  AddressesNamespace& addresses() { return *addresses_; }
  QueuesNamespace& queues() { return *queues_; }
  RecordingsNamespace& recordings() { return *recordings_; }
  NumberGroupsNamespace& number_groups() { return *number_groups_; }
  VerifiedCallersNamespace& verified_callers() { return *verified_callers_; }
  SipProfileNamespace& sip_profile() { return *sip_profile_; }
  LookupNamespace& lookup() { return *lookup_; }
  ShortCodesNamespace& short_codes() { return *short_codes_; }
  ImportedNumbersNamespace& imported_numbers() { return *imported_numbers_; }
  MFANamespace& mfa() { return *mfa_; }
  RegistryNamespace& registry() { return *registry_; }
  LogsNamespace& logs() { return *logs_; }
  ProjectNamespace& project() { return *project_; }
  PubSubNamespace& pubsub() { return *pubsub_; }
  ChatNamespace& chat() { return *chat_; }

  /// Get the underlying HTTP client
  const HttpClient& http_client() const { return *client_; }

 private:
  /// Helper used by both constructors to wire all 21 namespaces against
  /// the (already-built) HttpClient.
  void init_namespaces();

  std::string project_id_;

  std::unique_ptr<HttpClient> client_;

  std::unique_ptr<FabricNamespace> fabric_;
  std::unique_ptr<CallingNamespace> calling_;
  std::unique_ptr<PhoneNumbersNamespace> phone_numbers_;
  std::unique_ptr<DatasphereNamespace> datasphere_;
  std::unique_ptr<VideoNamespace> video_;
  std::unique_ptr<CompatNamespace> compat_;
  std::unique_ptr<AddressesNamespace> addresses_;
  std::unique_ptr<QueuesNamespace> queues_;
  std::unique_ptr<RecordingsNamespace> recordings_;
  std::unique_ptr<NumberGroupsNamespace> number_groups_;
  std::unique_ptr<VerifiedCallersNamespace> verified_callers_;
  std::unique_ptr<SipProfileNamespace> sip_profile_;
  std::unique_ptr<LookupNamespace> lookup_;
  std::unique_ptr<ShortCodesNamespace> short_codes_;
  std::unique_ptr<ImportedNumbersNamespace> imported_numbers_;
  std::unique_ptr<MFANamespace> mfa_;
  std::unique_ptr<RegistryNamespace> registry_;
  std::unique_ptr<LogsNamespace> logs_;
  std::unique_ptr<ProjectNamespace> project_;
  std::unique_ptr<PubSubNamespace> pubsub_;
  std::unique_ptr<ChatNamespace> chat_;
};

}  // namespace rest
}  // namespace signalwire
