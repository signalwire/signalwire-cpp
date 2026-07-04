// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT

#include "signalwire/web/web_service.hpp"

#include <openssl/crypto.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <system_error>

#include "httplib.h"
#include "signalwire/common.hpp"
#include "signalwire/logging.hpp"

namespace signalwire {
namespace web {

namespace fs = std::filesystem;

namespace {

// Python/Java default block-list.
const std::vector<std::string> kDefaultBlockedExtensions = {
    ".env", ".git", ".gitignore",  ".key",      ".pem",
    ".crt", ".pyc", "__pycache__", ".DS_Store", ".swp"};

std::string to_lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

// Return the extension (including the leading dot), lower-cased, or "".
std::string extension_of(const std::string& name) {
  auto dot = name.find_last_of('.');
  if (dot == std::string::npos) {
    return "";
  }
  return to_lower(name.substr(dot));
}

std::string file_name_of(const std::string& path) { return fs::path(path).filename().string(); }

// Minimal extension -> MIME mapping (parity with the reference custom types +
// common defaults); unknown extensions fall back to octet-stream.
std::string mime_type(const std::string& path) {
  const std::string ext = extension_of(path);
  if (ext == ".html" || ext == ".htm") {
    return "text/html";
  }
  if (ext == ".css") {
    return "text/css";
  }
  if (ext == ".js") {
    return "application/javascript";
  }
  if (ext == ".json") {
    return "application/json";
  }
  if (ext == ".txt") {
    return "text/plain";
  }
  if (ext == ".png") {
    return "image/png";
  }
  if (ext == ".jpg" || ext == ".jpeg") {
    return "image/jpeg";
  }
  if (ext == ".gif") {
    return "image/gif";
  }
  if (ext == ".svg") {
    return "image/svg+xml";
  }
  if (ext == ".pdf") {
    return "application/pdf";
  }
  return "application/octet-stream";
}

// Timing-safe string comparison.
bool secure_compare(const std::string& a, const std::string& b) {
  if (a.size() != b.size()) {
    return false;
  }
  return CRYPTO_memcmp(a.data(), b.data(), a.size()) == 0;
}

}  // namespace

WebService::WebService(int port, std::optional<std::map<std::string, std::string>> directories,
                       std::optional<std::pair<std::string, std::string>> basic_auth,
                       const std::optional<std::string>& config_file,
                       bool enable_directory_browsing,
                       std::optional<std::vector<std::string>> allowed_extensions,
                       std::optional<std::vector<std::string>> blocked_extensions,
                       std::int64_t max_file_size, bool enable_cors)
    : port_(port),
      basic_auth_(std::move(basic_auth)),
      enable_directory_browsing_(enable_directory_browsing),
      allowed_extensions_(std::move(allowed_extensions)),
      // The ternary's common type with the const default is `const vector&`, which
      // silently DROPS the move (binds const&& to the copy ctor). Move explicitly.
      blocked_extensions_(std::move(blocked_extensions).value_or(kDefaultBlockedExtensions)),
      max_file_size_(max_file_size),
      enable_cors_(enable_cors) {
  // config_file is accepted for parity; SecurityConfig/ConfigLoader wiring is
  // out of scope for this class.
  static_cast<void>(config_file);

  if (directories.has_value()) {
    for (const auto& [route, dir] : *directories) {
      directories_[normalize_route(route)] = dir;
    }
  }
}

WebService::~WebService() { stop(); }

std::string WebService::normalize_route(const std::string& route) {
  if (route.empty() || route.front() != '/') {
    return "/" + route;
  }
  return route;
}

bool WebService::blocked(const std::string& file_path) const {
  const std::string name = file_name_of(file_path);
  const std::string ext = extension_of(name);
  for (const auto& b : blocked_extensions_) {
    if (!b.empty() && b.front() == '.') {
      // Check both as an extension and as a full name (.env, .gitignore, ...).
      if (ext == to_lower(b) || name == b) {
        return true;
      }
    } else {
      if (name == b || file_path.find(b) != std::string::npos) {
        return true;
      }
    }
  }
  return false;
}

bool WebService::file_allowed(const std::string& file_path) const {
  std::error_code ec;
  if (!fs::is_regular_file(file_path, ec) || ec) {
    return false;
  }
  const auto size = fs::file_size(file_path, ec);
  if (ec || static_cast<std::int64_t>(size) > max_file_size_) {
    return false;
  }
  if (blocked(file_path)) {
    return false;
  }
  if (allowed_extensions_.has_value()) {
    const std::string ext = extension_of(file_name_of(file_path));
    return std::find(allowed_extensions_->begin(), allowed_extensions_->end(), ext) !=
           allowed_extensions_->end();
  }
  return true;
}

void WebService::add_directory(const std::string& route, const std::string& directory) {
  const std::string norm = normalize_route(route);
  std::error_code ec;
  if (!fs::exists(directory, ec) || ec) {
    throw std::invalid_argument("Directory does not exist: " + directory);
  }
  if (!fs::is_directory(directory, ec) || ec) {
    throw std::invalid_argument("Path is not a directory: " + directory);
  }
  directories_[norm] = directory;
  if (server_) {
    mount_directories();
  }
}

void WebService::remove_directory(const std::string& route) {
  directories_.erase(normalize_route(route));
  // httplib has no per-route removal; a running server keeps the handler but it
  // will 404 because the route is gone from directories_. Remount to refresh
  // the handler's captured map is unnecessary since handlers consult a fresh
  // lookup each request (see mount_directories).
}

void WebService::mount_directories() {
  if (!server_) {
    return;
  }
  for (const auto& [route, directory] : directories_) {
    const std::string pattern = route + R"((/.*)?)";
    const std::string route_copy = route;
    // The lambda body is wrapped in try/catch(...) below, so nothing actually
    // escapes into httplib's worker thread (clang-tidy can't see through the
    // lambda boundary, hence the inline suppression on the capture list).
    auto handler = [this, route_copy](  // NOLINT(bugprone-exception-escape)
                       const httplib::Request& req, httplib::Response& res) {
      // Handlers run on httplib's worker threads; an escaping exception (e.g.
      // base64_decode on a malformed Authorization header) would terminate the
      // thread. Contain any throw and surface it as a 500 instead.
      try {
        // Basic-auth check.
        if (basic_auth_.has_value()) {
          const std::string& user = basic_auth_->first;
          const std::string& pass = basic_auth_->second;
          bool ok = false;
          auto it = req.headers.find("Authorization");
          if (it != req.headers.end() && it->second.rfind("Basic ", 0) == 0) {
            std::string encoded = it->second.substr(6);
            // Trim surrounding whitespace before decoding.
            while (!encoded.empty() && std::isspace(static_cast<unsigned char>(encoded.front()))) {
              encoded.erase(encoded.begin());
            }
            while (!encoded.empty() && std::isspace(static_cast<unsigned char>(encoded.back()))) {
              encoded.pop_back();
            }
            const std::string decoded = signalwire::base64_decode(encoded);
            auto colon = decoded.find(':');
            if (colon != std::string::npos) {
              ok = secure_compare(user, decoded.substr(0, colon)) &&
                   secure_compare(pass, decoded.substr(colon + 1));
            }
          }
          if (!ok) {
            res.status = 401;
            res.set_header("WWW-Authenticate", "Basic realm=\"SignalWire Web Service\"");
            res.set_content("Authentication required", "text/plain");
            return;
          }
        }

        // The directory may have been removed after mounting.
        auto dir_it = directories_.find(route_copy);
        if (dir_it == directories_.end()) {
          res.status = 404;
          res.set_content("File not found", "text/plain");
          return;
        }
        const std::string& directory = dir_it->second;

        // Compute the path relative to the route.
        std::string rel = req.path;
        if (rel.rfind(route_copy, 0) == 0) {
          rel = rel.substr(route_copy.size());
        }
        while (!rel.empty() && rel.front() == '/') {
          rel.erase(rel.begin());
        }

        std::error_code ec;
        const fs::path base = fs::weakly_canonical(fs::absolute(directory), ec);
        fs::path full = fs::weakly_canonical(fs::absolute(fs::path(directory) / rel), ec);

        // Path-traversal protection: full must be within base.
        const std::string base_s = base.string();
        const std::string full_s = full.string();
        const bool within = full_s == base_s || full_s.rfind(base_s + "/", 0) == 0;
        if (!within) {
          res.status = 403;
          res.set_content("Access denied", "text/plain");
          return;
        }

        if (!fs::exists(full, ec) || ec) {
          res.status = 404;
          res.set_content("File not found", "text/plain");
          return;
        }

        // Directory request. When browsing is enabled, emit a simple listing;
        // otherwise serve index.html if present (Python/Java parity).
        if (fs::is_directory(full, ec)) {
          const fs::path index = full / "index.html";
          if (fs::exists(index, ec) && file_allowed(index.string())) {
            full = index;
          } else if (enable_directory_browsing_) {
            std::ostringstream listing;
            listing << "<!DOCTYPE html><html><body><ul>";
            std::error_code list_ec;
            for (const auto& entry : fs::directory_iterator(full, list_ec)) {
              const std::string name = entry.path().filename().string();
              if (!name.empty() && name.front() == '.') {
                continue;  // skip hidden files
              }
              const std::string suffix = entry.is_directory() ? "/" : "";
              listing << "<li><a href=\"" << name << suffix << "\">" << name << suffix
                      << "</a></li>";
            }
            listing << "</ul></body></html>";
            if (enable_cors_) {
              res.set_header("Access-Control-Allow-Origin", "*");
            }
            res.set_content(listing.str(), "text/html");
            return;
          } else {
            res.status = 403;
            res.set_content("Directory browsing disabled", "text/plain");
            return;
          }
        }

        if (!file_allowed(full.string())) {
          res.status = 403;
          res.set_content("File type not allowed", "text/plain");
          return;
        }

        std::ifstream in(full.string(), std::ios::binary);
        if (!in) {
          res.status = 404;
          res.set_content("File not found", "text/plain");
          return;
        }
        std::ostringstream body;
        body << in.rdbuf();
        res.set_header("Cache-Control", "public, max-age=3600");
        res.set_header("X-Content-Type-Options", "nosniff");
        if (enable_cors_) {
          res.set_header("Access-Control-Allow-Origin", "*");
        }
        res.set_content(body.str(), mime_type(full.string()));
      } catch (...) {
        // Contain ANY escaping throw (not just std::exception) so nothing
        // propagates into httplib's worker thread.
        res.status = 500;
        res.set_content("Internal server error", "text/plain");
      }
    };
    server_->Get(pattern, handler);
  }
}

int WebService::start(const std::string& host, std::optional<int> bind_port) {
  const int requested = bind_port.has_value() ? *bind_port : port_;

  server_ = std::make_unique<httplib::Server>();
  server_->set_payload_max_length(static_cast<size_t>(1024) * 1024);
  mount_directories();

  int actual = requested;
  if (requested == 0) {
    actual = server_->bind_to_any_port(host);
    if (actual == -1) {
      server_.reset();
      throw std::runtime_error("Failed to bind web service on " + host + ":0");
    }
  } else {
    if (!server_->bind_to_port(host, requested)) {
      server_.reset();
      throw std::runtime_error("Failed to bind web service on " + host + ":" +
                               std::to_string(requested));
    }
  }
  port_ = actual;

  server_thread_ = std::thread([this]() { server_->listen_after_bind(); });

  // Wait for the listener to come up so callers can connect immediately.
  for (int i = 0; i < 200; ++i) {
    if (server_->is_running()) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }

  get_logger().info("WebService listening on http://" + host + ":" + std::to_string(actual));
  return actual;
}

void WebService::stop() {
  if (server_) {
    server_->stop();
  }
  if (server_thread_.joinable()) {
    server_thread_.join();
  }
  server_.reset();
}

}  // namespace web
}  // namespace signalwire
