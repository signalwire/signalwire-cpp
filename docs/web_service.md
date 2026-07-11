# WebService Documentation

<!-- snippet-setup -->
```cpp
#include <signalwire/web/web_service.hpp>
#include <nlohmann/json.hpp>
#include <map>
#include <string>
#include <vector>
#include <optional>
#include <memory>

using json = nlohmann::json;
```

The `WebService` class provides static file serving capabilities for the SignalWire AI Agents SDK. It maps URL route prefixes to local directories and serves their files over an in-process HTTP server (the vendored cpp-httplib), with file-allowed safety checks, path-traversal protection, and optional basic auth.

## Table of Contents
- [Overview](#overview)
- [Building](#building)
- [Quick Start](#quick-start)
- [Configuration](#configuration)
- [Security Features](#security-features)
- [HTTPS/SSL Support](#httpsssl-support)
- [API Endpoints](#api-endpoints)
- [Usage Examples](#usage-examples)
- [Deployment Patterns](#deployment-patterns)

## Overview

WebService is designed to serve static files with configurable security features. It's perfect for:
- Serving agent documentation and API specs
- Hosting static assets (images, CSS, JavaScript)
- Serving generated reports and exports
- Providing configuration files and templates
- Hosting agent UI components

### Key Features
- **Multiple directory mounting** - Serve different directories at different URL paths
- **Security-first design** - Authentication, CORS, security headers, file filtering
- **Directory browsing** - Optional HTML directory listings
- **MIME type handling** - Automatic content-type detection
- **Path traversal protection** - Prevents access outside designated directories
- **File filtering** - Allow/block specific file extensions

## Building

WebService is part of the core SignalWire AI Agents SDK static library
(`libsignalwire.a`). Include its header and link the library:

```cpp
#include <signalwire/web/web_service.hpp>

using namespace signalwire;
```

## Quick Start

```cpp
#include <signalwire/web/web_service.hpp>

using namespace signalwire;

int main() {
    // Create a service to serve files
    signalwire::web::WebService service(
        8002,
        std::map<std::string, std::string>{
            {"/docs", "./documentation"},
            {"/assets", "./static/assets"},
        });

    // Start the service (non-blocking; returns the bound port)
    int bound = service.start();
    // Service available at http://localhost:8002
}
```

## Configuration

WebService is configured through its constructor.

### Constructor Parameters

```cpp
signalwire::web::WebService service(
    8002,                                    // Port to bind to
    std::map<std::string, std::string>{      // URL path to directory mappings
        {"/docs", "./documentation"},
        {"/assets", "./static"},
    },
    std::make_pair("admin", "secret"),       // Custom basic auth (user, pass)
    std::nullopt,                            // config_file (parity no-op)
    true,                                    // enable_directory_browsing
    std::vector<std::string>{".html", ".css", ".js"},  // allowed_extensions
    std::vector<std::string>{".env", ".key"},          // blocked_extensions
    100LL * 1024 * 1024,                     // max_file_size (100MB)
    true);                                   // enable_cors
```

### Environment Variables

Basic-auth and TLS material can be provided via the standard SDK environment
variables consumed by the security layer:

```bash
# Basic authentication
export SWML_BASIC_AUTH_USER="admin"
export SWML_BASIC_AUTH_PASSWORD="secretpassword"

# TLS is terminated externally (reverse proxy) — see HTTPS/SSL Support below.
export SWML_ALLOWED_HOSTS="example.com,*.example.com"
export SWML_CORS_ORIGINS="https://app.example.com"
```

## Security Features

### Basic Authentication

WebService implements HTTP Basic Authentication. Credentials can be set via:

1. **Constructor**: the `basic_auth` argument, `std::make_pair("username", "password")`
2. **Environment**: `SWML_BASIC_AUTH_USER` and `SWML_BASIC_AUTH_PASSWORD`

### File Security

#### Default Blocked Extensions/Files
- `.env`, `.git`, `.gitignore`
- `.key`, `.pem`, `.crt`
- `.DS_Store`, `.swp`

The `file_allowed()` accessor reports whether a given path would be served under
the current size and extension/name filters:

```cpp
signalwire::web::WebService service(8002, std::map<std::string, std::string>{{"/docs", "./documentation"}});
bool ok = service.file_allowed("./documentation/index.html");
```

#### Path Traversal Protection
WebService prevents access outside designated directories. Requests such as
`GET /docs/../../../etc/passwd` or `GET /docs/./././../config.json` are rejected
before touching the filesystem.

#### File Size Limits
Default maximum file size is 100MB. Configure it via the `max_file_size`
constructor argument:

```cpp
signalwire::web::WebService service(
    8002, std::nullopt, std::nullopt, std::nullopt,
    false, std::nullopt, std::nullopt,
    50LL * 1024 * 1024);  // 50MB
```

### Security Headers

Automatically adds security headers to all responses:
- `X-Content-Type-Options: nosniff`
- `X-Frame-Options: DENY`
- `X-XSS-Protection: 1; mode=block`

## HTTPS/SSL Support

The C++ WebService serves plain HTTP from its in-process server; TLS is
terminated **externally** by a reverse proxy (Nginx/Apache) or a load balancer.
This mirrors the SDK's build configuration where cpp-httplib is compiled without
OpenSSL TLS support. See [Nginx Reverse Proxy](#nginx-reverse-proxy) below for a
production TLS front-end.

### Generating Self-Signed Certificates

For development/testing behind a proxy:

```bash
# Generate a self-signed certificate for the reverse proxy
openssl req -x509 -newkey rsa:4096 -keyout key.pem -out cert.pem \
    -days 365 -nodes -subj "/CN=localhost"
```

## API Endpoints

### GET /health
Health check endpoint (no authentication required)

**Response:**
```json
{
    "status": "healthy",
    "directories": ["/docs", "/assets"],
    "auth_required": true,
    "directory_browsing": true
}
```

### GET /
Root endpoint showing available directories

**Response:** HTML page listing all mounted directories

### GET /{route}/{file_path}
Serve files from mounted directories

**Parameters:**
- `route`: The mounted directory route (e.g., `/docs`)
- `file_path`: Path to file within the directory

**Response:**
- File content with appropriate MIME type
- 404 if file not found
- 403 if file type blocked or directory browsing disabled

## Usage Examples

### Basic File Serving

```cpp
signalwire::web::WebService service(
    8002,
    std::map<std::string, std::string>{
        {"/docs", "./documentation"},
        {"/api", "./api-specs"},
    });
service.start();

// Files accessible at:
// http://localhost:8002/docs/index.html
// http://localhost:8002/api/swagger.json
```

### With Directory Browsing

```cpp
signalwire::web::WebService service(
    8002,
    std::map<std::string, std::string>{{"/files", "./public"}},
    std::nullopt, std::nullopt,
    /*enable_directory_browsing=*/true);
service.start();

// Browse files at: http://localhost:8002/files/
```

### Restricted File Types

```cpp
// Only serve web assets
signalwire::web::WebService service(
    8002,
    std::map<std::string, std::string>{{"/web", "./www"}},
    std::nullopt, std::nullopt,
    /*enable_directory_browsing=*/false,
    std::vector<std::string>{".html", ".css", ".js", ".png", ".jpg", ".woff2"});
```

### Dynamic Directory Management

```cpp
signalwire::web::WebService service;  // defaults (port 8002, no directories)

// Add directories after construction
service.add_directory("/docs", "./documentation");
service.add_directory("/reports", "./generated/reports");

// Remove a directory
service.remove_directory("/reports");

service.start();
```

### With Custom Authentication

```cpp
signalwire::web::WebService service(
    8002,
    std::map<std::string, std::string>{{"/private", "./sensitive-docs"}},
    std::make_pair("admin", "super-secret-password"));
service.start();
```

### Ephemeral Port (tests)

```cpp
// Pass port 0 to bind an OS-assigned ephemeral port; start() returns it.
signalwire::web::WebService service(
    0, std::map<std::string, std::string>{{"/docs", "./documentation"}});
int bound = service.start("127.0.0.1");
// ... exercise the service on `bound` ...
service.stop();
```

## Deployment Patterns

### Standalone Service

Run WebService as a dedicated static file server:

```cpp
// web_server.cpp
#include <signalwire/web/web_service.hpp>

#include <chrono>
#include <thread>

using namespace signalwire;

int main() {
    signalwire::web::WebService service(
        8002,
        std::map<std::string, std::string>{
            {"/docs", "/var/www/docs"},
            {"/assets", "/var/www/assets"},
            {"/downloads", "/var/www/downloads"},
        });
    service.start();  // non-blocking

    // Keep the process alive while the background server runs.
    while (true) {
        std::this_thread::sleep_for(std::chrono::hours(1));
    }
}
```

### Alongside AI Agents

`WebService::start()` is non-blocking (it runs the HTTP server on a background
thread), so you can start it and then run your agent on a different port:

```cpp
#include <signalwire/agent/agent_base.hpp>
#include <signalwire/web/web_service.hpp>

using namespace signalwire;

class MyAgent : public signalwire::agent::AgentBase {
public:
    MyAgent() : AgentBase("My Agent") {}
};

int main() {
    // Start WebService in the background (returns immediately)
    signalwire::web::WebService web(
        8002, std::map<std::string, std::string>{{"/docs", "./agent-docs"}});
    web.start();

    // Run the agent on its own port (set via the constructor's port argument)
    MyAgent agent;
    agent.serve();  // Agent on its constructor port (3000), WebService on 8002
}
```

### Nginx Reverse Proxy

For production TLS termination, put Nginx in front of the plain-HTTP WebService:

```nginx
server {
    listen 80;
    server_name static.example.com;

    # Redirect to HTTPS
    return 301 https://$server_name$request_uri;
}

server {
    listen 443 ssl http2;
    server_name static.example.com;

    ssl_certificate /etc/ssl/certs/example.com.crt;
    ssl_certificate_key /etc/ssl/private/example.com.key;

    location / {
        proxy_pass http://localhost:8002;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;

        # Cache static assets
        location ~* \.(jpg|jpeg|png|gif|ico|css|js)$ {
            proxy_pass http://localhost:8002;
            expires 1h;
            add_header Cache-Control "public, immutable";
        }
    }
}
```

## Best Practices

### Security
1. **Always terminate TLS at the proxy in production** - Protect data in transit
2. **Change default credentials** - Set explicit `basic_auth` in production
3. **Restrict file types** - Use `allowed_extensions` to whitelist safe files
4. **Disable directory browsing** - Turn off in production environments
5. **Use a reverse proxy** - Put Nginx/Apache in front for TLS and extra security

### Performance
1. **Set appropriate cache headers** at the reverse proxy
2. **Limit file sizes** - Adjust `max_file_size` based on your needs
3. **Use a CDN for static assets** - Offload traffic for better performance
4. **Compress large files** - Use gzip/brotli at the reverse proxy level

### Organization
1. **Separate content types** - Use different routes for different file types
2. **Version your assets** - Include a version in the path (e.g., `/assets/v1/`)
3. **Use index.html** - Provide default files for directories
4. **Document your structure** - Maintain clear directory organization

## API Reference

### WebService Class

```cpp
class WebService {
 public:
    static constexpr std::int64_t kDefaultMaxFileSize = 100LL * 1024 * 1024;

    explicit WebService(
        int port = 8002,
        std::optional<std::map<std::string, std::string>> directories = std::nullopt,
        std::optional<std::pair<std::string, std::string>> basic_auth = std::nullopt,
        const std::optional<std::string>& config_file = std::nullopt,
        bool enable_directory_browsing = false,
        std::optional<std::vector<std::string>> allowed_extensions = std::nullopt,
        std::optional<std::vector<std::string>> blocked_extensions = std::nullopt,
        std::int64_t max_file_size = kDefaultMaxFileSize,
        bool enable_cors = true);
};
```

#### Parameters
- `port`: Port to bind to (default: 8002; 0 = OS-assigned ephemeral)
- `directories`: Map of URL route prefixes to local directories
- `basic_auth`: `std::pair<username, password>` for authentication
- `config_file`: accepted for signature parity (loading is a no-op)
- `enable_directory_browsing`: Allow directory listing (default: false)
- `allowed_extensions`: List of allowed file extensions
- `blocked_extensions`: List of blocked file extensions
- `max_file_size`: Maximum file size in bytes (default: 100MB)
- `enable_cors`: Enable CORS headers (default: true)

#### Methods

##### start()
```cpp
int start(const std::string& host = "0.0.0.0",
          std::optional<int> bind_port = std::nullopt);
```
Start the web service (non-blocking) and return the bound port.

##### stop()
```cpp
void stop();
```
Stop the service and release the socket. Safe to call when not running.

##### add_directory()
```cpp
void add_directory(const std::string& route, const std::string& directory);
```
Add a new directory to serve. Throws `std::invalid_argument` when the path does
not exist or is not a directory.

##### remove_directory()
```cpp
void remove_directory(const std::string& route);
```
Remove a directory from being served (no-op when absent).

##### file_allowed()
<!-- snippet: no-compile member-function signature listing (const qualifier shown out of class context) -->
```cpp
bool file_allowed(const std::string& file_path) const;
```
Whether a file may be served under the current size and extension/name filters.

## Integration with SignalWire Agents

WebService complements AI agents by providing static file serving:

```cpp
#include <signalwire/agent/agent_base.hpp>
#include <signalwire/web/web_service.hpp>

using namespace signalwire;
using json = nlohmann::json;

class DocumentationAgent : public signalwire::agent::AgentBase {
public:
    DocumentationAgent() : AgentBase("Documentation Assistant") {
        // Reference documentation served by WebService
        prompt_add_section(
            "Documentation",
            "User documentation is available at https://example.com:8002/docs/");

        json params = {
            {"type", "object"},
            {"properties",
             {{"doc_name",
               {{"type", "string"},
                {"description", "Name of the documentation page"}}}}}};

        define_tool(
            "get_doc_link", "Get link to a documentation page", params,
            [](const json& args, const json& /*raw*/) -> swaig::FunctionResult {
                std::string doc_name = args.at("doc_name");
                return swaig::FunctionResult(
                    "Documentation available at: https://example.com:8002/docs/"
                    + doc_name + ".html");
            });
    }
};

int main() {
    // Start WebService for documentation (non-blocking)
    signalwire::web::WebService web(
        8002, std::map<std::string, std::string>{{"/docs", "./documentation"}});
    web.start();

    // Start the agent (port comes from its constructor, default 3000)
    DocumentationAgent agent;
    agent.serve();
}
```

## Summary

WebService provides a secure, configurable static file server that integrates
with the SignalWire AI Agents SDK. It follows the same architectural patterns as
other SDK services, providing configurable security features and flexible
deployment options.
