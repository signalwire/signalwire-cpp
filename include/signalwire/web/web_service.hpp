// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
//
// WebService — static-file serving service over an in-process HTTP server.
//
// Mirrors the Python reference signalwire.web.web_service.WebService and the
// Java port com.signalwire.sdk.web.WebService. Maps URL route prefixes to local
// directories and serves their files over HTTP with a file-allowed safety check
// (size + extension/name filters), path-traversal protection, and optional
// basic auth.
//
// Idiom note: the Python reference builds a FastAPI/uvicorn app; C++ (an AOT
// port, like Java) uses the vendored cpp-httplib server. start() launches the
// server on a background thread (non-blocking) and returns the bound port, so
// it is safe to start/stop in tests without hanging. Pass port 0 to bind an
// OS-assigned ephemeral port.

#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

// Forward-declare the httplib server to keep the massive header out of this one.
namespace httplib {
class Server;
}  // namespace httplib

namespace signalwire {
namespace web {

/// Static file serving service with an HTTP API.
class WebService {
 public:
  /// Default maximum servable file size (100 MB).
  static constexpr std::int64_t kDefaultMaxFileSize = 100LL * 1024 * 1024;

  /// Construct a WebService.
  ///
  /// Mirrors the Python reference constructor surface. `config_file` is accepted
  /// for signature parity; config-file loading (SecurityConfig / ConfigLoader)
  /// is out of scope for this class and is a no-op here.
  explicit WebService(int port = 8002,
                      std::optional<std::map<std::string, std::string>> directories = std::nullopt,
                      std::optional<std::pair<std::string, std::string>> basic_auth = std::nullopt,
                      std::optional<std::string> config_file = std::nullopt,
                      bool enable_directory_browsing = false,
                      std::optional<std::vector<std::string>> allowed_extensions = std::nullopt,
                      std::optional<std::vector<std::string>> blocked_extensions = std::nullopt,
                      std::int64_t max_file_size = kDefaultMaxFileSize, bool enable_cors = true);

  ~WebService();

  WebService(const WebService&) = delete;
  WebService& operator=(const WebService&) = delete;

  /// Add a directory to serve at `route`. Remounts immediately if running.
  /// Throws std::invalid_argument when the path does not exist or is not a
  /// directory (Python raises ValueError).
  void add_directory(const std::string& route, const std::string& directory);

  /// Remove the directory served at `route` (no-op when absent).
  void remove_directory(const std::string& route);

  /// Start the service (non-blocking). Binds `bind_port` (or the configured
  /// port; 0 = OS-assigned ephemeral) on `host` and returns the bound port.
  int start(const std::string& host = "0.0.0.0", std::optional<int> bind_port = std::nullopt);

  /// Stop the service and release the socket. Safe to call when not running.
  void stop();

  /// Whether a file may be served (size + extension/name filters). Mirrors the
  /// Java fileAllowed / Python _is_file_allowed.
  [[nodiscard]] bool file_allowed(const std::string& file_path) const;

  // ---- Accessors ----
  [[nodiscard]] int port() const { return port_; }
  [[nodiscard]] const std::map<std::string, std::string>& directories() const {
    return directories_;
  }

 private:
  /// Normalize a route to a leading '/'.
  [[nodiscard]] static std::string normalize_route(const std::string& route);

  /// Whether the file is blocked by name/extension filters.
  [[nodiscard]] bool blocked(const std::string& file_path) const;

  /// (Re)mount all directories onto the running server.
  void mount_directories();

  int port_;
  std::map<std::string, std::string> directories_;
  std::optional<std::pair<std::string, std::string>> basic_auth_;
  bool enable_directory_browsing_;
  std::optional<std::vector<std::string>> allowed_extensions_;
  std::vector<std::string> blocked_extensions_;
  std::int64_t max_file_size_;
  bool enable_cors_;

  std::unique_ptr<httplib::Server> server_;
  std::thread server_thread_;
};

}  // namespace web
}  // namespace signalwire
