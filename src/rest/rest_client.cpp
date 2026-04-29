// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#include "signalwire/rest/rest_client.hpp"
#include "signalwire/common.hpp"

namespace signalwire {
namespace rest {

RestClient::RestClient(const std::string& space,
                                   const std::string& project_id,
                                   const std::string& token)
    : project_id_(project_id) {
    std::string base_url = "https://" + space;
    client_ = std::make_unique<HttpClient>(base_url, project_id, token);
    init_namespaces();
}

RestClient RestClient::from_env() {
    std::string space = get_env("SIGNALWIRE_SPACE");
    std::string project = get_env("SIGNALWIRE_PROJECT_ID");
    std::string token = get_env("SIGNALWIRE_API_TOKEN");

    if (space.empty() || project.empty() || token.empty()) {
        throw std::runtime_error(
            "Missing required env vars: SIGNALWIRE_SPACE, SIGNALWIRE_PROJECT_ID, SIGNALWIRE_API_TOKEN");
    }

    return RestClient(space, project, token);
}

RestClient RestClient::with_base_url(const std::string& base_url,
                                       const std::string& project_id,
                                       const std::string& token) {
    // Default-construct a placeholder, then re-wire with the pre-built
    // base URL. We deliberately don't add a public ctor that takes a
    // base URL — production callers should use the space form.
    RestClient rc("placeholder", project_id, token);
    rc.client_ = std::make_unique<HttpClient>(base_url, project_id, token);
    rc.project_id_ = project_id;
    rc.init_namespaces();
    return rc;
}

void RestClient::init_namespaces() {
    fabric_ = std::make_unique<FabricNamespace>(*client_);
    calling_ = std::make_unique<CallingNamespace>(*client_);
    phone_numbers_ = std::make_unique<PhoneNumbersNamespace>(*client_);
    datasphere_ = std::make_unique<DatasphereNamespace>(*client_);
    video_ = std::make_unique<VideoNamespace>(*client_);
    compat_ = std::make_unique<CompatNamespace>(*client_);
    addresses_ = std::make_unique<AddressesNamespace>(*client_);
    queues_ = std::make_unique<QueuesNamespace>(*client_);
    recordings_ = std::make_unique<RecordingsNamespace>(*client_);
    number_groups_ = std::make_unique<NumberGroupsNamespace>(*client_);
    verified_callers_ = std::make_unique<VerifiedCallersNamespace>(*client_);
    sip_profile_ = std::make_unique<SipProfileNamespace>(*client_);
    lookup_ = std::make_unique<LookupNamespace>(*client_);
    short_codes_ = std::make_unique<ShortCodesNamespace>(*client_);
    imported_numbers_ = std::make_unique<ImportedNumbersNamespace>(*client_);
    mfa_ = std::make_unique<MFANamespace>(*client_);
    registry_ = std::make_unique<RegistryNamespace>(*client_);
    logs_ = std::make_unique<LogsNamespace>(*client_);
    project_ = std::make_unique<ProjectNamespace>(*client_);
    pubsub_ = std::make_unique<PubSubNamespace>(*client_);
    chat_ = std::make_unique<ChatNamespace>(*client_);
}

} // namespace rest
} // namespace signalwire
