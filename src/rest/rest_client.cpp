// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#include "signalwire/rest/rest_client.hpp"

#include "signalwire/common.hpp"

namespace signalwire {
namespace rest {

RestClient::RestClient(const std::string& space, const std::string& project_id,
                       const std::string& token, const RequestOptions& request_options,
                       const std::string& personal_access_token)
    : project_id_(project_id) {
  wire_clients("https://" + space, project_id, token, request_options, personal_access_token);
  init_tree();
}

void RestClient::wire_clients(const std::string& base_url, const std::string& project_id,
                              const std::string& token, const RequestOptions& request_options,
                              const std::string& personal_access_token) {
  client_ = std::make_unique<HttpClient>(base_url, project_id, token, request_options);
  // A PAT-only client has no project credential: its project-scoped resources raise
  // instead of sending an unauthenticated request.
  if ((project_id.empty() || token.empty()) && !personal_access_token.empty()) {
    client_->missing_credential_ =
        "project and token are required for this resource (SIGNALWIRE_PROJECT_ID / "
        "SIGNALWIRE_API_TOKEN); this client has only a personal access token, which "
        "authenticates client.space";
  }
  // A Personal Access Token is HTTP Basic with an EMPTY username
  // (prime-rails API::Space::BaseController -> Authenticators::PersonalAccessToken).
  pat_client_ = std::make_unique<HttpClient>(base_url, "", personal_access_token, request_options);
  if (personal_access_token.empty()) {
    pat_client_->missing_credential_ =
        "personal_access_token is required for client.space (SIGNALWIRE_PERSONAL_ACCESS_TOKEN)";
  }
}

RestClient RestClient::from_env() {
  std::string space = get_env("SIGNALWIRE_SPACE");
  std::string project = get_env("SIGNALWIRE_PROJECT_ID");
  std::string token = get_env("SIGNALWIRE_API_TOKEN");
  std::string pat = get_env("SIGNALWIRE_PERSONAL_ACCESS_TOKEN");

  if (space.empty() || ((project.empty() || token.empty()) && pat.empty())) {
    throw std::runtime_error(
        "Missing required env vars: SIGNALWIRE_SPACE, SIGNALWIRE_PROJECT_ID, SIGNALWIRE_API_TOKEN "
        "(or, for client.space only, SIGNALWIRE_SPACE and SIGNALWIRE_PERSONAL_ACCESS_TOKEN)");
  }

  return RestClient(space, project, token, {}, pat);
}

RestClient RestClient::with_base_url(const std::string& base_url, const std::string& project_id,
                                     const std::string& token,
                                     const RequestOptions& request_options,
                                     const std::string& personal_access_token) {
  // Default-construct a placeholder, then re-wire with the pre-built
  // base URL. We deliberately don't add a public ctor that takes a
  // base URL — production callers should use the space form.
  RestClient rc("placeholder", project_id, token);
  rc.wire_clients(base_url, project_id, token, request_options, personal_access_token);
  rc.project_id_ = project_id;
  rc.init_tree();
  return rc;
}

}  // namespace rest
}  // namespace signalwire
