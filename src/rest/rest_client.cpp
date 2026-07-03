// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#include "signalwire/rest/rest_client.hpp"

#include "signalwire/common.hpp"

namespace signalwire {
namespace rest {

RestClient::RestClient(const std::string& space, const std::string& project_id,
                       const std::string& token)
    : project_id_(project_id) {
  std::string base_url = "https://" + space;
  client_ = std::make_unique<HttpClient>(base_url, project_id, token);
  init_tree();
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

RestClient RestClient::with_base_url(const std::string& base_url, const std::string& project_id,
                                     const std::string& token) {
  // Default-construct a placeholder, then re-wire with the pre-built
  // base URL. We deliberately don't add a public ctor that takes a
  // base URL — production callers should use the space form.
  RestClient rc("placeholder", project_id, token);
  rc.client_ = std::make_unique<HttpClient>(base_url, project_id, token);
  rc.project_id_ = project_id;
  rc.init_tree();
  return rc;
}

}  // namespace rest
}  // namespace signalwire
