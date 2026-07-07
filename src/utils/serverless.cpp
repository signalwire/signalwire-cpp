// Copyright (c) 2025 SignalWire
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#include "signalwire/utils/serverless.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <regex>

#include "signalwire/agent/agent_base.hpp"
#include "signalwire/common.hpp"
#include "signalwire/core/logging_config.hpp"
#include "signalwire/swaig/function_result.hpp"

namespace signalwire {
namespace utils {

bool is_serverless_mode() {
  return signalwire::core::logging_config::get_execution_mode() != "server";
}

namespace {

// Coerce a JSON value to a string, falling back to `dflt` for absent/non-string.
std::string as_string(const json& v, const std::string& dflt = "") {
  if (v.is_string()) {
    return v.get<std::string>();
  }
  if (v.is_number_integer()) {
    return std::to_string(v.get<int64_t>());
  }
  if (v.is_boolean()) {
    return v.get<bool>() ? "true" : "false";
  }
  return dflt;
}

std::string to_upper(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), ::toupper);
  return s;
}

// Strip a query string from a path.
std::string strip_query(std::string path) {
  auto q = path.find('?');
  if (q != std::string::npos) {
    path = path.substr(0, q);
  }
  return path;
}

// Pull string-valued headers out of a JSON object into a header map.
std::map<std::string, std::string> headers_from_json(const json& obj) {
  std::map<std::string, std::string> out;
  if (obj.is_object()) {
    for (auto it = obj.begin(); it != obj.end(); ++it) {
      if (it.value().is_string()) {
        out[it.key()] = it.value().get<std::string>();
      }
    }
  }
  return out;
}

// The dispatch response for an AgentBase.handle_request() call.
ServerlessResponse dispatch(agent::AgentBase& agent, const std::string& method,
                            const std::string& path,
                            const std::map<std::string, std::string>& headers,
                            const std::optional<std::string>& body) {
  std::optional<json> parsed_body;
  if (body && !body->empty()) {
    try {
      parsed_body = json::parse(*body);
    } catch (const std::exception&) {
      parsed_body = std::nullopt;  // non-JSON body -> empty object downstream
    }
  }
  auto [status, resp_headers, resp_body] =
      agent.handle_request(method, path.empty() ? "/" : path, headers, parsed_body);
  return ServerlessResponse{status, resp_headers, resp_body};
}

// Strip leading + trailing '/' from a path (Python's path.strip("/")).
std::string strip_slashes(const std::string& s) {
  size_t b = s.find_first_not_of('/');
  if (b == std::string::npos) {
    return "";
  }
  size_t e = s.find_last_not_of('/');
  return s.substr(b, e - b + 1);
}

// Lambda basic-auth check over the event headers (Corresponds to
// AuthMixin._check_lambda_auth). Returns true only for a valid Basic header.
bool check_lambda_auth(agent::AgentBase& agent, const json& event) {
  if (!event.is_object() || !event.contains("headers")) {
    return false;
  }
  std::map<std::string, std::string> headers = headers_from_json(event["headers"]);
  std::string auth;
  for (const auto& [k, v] : headers) {
    std::string lk = k;
    std::transform(lk.begin(), lk.end(), lk.begin(), ::tolower);
    if (lk == "authorization") {
      auth = v;
      break;
    }
  }
  if (auth.size() < 6 || auth.substr(0, 6) != "Basic ") {
    return false;
  }
  std::string decoded = signalwire::base64_decode(auth.substr(6));
  auto colon = decoded.find(':');
  if (colon == std::string::npos) {
    return false;
  }
  return agent.validate_basic_auth(decoded.substr(0, colon), decoded.substr(colon + 1));
}

// The 401 auth challenge (Corresponds to AuthMixin._send_lambda_auth_challenge).
ServerlessResponse lambda_auth_challenge() {
  return ServerlessResponse{401,
                            {{"WWW-Authenticate", "Basic realm=\"SignalWire Agent\""},
                             {"Content-Type", "application/json"}},
                            R"({"error": "Unauthorized"})"};
}

// Execute a SWAIG function in serverless context and return the JSON-encoded
// result (Corresponds to ServerlessMixin._execute_swaig_function). Validates the
// function-name format and existence exactly as the reference does.
std::string execute_serverless_swaig(agent::AgentBase& agent, const std::string& function_name,
                                     const json& args, const json& raw_data) {
  static const std::regex name_re("^[a-zA-Z_][a-zA-Z0-9_]*$");
  if (!function_name.empty() && !std::regex_match(function_name, name_re)) {
    return json{{"error", "Invalid function name format: '" + function_name + "'"}}.dump();
  }
  if (!agent.has_function(function_name)) {
    return json{{"error", "Function '" + function_name + "' not found"}}.dump();
  }
  swaig::FunctionResult result = agent.on_function_call(function_name, args, raw_data);
  return result.to_json().dump();
}

// Extract the SWAIG args from raw_data (mirrors the FastAPI handler +
// serverless_mixin: argument.parsed[0], else parse argument.raw).
json extract_swaig_args(const json& raw_data) {
  if (raw_data.is_object() && raw_data.contains("argument") && raw_data["argument"].is_object()) {
    const json& argument = raw_data["argument"];
    if (argument.contains("parsed") && argument["parsed"].is_array() &&
        !argument["parsed"].empty()) {
      return argument["parsed"][0];
    }
    if (argument.contains("raw") && argument["raw"].is_string()) {
      json parsed = json::parse(argument["raw"].get<std::string>(), nullptr, false);
      if (!parsed.is_discarded()) {
        return parsed;
      }
    }
  }
  return json::object();
}

std::string env_or(const std::map<std::string, std::string>& env, const std::string& key,
                   const std::string& dflt) {
  auto it = env.find(key);
  if (it != env.end()) {
    return it->second;
  }
  const char* v = std::getenv(key.c_str());
  return v != nullptr ? std::string(v) : dflt;
}

}  // namespace

ServerlessResponse handle_lambda(agent::AgentBase& agent, const json& event, const json& /*ctx*/) {
  // Method: REST API v1 `httpMethod`, else HTTP API v2 requestContext.http.method.
  std::string method = "GET";
  if (event.contains("httpMethod")) {
    method = as_string(event["httpMethod"], "GET");
  } else if (event.contains("requestContext") && event["requestContext"].is_object() &&
             event["requestContext"].contains("http") &&
             event["requestContext"]["http"].is_object()) {
    method = as_string(event["requestContext"]["http"].value("method", json("GET")), "GET");
  }
  method = to_upper(method);

  // Path: REST v1 `path`, else HTTP API v2 `rawPath`.
  std::string path = "/";
  if (event.contains("path")) {
    path = as_string(event["path"], "/");
  } else if (event.contains("rawPath")) {
    path = as_string(event["rawPath"], "/");
  }

  std::optional<std::string> body;
  if (event.contains("body") && event["body"].is_string()) {
    std::string raw = event["body"].get<std::string>();
    if (event.value("isBase64Encoded", false)) {
      body = signalwire::base64_decode(raw);
    } else {
      body = raw;
    }
  }

  std::map<std::string, std::string> headers = event.contains("headers")
                                                   ? headers_from_json(event["headers"])
                                                   : std::map<std::string, std::string>{};

  // Lambda-mode auth + SWAIG dispatch (Corresponds to
  // ServerlessMixin.handle_serverless_request lambda branch). The event's
  // rawPath is agent-RELATIVE: "swaig" (function in body) or "{function_name}"
  // dispatches a SWAIG function directly; root/other renders SWML. This is why
  // the reference does NOT route the raw path through handle_request's
  // route-prefixed dispatch.
  if (!check_lambda_auth(agent, event)) {
    return lambda_auth_challenge();
  }

  const std::string rel_path = strip_slashes(path);

  // Parse the body once for SWAIG dispatch (function name + args + raw_data).
  json raw_data = json::object();
  std::string function_name;
  if (body && !body->empty()) {
    json parsed = json::parse(*body, nullptr, false);
    if (!parsed.is_discarded() && parsed.is_object()) {
      raw_data = parsed;
      if (parsed.contains("function") && parsed["function"].is_string()) {
        function_name = parsed["function"].get<std::string>();
      }
    }
  }

  const std::map<std::string, std::string> swaig_headers = {{"Content-Type", "application/json"}};

  // Case 1: /swaig endpoint with a function name in the body.
  if ((rel_path == "swaig") && !function_name.empty()) {
    json args = extract_swaig_args(raw_data);
    return ServerlessResponse{200, swaig_headers,
                              execute_serverless_swaig(agent, function_name, args, raw_data)};
  }
  // Case 2: path-based function routing (e.g. "/say_hello").
  if (!rel_path.empty() && rel_path != "swaig") {
    json args = extract_swaig_args(raw_data);
    return ServerlessResponse{200, swaig_headers,
                              execute_serverless_swaig(agent, rel_path, args, raw_data)};
  }

  // Case 3: root path (or /swaig without a function) — render SWML.
  return dispatch(agent, "GET", "/", headers, std::nullopt);
}

ServerlessResponse handle_gcf(agent::AgentBase& agent, const std::string& method,
                              const std::string& path,
                              const std::map<std::string, std::string>& headers,
                              const std::optional<std::string>& body) {
  return dispatch(agent, to_upper(method), strip_query(path.empty() ? "/" : path), headers, body);
}

ServerlessResponse handle_azure(agent::AgentBase& agent, const json& request) {
  std::string method = as_string(
      request.contains("method") ? request["method"] : request.value("Method", json("GET")), "GET");
  std::string url =
      as_string(request.contains("url") ? request["url"] : request.value("Url", json("/")), "/");

  // Extract just the path from the URL.
  std::string path = url;
  auto scheme = path.find("://");
  if (scheme != std::string::npos) {
    auto slash = path.find('/', scheme + 3);
    path = slash == std::string::npos ? "/" : path.substr(slash);
  }
  path = strip_query(path);

  std::optional<std::string> body;
  const json& raw_body = request.contains("body") ? request["body"] : request.value("Body", json());
  if (raw_body.is_string()) {
    body = raw_body.get<std::string>();
  }

  const json& raw_headers =
      request.contains("headers") ? request["headers"] : request.value("Headers", json::object());
  return dispatch(agent, to_upper(method), path, headers_from_json(raw_headers), body);
}

ServerlessResponse handle_cgi(agent::AgentBase& agent,
                              const std::map<std::string, std::string>& env,
                              const std::optional<std::string>& body) {
  std::string method = to_upper(env_or(env, "REQUEST_METHOD", "GET"));
  std::string path = env_or(env, "PATH_INFO", "");
  if (path.empty()) {
    path = env_or(env, "REQUEST_URI", "/");
  }
  path = strip_query(path.empty() ? "/" : path);

  // Reconstruct headers from HTTP_* env vars (+ CONTENT_TYPE) — mirrors php's
  // extractServerHeaders. HTTP_X_FOO -> X-Foo.
  std::map<std::string, std::string> headers;
  for (const auto& [k, v] : env) {
    if (k.rfind("HTTP_", 0) == 0) {
      std::string name = k.substr(5);
      std::transform(name.begin(), name.end(), name.begin(),
                     [](char c) { return c == '_' ? '-' : static_cast<char>(std::tolower(c)); });
      // Title-case each hyphen segment (Content-Type style).
      bool at_start = true;
      for (char& c : name) {
        if (at_start) {
          c = static_cast<char>(std::toupper(c));
          at_start = false;
        }
        if (c == '-') {
          at_start = true;
        }
      }
      headers[name] = v;
    }
  }
  std::string content_type = env_or(env, "CONTENT_TYPE", "");
  if (!content_type.empty()) {
    headers["Content-Type"] = content_type;
  }

  return dispatch(agent, method, path, headers, body);
}

ServerlessResponse handle_serverless_request(agent::AgentBase& agent, const json& event,
                                             const json& context, const std::string& mode) {
  std::string resolved =
      mode.empty() ? signalwire::core::logging_config::get_execution_mode() : mode;

  if (resolved == "lambda") {
    return handle_lambda(agent, event, context);
  }
  if (resolved == "google_cloud_function" || resolved == "gcf") {
    std::string method = event.value("method", std::string("GET"));
    std::string path = event.value("path", std::string("/"));
    std::optional<std::string> body;
    if (event.contains("body") && event["body"].is_string()) {
      body = event["body"].get<std::string>();
    }
    return handle_gcf(agent, method, path,
                      event.contains("headers") ? headers_from_json(event["headers"])
                                                : std::map<std::string, std::string>{},
                      body);
  }
  if (resolved == "azure_function" || resolved == "azure") {
    return handle_azure(agent, event);
  }
  if (resolved == "cgi") {
    std::map<std::string, std::string> env;
    if (event.is_object() && event.contains("env") && event["env"].is_object()) {
      env = headers_from_json(event["env"]);
    }
    std::optional<std::string> body;
    if (event.contains("body") && event["body"].is_string()) {
      body = event["body"].get<std::string>();
    }
    return handle_cgi(agent, env, body);
  }

  // "server" / unknown: still produce a real response (render SWML via GET) so
  // a dispatch never falls through to a live serve() or an empty handler.
  return dispatch(agent, "GET", "/", {}, std::nullopt);
}

}  // namespace utils
}  // namespace signalwire
