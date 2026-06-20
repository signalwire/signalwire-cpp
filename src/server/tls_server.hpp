// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#pragma once

// Shared TLS cert-resolver + httplib server factory for the SDK's three
// in-process HTTP servers (AgentServer, swml::Service, agent::AgentBase).
//
// When TLS is configured the factory returns an httplib::SSLServer (which
// IS-A httplib::Server, so it upcasts cleanly into the existing
// std::unique_ptr<httplib::Server> every server site already holds, and
// setup_routes() is unchanged). Otherwise it returns a plain Server.
//
// Configuration mirrors the Python reference (security_config.py):
//   SWML_SSL_ENABLED   — "true"/"1"/"yes" turns TLS on
//   SWML_SSL_CERT_PATH  — server certificate (PEM)
//   SWML_SSL_KEY_PATH   — server private key (PEM)
// An explicit per-instance setter (TlsServerConfig passed directly) overrides
// the environment so callers can enable TLS programmatically.

#include <memory>
#include <string>

#include "httplib.h"
#include "signalwire/common.hpp"

namespace signalwire {
namespace server {

/// Resolved TLS configuration for an in-process HTTP server.
struct TlsServerConfig {
  bool enabled = false;
  std::string cert_path;
  std::string key_path;

  /// True only when TLS is enabled AND both cert + key paths are present.
  bool usable() const { return enabled && !cert_path.empty() && !key_path.empty(); }
};

/// Resolve TLS config from the SWML_SSL_* environment variables, mirroring
/// signalwire-python's SecurityConfig.load_from_env(). Returns enabled=false
/// when SWML_SSL_ENABLED is unset/false.
inline TlsServerConfig resolve_tls_config_from_env() {
  TlsServerConfig cfg;
  std::string enabled = get_env("SWML_SSL_ENABLED", "");
  for (auto& c : enabled) c = static_cast<char>(::tolower(c));
  cfg.enabled = (enabled == "true" || enabled == "1" || enabled == "yes" || enabled == "on");
  cfg.cert_path = get_env("SWML_SSL_CERT_PATH", "");
  cfg.key_path = get_env("SWML_SSL_KEY_PATH", "");
  return cfg;
}

/// Construct the httplib server for the given config. When cfg.usable(), an
/// SSLServer (TLS termination in-process) is returned upcast to Server*;
/// otherwise a plain Server. Returns nullptr only on allocation failure.
/// When an SSLServer is requested but its cert/key fail to load, the returned
/// server's is_valid() is false — callers log and refuse to listen, the same
/// failure mode as a bad bind.
inline std::unique_ptr<httplib::Server> make_http_server(const TlsServerConfig& cfg) {
  if (cfg.usable()) {
    return std::make_unique<httplib::SSLServer>(cfg.cert_path.c_str(), cfg.key_path.c_str());
  }
  return std::make_unique<httplib::Server>();
}

}  // namespace server
}  // namespace signalwire
