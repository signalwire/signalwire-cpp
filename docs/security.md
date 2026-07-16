# Security Configuration Guide

This guide covers the security features and configuration options available in SignalWire AI Agents SDK for both SWML-based services (SWML -- SignalWire Markup Language -- is the JSON document format that defines agent behavior) and the standalone Search Service.

## Overview

The SDK provides a unified security configuration system that ensures consistent security behavior across all services. All security settings are controlled through environment variables, with secure defaults that can be overridden as needed.

## Quick Start

### Basic HTTPS Setup

To enable HTTPS for any service:

```bash
export SWML_SSL_ENABLED=true
export SWML_SSL_CERT_PATH=/path/to/cert.pem
export SWML_SSL_KEY_PATH=/path/to/key.pem
export SWML_DOMAIN=yourdomain.com
```

### Basic Authentication

Basic authentication is enabled by default with auto-generated credentials. To set custom credentials:

```bash
export SWML_BASIC_AUTH_USER=myusername
export SWML_BASIC_AUTH_PASSWORD=mysecurepassword
```

## Environment Variables

### SSL/TLS Configuration

| Variable | Default | Description |
|----------|---------|-------------|
| `SWML_SSL_ENABLED` | `false` | Enable HTTPS (`true`, `1`, `yes` to enable) |
| `SWML_SSL_CERT_PATH` | - | Path to SSL certificate file |
| `SWML_SSL_KEY_PATH` | - | Path to SSL private key file |
| `SWML_DOMAIN` | - | Domain name for SSL (used for URL generation) |
| `SWML_SSL_VERIFY_MODE` | `CERT_REQUIRED` | SSL verification mode |

### Authentication

| Variable | Default | Description |
|----------|---------|-------------|
| `SWML_BASIC_AUTH_USER` | `signalwire` | Basic auth username |
| `SWML_BASIC_AUTH_PASSWORD` | *auto-generated* | Basic auth password (32-char token if not set) |

### Security Headers and Policies

| Variable | Default | Description |
|----------|---------|-------------|
| `SWML_USE_HSTS` | `true` | Enable HSTS when HTTPS is active |
| `SWML_HSTS_MAX_AGE` | `31536000` | HSTS max-age in seconds (1 year) |
| `SWML_ALLOWED_HOSTS` | `*` | Comma-separated list of allowed hosts |
| `SWML_CORS_ORIGINS` | `*` | Comma-separated list of allowed CORS origins |

### Request Limits

| Variable | Default | Description |
|----------|---------|-------------|
| `SWML_MAX_REQUEST_SIZE` | `10485760` | Maximum request size in bytes (10MB) |
| `SWML_RATE_LIMIT` | `60` | Requests per minute limit |
| `SWML_REQUEST_TIMEOUT` | `30` | Request timeout in seconds |

### Validation & URL Safety

These toggles relax built-in safety checks. Leave them at their secure defaults
in production — each one only exists as an escape hatch for controlled local /
testing setups.

| Variable | Default | Description |
|----------|---------|-------------|
| `SWML_ALLOW_PRIVATE_URLS` | `false` | When `true`/`1`/`yes`, permit webhook / fetch URLs that resolve to private, loopback, or link-local addresses. **Security-critical:** the default (`false`) blocks these to prevent SSRF; only enable it for a trusted local environment. |
| `SWML_SKIP_SCHEMA_VALIDATION` | `false` | When `true`/`1`/`yes`, skip SWML schema validation of generated documents. **Security-critical:** disabling validation lets malformed / unexpected SWML through; leave it off outside debugging. |

### Webhook Signature Validation

When a signing key is configured, `AgentBase` auto-mounts the SignalWire webhook
signature validator on its POST routes (`/`, `/swaig`, `/post_prompt`); requests
that are unsigned or carry a wrong signature get a `403` and never reach the
handler. The key comes from the Dashboard (API Credentials) and is resolved in
this order: an explicit `set_signing_key(...)` / constructor `signing_key`
argument first, then the `SIGNALWIRE_SIGNING_KEY` environment variable as a
fallback.

| Variable | Default | Description |
|----------|---------|-------------|
| `SIGNALWIRE_SIGNING_KEY` | - | The SignalWire Signing Key used to validate inbound webhook signatures on the agent's POST routes, honored as a fallback when no explicit key is passed to `AgentBase`. **Security-critical / secret:** it is the shared secret that authenticates webhook callers — keep it out of logs and source control; when neither it nor an explicit key is set, `AgentBase` logs a startup warning and accepts *unsigned* POSTs (a production footgun). |

```bash
export SIGNALWIRE_SIGNING_KEY=PSK...   # or pass signing_key to the AgentBase constructor
```

## Service-Specific Usage

### SWML Services (AgentBase)

SWML-based services automatically use the unified security configuration:

```cpp
#include <signalwire/agent/agent_base.hpp>

using namespace signalwire;

class MyAgent : public agent::AgentBase {
public:
    MyAgent() : AgentBase("secure-agent", "/agent") {
        // Security is configured from the environment (SWML_* variables)
    }
};

int main() {
    MyAgent agent;
    agent.serve();  // honours SWML_BASIC_AUTH_USER / SWML_BASIC_AUTH_PASSWORD and host/CORS settings; port from the constructor
}
```

> **Note:** The standalone search service (semantic / vector search) is not part
> of the C++ port; it remains a Python-only capability. TLS for the C++ HTTP
> server is terminated by an external reverse proxy (see the HTTPS notes below).

## Security Headers

When HTTPS is enabled, the following security headers are automatically added to responses:

- `Strict-Transport-Security`: Forces HTTPS connections (when `SWML_USE_HSTS=true`)
- `X-Content-Type-Options: nosniff`: Prevents MIME type sniffing
- `X-Frame-Options: DENY`: Prevents clickjacking
- `X-XSS-Protection: 1; mode=block`: Enables XSS filtering
- `Referrer-Policy: strict-origin-when-cross-origin`: Controls referrer information

## CORS Configuration

Cross-Origin Resource Sharing (CORS) is configured to:
- Allow credentials
- Allow all methods by default
- Allow all headers by default
- Origins controlled by `SWML_CORS_ORIGINS`

To restrict CORS to specific domains:

```bash
export SWML_CORS_ORIGINS="https://app1.example.com,https://app2.example.com"
```

## Host Validation

By default, all hosts are allowed (`SWML_ALLOWED_HOSTS=*`). To restrict to specific hosts:

```bash
export SWML_ALLOWED_HOSTS="example.com,api.example.com"
```

## Best Practices

### 1. Production HTTPS Setup

For production environments, always enable HTTPS:

```bash
# Production configuration
export SWML_SSL_ENABLED=true
export SWML_SSL_CERT_PATH=/etc/ssl/certs/server.crt
export SWML_SSL_KEY_PATH=/etc/ssl/private/server.key
export SWML_DOMAIN=api.yourdomain.com
export SWML_ALLOWED_HOSTS=api.yourdomain.com
export SWML_CORS_ORIGINS=https://app.yourdomain.com
```

### 2. Strong Authentication

Always set strong credentials in production:

```bash
export SWML_BASIC_AUTH_USER=api_user
export SWML_BASIC_AUTH_PASSWORD=$(openssl rand -base64 32)
```

### 3. Certificate Management

- Use certificates from a trusted CA in production
- For development, you can generate self-signed certificates:

```bash
# Generate self-signed certificate for development
openssl req -x509 -newkey rsa:4096 -keyout key.pem -out cert.pem -days 365 -nodes
```

### 4. Rate Limiting

Adjust rate limits based on your usage patterns:

```bash
# Higher rate limit for internal services
export SWML_RATE_LIMIT=300

# Lower rate limit for public APIs
export SWML_RATE_LIMIT=20
```

### 5. Monitoring

Monitor security-related logs:

```text
Security events are logged with structured data.
Look for log entries with:
- "security_config_loaded"  - Configuration details
- "ssl_config_invalid"      - SSL configuration errors
- "starting_server"         - Service startup with security info
```

## Migration Guide

### From Previous Versions

The security configuration is backward compatible. Existing environment variables continue to work:

- `SWML_SSL_ENABLED`, `SWML_SSL_CERT_PATH`, `SWML_SSL_KEY_PATH` - Still supported
- `SWML_BASIC_AUTH_USER`, `SWML_BASIC_AUTH_PASSWORD` - Still supported
- Auto-generated credentials if not set - Still works the same way

### New Features

The following are new security features:

1. **Search Service HTTPS**: The search service now supports HTTPS using the same environment variables
2. **Security Headers**: Automatically added when appropriate
3. **CORS Configuration**: Fine-grained control over CORS origins
4. **Host Validation**: Restrict which hosts can access the service
5. **Rate Limiting**: Built-in rate limiting support
6. **HSTS**: HTTP Strict Transport Security for HTTPS connections

## Troubleshooting

### SSL Certificate Issues

If you see SSL configuration errors:

1. Check file paths exist and are readable:
   ```bash
   ls -la $SWML_SSL_CERT_PATH $SWML_SSL_KEY_PATH
   ```

2. Verify certificate validity:
   ```bash
   openssl x509 -in $SWML_SSL_CERT_PATH -text -noout
   ```

3. Check for matching key and certificate:
   ```bash
   openssl x509 -noout -modulus -in $SWML_SSL_CERT_PATH | openssl md5
   openssl rsa -noout -modulus -in $SWML_SSL_KEY_PATH | openssl md5
   ```

### Authentication Issues

If authentication fails:

1. Check credentials are set correctly:
   ```bash
   echo "User: $SWML_BASIC_AUTH_USER"
   echo "Pass length: ${#SWML_BASIC_AUTH_PASSWORD}"
   ```

2. Look for auto-generated credentials in startup logs:
   ```
   Basic Auth: signalwire:generated_password_here
   ```

3. Test with curl:
   ```bash
   curl -u username:password https://localhost:8000/health
   ```

### CORS Issues

If you encounter CORS errors:

1. Check the origin is allowed:
   ```bash
   echo $SWML_CORS_ORIGINS
   ```

2. For development, you can temporarily allow all origins:
   ```bash
   export SWML_CORS_ORIGINS="*"
   ```

3. For production, specify exact origins:
   ```bash
   export SWML_CORS_ORIGINS="https://app.example.com,https://admin.example.com"
   ```

## Security Checklist

Before deploying to production:

- [ ] HTTPS enabled with valid certificates
- [ ] Strong authentication credentials set
- [ ] CORS origins restricted to known domains
- [ ] Host validation configured
- [ ] Rate limits appropriate for usage
- [ ] Security headers verified in responses
- [ ] Logs monitored for security events
- [ ] SSL certificate expiration monitoring in place
- [ ] Regular security updates applied