// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#pragma once

#include <string>
#include <memory>
#include <optional>
#include <nlohmann/json.hpp>
#include "signalwire/rest/http_client.hpp"
#include "signalwire/rest/phone_call_handler.hpp"

namespace signalwire {
namespace rest {

using json = nlohmann::json;

/// Top-level SignalWire REST client with all API namespaces
class RestClient {
public:
    RestClient(const std::string& space,
                     const std::string& project_id,
                     const std::string& token);

    /// Initialize from environment variables
    static RestClient from_env();

    /// Construct with an explicit pre-built base URL (`http://...` or
    /// `https://...`) instead of synthesizing one from the SignalWire
    /// space hostname. Used by audit harnesses pointing the client at
    /// loopback fixtures. The space-based constructor remains the
    /// production path.
    static RestClient with_base_url(const std::string& base_url,
                                     const std::string& project_id,
                                     const std::string& token);

    /// Project ID accessor (read-only).
    const std::string& project_id() const { return project_id_; }

    // ========================================================================
    // API Namespaces (all 21)
    // ========================================================================

    struct FabricNamespace {
        CrudResource subscribers;
        CrudResource addresses;
        CrudResource sip_endpoints;
        CrudResource call_flows;
        CrudResource swml_scripts;
        CrudResource conferences;
        CrudResource resources;
        CrudResource tokens;
        CrudResource routing;
        CrudResource agents;
        CrudResource domains;
        CrudResource topics;
        CrudResource webhooks;

        FabricNamespace(const HttpClient& c)
            : subscribers(c, "/api/fabric/subscribers"),
              addresses(c, "/api/fabric/addresses"),
              sip_endpoints(c, "/api/fabric/sip_endpoints"),
              call_flows(c, "/api/fabric/call_flows"),
              swml_scripts(c, "/api/fabric/swml_scripts"),
              conferences(c, "/api/fabric/conferences"),
              resources(c, "/api/fabric/resources"),
              tokens(c, "/api/fabric/tokens"),
              routing(c, "/api/fabric/routing"),
              agents(c, "/api/fabric/agents"),
              domains(c, "/api/fabric/domains"),
              topics(c, "/api/fabric/topics"),
              webhooks(c, "/api/fabric/webhooks") {}
    };

    struct CallingNamespace {
        const HttpClient& client;

        CallingNamespace(const HttpClient& c) : client(c) {}

        json dial(const json& params) const { return client.post("/api/calling/calls", params); }
        json list_calls(const std::map<std::string, std::string>& p = {}) const { return client.get("/api/calling/calls", p); }
        json get_call(const std::string& id) const { return client.get("/api/calling/calls/" + id); }
        json update_call(const std::string& id, const json& data) const { return client.put("/api/calling/calls/" + id, data); }
        json end_call(const std::string& id) const { return client.del("/api/calling/calls/" + id); }
        json play(const std::string& id, const json& data) const { return client.post("/api/calling/calls/" + id + "/play", data); }
        json record(const std::string& id, const json& data) const { return client.post("/api/calling/calls/" + id + "/record", data); }
        json collect(const std::string& id, const json& data) const { return client.post("/api/calling/calls/" + id + "/collect", data); }
        json tap(const std::string& id, const json& data) const { return client.post("/api/calling/calls/" + id + "/tap", data); }
        json detect(const std::string& id, const json& data) const { return client.post("/api/calling/calls/" + id + "/detect", data); }
        json connect(const std::string& id, const json& data) const { return client.post("/api/calling/calls/" + id + "/connect", data); }
        json send_digits(const std::string& id, const json& data) const { return client.post("/api/calling/calls/" + id + "/send_digits", data); }
        json transfer(const std::string& id, const json& data) const { return client.post("/api/calling/calls/" + id + "/transfer", data); }
        json answer(const std::string& id) const { return client.post("/api/calling/calls/" + id + "/answer"); }
        json hangup(const std::string& id) const { return client.post("/api/calling/calls/" + id + "/hangup"); }
        json hold(const std::string& id) const { return client.post("/api/calling/calls/" + id + "/hold"); }
        json unhold(const std::string& id) const { return client.post("/api/calling/calls/" + id + "/unhold"); }
    };

    struct PhoneNumbersNamespace : public CrudResource {
        PhoneNumbersNamespace(const HttpClient& c) : CrudResource(c, "/api/relay/rest/phone_numbers") {}
        json search(const std::map<std::string, std::string>& p) const { return client_.get(base_path_ + "/search", p); }
        json buy(const json& data) const { return client_.post(base_path_ + "/buy", data); }
        json release(const std::string& id) const { return client_.del(base_path_ + "/" + id); }

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

        static json make_swml_webhook_body(const std::string& url) {
            return {
                {"call_handler", to_wire_string(PhoneCallHandler::RelayScript)},
                {"call_relay_script_url", url},
            };
        }

        static json make_cxml_webhook_body(const std::string& url,
                                           const CxmlWebhookOptions& opts = {}) {
            json body = {
                {"call_handler", to_wire_string(PhoneCallHandler::LamlWebhooks)},
                {"call_request_url", url},
            };
            if (opts.fallback_url) body["call_fallback_url"] = *opts.fallback_url;
            if (opts.status_callback_url) body["call_status_callback_url"] = *opts.status_callback_url;
            return body;
        }

        static json make_cxml_application_body(const std::string& application_id) {
            return {
                {"call_handler", to_wire_string(PhoneCallHandler::LamlApplication)},
                {"call_laml_application_id", application_id},
            };
        }

        static json make_ai_agent_body(const std::string& agent_id) {
            return {
                {"call_handler", to_wire_string(PhoneCallHandler::AiAgent)},
                {"call_ai_agent_id", agent_id},
            };
        }

        static json make_call_flow_body(const std::string& flow_id,
                                        const CallFlowOptions& opts = {}) {
            json body = {
                {"call_handler", to_wire_string(PhoneCallHandler::CallFlow)},
                {"call_flow_id", flow_id},
            };
            if (opts.version) body["call_flow_version"] = *opts.version;
            return body;
        }

        static json make_relay_application_body(const std::string& name) {
            return {
                {"call_handler", to_wire_string(PhoneCallHandler::RelayApplication)},
                {"call_relay_application", name},
            };
        }

        static json make_relay_topic_body(const std::string& topic,
                                          const RelayTopicOptions& opts = {}) {
            json body = {
                {"call_handler", to_wire_string(PhoneCallHandler::RelayTopic)},
                {"call_relay_topic", topic},
            };
            if (opts.status_callback_url) body["call_relay_topic_status_callback_url"] = *opts.status_callback_url;
            return body;
        }

        // -- Typed helpers ---------------------------------------------------

        /// Route inbound calls to an SWML webhook URL.
        /// Server auto-creates a ``swml_webhook`` Fabric resource keyed off
        /// this URL.
        json set_swml_webhook(const std::string& resource_id, const std::string& url) const {
            return update(resource_id, make_swml_webhook_body(url));
        }

        /// Route inbound calls to a cXML (Twilio-compat / LAML) webhook.
        /// Despite the wire value ``laml_webhooks`` being plural, this creates
        /// a single ``cxml_webhook`` Fabric resource. Extra options populate
        /// fallback and status-callback fields.
        json set_cxml_webhook(const std::string& resource_id,
                              const std::string& url,
                              const CxmlWebhookOptions& opts = {}) const {
            return update(resource_id, make_cxml_webhook_body(url, opts));
        }

        /// Route inbound calls to an existing cXML application by ID.
        json set_cxml_application(const std::string& resource_id,
                                  const std::string& application_id) const {
            return update(resource_id, make_cxml_application_body(application_id));
        }

        /// Route inbound calls to an AI Agent Fabric resource by ID.
        json set_ai_agent(const std::string& resource_id,
                          const std::string& agent_id) const {
            return update(resource_id, make_ai_agent_body(agent_id));
        }

        /// Route inbound calls to a Call Flow by ID.
        /// ``opts.version`` accepts ``"working_copy"`` or ``"current_deployed"``.
        json set_call_flow(const std::string& resource_id,
                           const std::string& flow_id,
                           const CallFlowOptions& opts = {}) const {
            return update(resource_id, make_call_flow_body(flow_id, opts));
        }

        /// Route inbound calls to a named RELAY application.
        json set_relay_application(const std::string& resource_id,
                                   const std::string& name) const {
            return update(resource_id, make_relay_application_body(name));
        }

        /// Route inbound calls to a RELAY topic (client subscription).
        json set_relay_topic(const std::string& resource_id,
                             const std::string& topic,
                             const RelayTopicOptions& opts = {}) const {
            return update(resource_id, make_relay_topic_body(topic, opts));
        }
    };

    struct DatasphereNamespace {
        CrudResource documents;
        const HttpClient& client;

        DatasphereNamespace(const HttpClient& c)
            : documents(c, "/api/datasphere/documents"), client(c) {}

        json search(const json& data) const {
            return client.post("/api/datasphere/documents/search", data);
        }
    };

    struct VideoNamespace {
        CrudResource rooms;
        CrudResource room_sessions;
        CrudResource recordings;

        VideoNamespace(const HttpClient& c)
            : rooms(c, "/api/video/rooms"),
              room_sessions(c, "/api/video/room_sessions"),
              recordings(c, "/api/video/recordings") {}
    };

    struct CompatNamespace {
        const HttpClient& client;

        CompatNamespace(const HttpClient& c) : client(c) {}

        json create_call(const json& data) const { return client.post("/api/laml/2010-04-01/Accounts/calls", data); }
        json send_message(const json& data) const { return client.post("/api/laml/2010-04-01/Accounts/messages", data); }
        json list_calls(const std::map<std::string, std::string>& p = {}) const { return client.get("/api/laml/2010-04-01/Accounts/calls", p); }
        json list_messages(const std::map<std::string, std::string>& p = {}) const { return client.get("/api/laml/2010-04-01/Accounts/messages", p); }
    };

    // Simple CRUD namespaces
    struct AddressesNamespace : public CrudResource {
        AddressesNamespace(const HttpClient& c) : CrudResource(c, "/api/relay/rest/addresses") {}
    };

    struct QueuesNamespace : public CrudResource {
        QueuesNamespace(const HttpClient& c) : CrudResource(c, "/api/relay/rest/queues") {}
    };

    struct RecordingsNamespace : public CrudResource {
        RecordingsNamespace(const HttpClient& c) : CrudResource(c, "/api/relay/rest/recordings") {}
    };

    struct NumberGroupsNamespace : public CrudResource {
        NumberGroupsNamespace(const HttpClient& c) : CrudResource(c, "/api/relay/rest/number_groups") {}
    };

    struct VerifiedCallersNamespace : public CrudResource {
        VerifiedCallersNamespace(const HttpClient& c) : CrudResource(c, "/api/relay/rest/verified_callers") {}
    };

    struct SipProfileNamespace : public CrudResource {
        SipProfileNamespace(const HttpClient& c) : CrudResource(c, "/api/relay/rest/sip_profile") {}
    };

    struct LookupNamespace {
        const HttpClient& client;
        LookupNamespace(const HttpClient& c) : client(c) {}
        json lookup(const std::string& number) const { return client.get("/api/relay/rest/lookup/" + number); }
    };

    struct ShortCodesNamespace : public CrudResource {
        ShortCodesNamespace(const HttpClient& c) : CrudResource(c, "/api/relay/rest/short_codes") {}
    };

    struct ImportedNumbersNamespace : public CrudResource {
        ImportedNumbersNamespace(const HttpClient& c) : CrudResource(c, "/api/relay/rest/imported_numbers") {}
    };

    struct MFANamespace {
        const HttpClient& client;
        MFANamespace(const HttpClient& c) : client(c) {}
        json request_code(const json& data) const { return client.post("/api/mfa/request", data); }
        json verify_code(const json& data) const { return client.post("/api/mfa/verify", data); }
    };

    struct RegistryNamespace : public CrudResource {
        RegistryNamespace(const HttpClient& c) : CrudResource(c, "/api/relay/rest/registry") {}
    };

    struct LogsNamespace : public CrudResource {
        LogsNamespace(const HttpClient& c) : CrudResource(c, "/api/relay/rest/logs") {}
    };

    struct ProjectNamespace {
        const HttpClient& client;
        ProjectNamespace(const HttpClient& c) : client(c) {}
        json get_project() const { return client.get("/api/relay/rest/project"); }
        json update_project(const json& data) const { return client.put("/api/relay/rest/project", data); }
    };

    struct PubSubNamespace {
        const HttpClient& client;
        PubSubNamespace(const HttpClient& c) : client(c) {}
        json publish(const json& data) const { return client.post("/api/pubsub/publish", data); }
    };

    struct ChatNamespace {
        const HttpClient& client;
        ChatNamespace(const HttpClient& c) : client(c) {}
        json send_message(const json& data) const { return client.post("/api/chat/messages", data); }
        json list_messages(const std::map<std::string, std::string>& p = {}) const { return client.get("/api/chat/messages", p); }
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

} // namespace rest
} // namespace signalwire
