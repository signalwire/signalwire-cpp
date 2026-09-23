// Copyright (c) 2025 SignalWire
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#pragma once

#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>

namespace signalwire {
namespace agent {
class AgentBase;
}  // namespace agent

namespace utils {

using json = nlohmann::json;

/**
 * Access shim letting the serverless dispatchers reach `AgentBase`'s protected
 * `swaig_validate_token` core. Each envelope (lambda / cgi / gcf / azure)
 * extracts the credential from its own payload shape and then routes the
 * DECISION through that single shared core, so a serverless deployment enforces
 * `secure` exactly as the HTTP endpoint does. Declared here (and befriended by
 * `AgentBase`) so the enforcement lives in one place instead of being
 * re-implemented per transport.
 */
struct ServerlessTokenAccess;

/**
 * Whether the SDK is running inside a short-lived / event-driven invocation
 * environment, as opposed to a long-lived server process. Determined from
 * `core::logging_config::get_execution_mode()`.
 *
 * @return `true` unless the detected mode is `"server"`.
 */
[[nodiscard]] bool is_serverless_mode();

/**
 * A platform-neutral serverless response: the `(status, headers, body)` shape
 * every dispatch handler produces. Lambda/Azure return this as a struct; GCF /
 * CGI additionally emit it to stdout when serving live.
 */
struct ServerlessResponse {
  int status = 200;
  std::map<std::string, std::string> headers;
  std::string body;
};

/**
 * Dispatch an AWS Lambda (API Gateway) invocation.
 *
 * Extracts method / path / headers / body from the API-Gateway `event`
 * (HTTP API v2 `rawPath` / `requestContext.http.method`, REST API v1
 * `httpMethod` / `path`, base64-decoding `isBase64Encoded` bodies), calls
 * `agent.handle_request(...)`, and returns the API-Gateway-shaped response.
 */
[[nodiscard]] ServerlessResponse handle_lambda(agent::AgentBase& agent, const json& event,
                                               const json& context = json::object());

/**
 * Dispatch a Google Cloud Function invocation from an explicit request tuple
 * (the live GCF path reads these from the runtime; the tuple form is what the
 * dispatcher and tests feed in). Calls `agent.handle_request(...)`.
 */
[[nodiscard]] ServerlessResponse handle_gcf(agent::AgentBase& agent, const std::string& method,
                                            const std::string& path,
                                            const std::map<std::string, std::string>& headers,
                                            const std::optional<std::string>& body);

/**
 * Dispatch an Azure Functions invocation from a request object (method / url /
 * headers / body).
 */
[[nodiscard]] ServerlessResponse handle_azure(agent::AgentBase& agent, const json& request);

/**
 * Dispatch a CGI / FastCGI invocation. Reads REQUEST_METHOD / PATH_INFO /
 * CONTENT_TYPE / HTTP_* from `env` (defaulting to the process environment when
 * `env` is empty) and takes the request body explicitly (the live path reads
 * it from stdin via CONTENT_LENGTH).
 */
[[nodiscard]] ServerlessResponse handle_cgi(agent::AgentBase& agent,
                                            const std::map<std::string, std::string>& env,
                                            const std::optional<std::string>& body);

/**
 * Auto-detect (or force) the serverless platform and dispatch the request to
 * the matching handler, returning the `(status, headers, body)` response.
 * `mode` (empty = auto-detect via `get_execution_mode`) selects
 * lambda / google_cloud_function / azure_function / cgi. An unknown/`"server"`
 * mode renders SWML via a plain GET `handle_request` so a dispatch always
 * produces a real response (never a fall-through to serve()).
 */
[[nodiscard]] ServerlessResponse handle_serverless_request(agent::AgentBase& agent,
                                                           const json& event = json::object(),
                                                           const json& context = json::object(),
                                                           const std::string& mode = "");

}  // namespace utils
}  // namespace signalwire
