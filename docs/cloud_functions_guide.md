# SignalWire AI Agents - Cloud Functions Deployment Guide

This guide covers deploying SignalWire AI Agents to Google Cloud Functions and Azure Functions.

<!-- snippet-setup -->
```cpp
#include <signalwire/agent/agent_base.hpp>
#include <nlohmann/json.hpp>
#include <iostream>
#include <string>

using json = nlohmann::json;
```

## Overview

SignalWire AI Agents now support deployment to major cloud function platforms:

- **Google Cloud Functions** - Serverless compute platform on Google Cloud
- **Azure Functions** - Serverless compute service on Microsoft Azure
- **AWS Lambda** - Already supported (see existing documentation)

## Google Cloud Functions

### Environment Detection

The agent automatically detects Google Cloud Functions environment using these variables:
- `FUNCTION_TARGET` - The function entry point
- `K_SERVICE` - Knative service name (Cloud Run/Functions)
- `GOOGLE_CLOUD_PROJECT` - Google Cloud project ID

### Deployment Steps

Google Cloud Functions and Azure Functions do not offer a first-class C++
runtime, so a C++ agent is packaged as a **container** (deployable to Cloud Run /
Cloud Functions 2nd gen, or an Azure Custom Handler). The container's HTTP entry
point translates the inbound request into an event JSON and dispatches it through
`agent.handle_serverless_request(event, context, mode)`, which returns a
`(status, headers, body)` response.

1. **Write your handler** (`main.cpp`) — a small HTTP server that forwards each
   request to the agent:
```cpp
#include <signalwire/agent/agent_base.hpp>
#include <signalwire/utils/serverless.hpp>

using namespace signalwire;
using json = nlohmann::json;

int main() {
    // Create and configure your agent.
    signalwire::agent::AgentBase agent("my-agent", "/");
    agent.prompt_add_section("Role", "You are a helpful assistant.");

    // Build the event describing the invocation. On Cloud Run / GCF 2nd gen
    // you populate this from the incoming HTTP request; here we force the
    // Google Cloud Function dispatch mode.
    json event = {
        {"method", "POST"},
        {"path", "/"},
        {"headers", json::object()},
        {"body", ""}
    };

    utils::ServerlessResponse resp =
        agent.handle_serverless_request(event, json::object(),
                                        "google_cloud_function");

    // resp.status / resp.headers / resp.body — write these back to the caller.
    return resp.status == 200 ? 0 : 1;
}
```

2. **Build and link** against the static library (`libsignalwire.a`) inside your
   container image:
```bash
g++ -std=c++20 -I include -I deps main.cpp \
    -L build -lsignalwire -lssl -lcrypto -lpthread -o agent
```

3. **Deploy the container** (Cloud Run / Cloud Functions 2nd gen):
```bash
gcloud run deploy my-agent \
    --source . \
    --allow-unauthenticated
```

### Environment Variables

Set these environment variables for your function:

```bash
# SignalWire credentials
export SIGNALWIRE_PROJECT_ID="your-project-id"
export SIGNALWIRE_TOKEN="your-token"

# Agent configuration
export AGENT_USERNAME="your-username"
export AGENT_PASSWORD="your-password"

# Optional: Custom region/project settings
export FUNCTION_REGION="us-central1"
export GOOGLE_CLOUD_PROJECT="your-project-id"
```

### URL Format

Google Cloud Functions URLs follow this pattern:
```
https://{region}-{project-id}.cloudfunctions.net/{function-name}
```

With authentication:
```
https://username:password@{region}-{project-id}.cloudfunctions.net/{function-name}
```

## Azure Functions

### Environment Detection

The agent automatically detects Azure Functions environment using these variables:
- `AZURE_FUNCTIONS_ENVIRONMENT` - Azure Functions runtime environment
- `FUNCTIONS_WORKER_RUNTIME` - Runtime language (python, node, etc.)
- `AzureWebJobsStorage` - Azure storage connection string

### Deployment Steps

Azure Functions runs C++ as a **Custom Handler**: the Functions host forwards
each trigger to a self-hosted HTTP server (your compiled agent binary) over
`localhost`.

1. **Create your function app structure**:
```
my-agent-function/
├── agent            # compiled C++ handler binary
├── host.json
├── function.json
└── Dockerfile
```

2. **Write the custom handler** (`main.cpp`) — a small HTTP server that
   dispatches each request through the agent:
```cpp
#include <signalwire/agent/agent_base.hpp>
#include <signalwire/utils/serverless.hpp>

using namespace signalwire;
using json = nlohmann::json;

int main() {
    signalwire::agent::AgentBase agent("my-agent", "/");
    agent.prompt_add_section("Role", "You are a helpful assistant.");

    // Populate this from the inbound Azure request (method / headers / body);
    // "azure_function" forces the Azure dispatch path.
    json request = {
        {"method", "POST"},
        {"url", "/"},
        {"headers", json::object()},
        {"body", ""}
    };

    utils::ServerlessResponse resp =
        agent.handle_serverless_request(request, json::object(),
                                        "azure_function");

    // Write resp.status / resp.headers / resp.body back to the Functions host.
    return resp.status == 200 ? 0 : 1;
}
```

3. **Create `host.json`** declaring the custom handler:
```json
{
  "version": "2.0",
  "customHandler": {
    "description": {
      "defaultExecutablePath": "agent",
      "workingDirectory": "",
      "arguments": []
    },
    "enableForwardingHttpRequest": true
  }
}
```

4. **Create `function.json`**:
```json
{
  "bindings": [
    {
      "authLevel": "anonymous",
      "type": "httpTrigger",
      "direction": "in",
      "name": "req",
      "methods": ["get", "post"]
    },
    {
      "type": "http",
      "direction": "out",
      "name": "$return"
    }
  ]
}
```

5. **Deploy using Azure CLI** (custom-handler function app):
```bash
# Create function app
az functionapp create \
    --resource-group myResourceGroup \
    --consumption-plan-location westus \
    --runtime custom \
    --functions-version 4 \
    --name my-agent-function \
    --storage-account mystorageaccount

# Deploy code
func azure functionapp publish my-agent-function
```

### Environment Variables

Set these in your Azure Function App settings:

```bash
# SignalWire credentials
SIGNALWIRE_PROJECT_ID="your-project-id"
SIGNALWIRE_TOKEN="your-token"

# Agent configuration
AGENT_USERNAME="your-username"
AGENT_PASSWORD="your-password"

# Azure-specific (usually auto-set)
AZURE_FUNCTIONS_ENVIRONMENT="Development"
WEBSITE_SITE_NAME="my-agent-function"
```

### URL Format

Azure Functions URLs follow this pattern:
```
https://{function-app-name}.azurewebsites.net/api/{function-name}
```

With authentication:
```
https://username:password@{function-app-name}.azurewebsites.net/api/{function-name}
```

## Authentication

Both platforms support HTTP Basic Authentication:

### Automatic Authentication
The agent automatically validates credentials in cloud function environments:

```cpp
signalwire::agent::AgentBase agent("my-agent", "/");
agent.set_auth("your-username", "your-password");
```

### Authentication Flow
1. Client sends request with `Authorization: Basic <credentials>` header
2. Agent validates credentials against configured username/password
3. If invalid, returns 401 with `WWW-Authenticate` header
4. If valid, processes the request normally

## Testing

### SignalWire Agent Testing Tool

The C++ SDK ships a `swaig-test` CLI (`bin/swaig-test`) for testing an agent's
SWAIG tools. It operates against a **running agent over HTTP** or introspects a
**compiled example binary** — it does not simulate cloud-function runtimes
(there is no `--simulate-serverless` mode in the C++ tool). To validate the
serverless dispatch path directly, drive `handle_serverless_request(...)` from a
small C++ program with a synthesized event, as shown in the deployment sections
above.

#### Introspecting a Compiled Example

`--example <name>` locates the CMake-built binary under `build/`, runs it with
`SWAIG_LIST_TOOLS=1`, and prints the registered tools — no port binding, no HTTP:

```bash
# List the tools a compiled example registers
swaig-test --example llm_params_demo --list-tools
```

#### Testing a Running Agent Over HTTP

Start your agent (locally or in a container), then point `swaig-test` at its URL.
Basic-auth credentials can be embedded in the URL:

```bash
# List the tools the running agent exposes
swaig-test http://user:pass@localhost:3000/my-agent --list-tools

# Dump the generated SWML document
swaig-test http://user:pass@localhost:3000/my-agent --dump-swml

# Execute a specific SWAIG tool
swaig-test http://user:pass@localhost:3000/my-agent \
  --exec search_knowledge --param query=test
```

**Output Options:**
- `--list-tools` — List the SWAIG tools registered on the agent
- `--dump-swml` — Generate and display the SWML document
- `--exec <tool>` — Execute a specific SWAIG tool (URL mode)
- `--param key=value` — Pass an argument to `--exec`

### Local Testing

Build and run your compiled handler locally, then exercise it with `curl` or
`swaig-test`:

```bash
# Build the static library and your handler
mkdir -p build && cd build && cmake .. && make -j$(nproc)
g++ -std=c++20 -I ../include -I ../deps ../main.cpp \
    -L . -lsignalwire -lssl -lcrypto -lpthread -o agent

# Run it (a Service/AgentBase serve() listens on the configured port)
./agent
```

### Testing Authentication

```bash
# Test without auth (should return 401)
curl https://your-function-url/

# Test with valid auth
curl -u username:password https://your-function-url/

# Test SWAIG function call
curl -u username:password \
  -H "Content-Type: application/json" \
  -d '{"call_id": "test", "argument": {"parsed": [{"param": "value"}]}}' \
  https://your-function-url/your_function_name
```

## Best Practices

### Performance
- Use connection pooling for database connections
- Implement proper caching strategies
- Minimize cold start times with smaller deployment packages

### Security
- Always use HTTPS endpoints
- Implement proper authentication
- Use environment variables for sensitive data
- Consider using cloud-native secret management

### Monitoring
- Enable cloud platform logging
- Monitor function execution times
- Set up alerts for errors and timeouts
- Use distributed tracing for complex workflows

### Cost Optimization
- Right-size memory allocation
- Implement proper timeout settings
- Use reserved capacity for predictable workloads
- Monitor and optimize function execution patterns

## Troubleshooting

### Common Issues

**Environment Detection:**
```cpp
#include <signalwire/core/logging_config.hpp>
#include <iostream>

// Check the detected execution mode ("server", "lambda",
// "google_cloud_function", "azure_function", "cgi", ...).
std::cout << "Detected mode: " << signalwire::core::logging_config::get_execution_mode() << "\n";
```

**URL Generation:**
```cpp
signalwire::agent::AgentBase agent("test", "/");
std::cout << "Base URL: " << agent.get_full_url() << "\n";
std::cout << "Auth URL: " << agent.get_full_url(/*include_auth=*/true) << "\n";
```

**Authentication Issues:**
- Verify username/password are set correctly
- Check that Authorization header is being sent
- Ensure credentials match exactly (case-sensitive)

### Debugging

Enable debug logging by raising the shared logger's level (or setting the
`SIGNALWIRE_LOG_LEVEL=debug` environment variable before startup):
```cpp
#include <signalwire/logging.hpp>

signalwire::Logger::instance().set_level(signalwire::LogLevel::Debug);
```

Check the relevant environment variables:
```cpp
for (const char* key : {"FUNCTION_TARGET", "K_SERVICE", "GOOGLE_CLOUD_PROJECT",
                        "AZURE_FUNCTIONS_ENVIRONMENT", "FUNCTIONS_WORKER_RUNTIME"}) {
    if (const char* value = std::getenv(key)) {
        std::cout << key << ": " << value << "\n";
    }
}
```

## Migration from Other Platforms

### From AWS Lambda
- Update environment variable names
- Modify request/response handling if needed
- Update deployment scripts

### From Traditional Servers
- Add cloud function entry point
- Configure environment variables
- Update URL generation logic
- Test authentication flow

## Examples

See `examples/lambda_agent.cpp` for a complete AWS Lambda deployment example.

## Support

For issues specific to cloud function deployment:
1. Check the troubleshooting section above
2. Verify environment variables are set correctly
3. Test authentication flow manually
4. Check cloud platform logs for detailed error messages
5. Refer to platform-specific documentation for deployment issues 