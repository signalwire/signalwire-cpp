# SignalWire SWML Service Guide

<!-- snippet-setup -->
```cpp
#include <signalwire/swml/service.hpp>
#include <signalwire/logging.hpp>
#include <nlohmann/json.hpp>
#include <iostream>
#include <string>

using json = nlohmann::json;
signalwire::swml::Service service;
json document = json::object();
```

## Table of Contents
- [Introduction](#introduction)
- [Installation](#installation)
- [Basic Usage](#basic-usage)
- [Centralized Logging System](#centralized-logging-system)
- [SWML Document Creation](#swml-document-creation)
- [Verb Handling](#verb-handling)
- [Web Service Features](#web-service-features)
- [Custom Routing Callbacks](#custom-routing-callbacks)
- [Advanced Usage](#advanced-usage)
- [API Reference](#api-reference)
- [Examples](#examples)

## Introduction

The `SWMLService` class provides a foundation for creating and serving SignalWire Markup Language (SWML) documents. It serves as the base class for all SignalWire services, including AI Agents, and handles common tasks such as:

- SWML document creation and manipulation
- Schema validation
- Web service functionality
- Authentication
- Centralized logging

The class is designed to be extended for specific use cases, while providing a full set of capabilities out of the box.

## Installation

The `swml::Service` class is part of the SignalWire C++ SDK, which is built from
source as a static library (`libsignalwire.a`). Build it with the project scripts
(see the build section of `CLAUDE.md`) and link against it:

```bash
mkdir -p build && cd build && cmake .. && make -j$(nproc)
```

Include the header in your code:

```cpp
#include <signalwire/swml/service.hpp>

using namespace signalwire;
using json = nlohmann::json;
```

## Basic Usage

Here's a simple example of creating an SWML service:

```cpp
#include <signalwire/swml/service.hpp>

using namespace signalwire;
using json = nlohmann::json;

int main() {
    signalwire::swml::Service service;
    service.set_name("voice-service")
           .set_route("/voice")
           .set_host("0.0.0.0")
           .set_port(3000);

    // Build the SWML document. Start fresh, then add verbs to the "main" section.
    service.reset_document();
    service.add_verb("main", "answer", json::object());
    service.add_verb("main", "play", {{"url", "say:Hello, thank you for calling our service."}});
    service.add_verb("main", "hangup", json::object());

    // Start the service (blocking).
    service.serve();
}
```

## Centralized Logging System

The C++ SDK provides a thread-safe, process-wide logger that all services share. It is available through `signalwire::Logger` (or the free helper `get_logger()`), so you do not need to configure logging in each service.

### How It Works

1. `Logger::instance()` returns the single shared logger for the process
2. Each service and the SDK internals write to that same logger
3. Every line is tagged with its severity (`debug`, `info`, `warn`, `error`)
4. The active level is read from the `SIGNALWIRE_LOG_LEVEL` environment variable on first use, and can be overridden at runtime with `set_level(...)`

### Using the Logger

Obtain the shared logger and call the level methods:

```cpp
signalwire::Logger& logger = signalwire::Logger::instance();  // or: signalwire::get_logger()

// Basic logging
logger.info("service_started");

// Contextual detail — build the message yourself
logger.debug("document_created size=" + std::to_string(document.dump().size()));

// Error logging
try {
    // Some operation
} catch (const std::exception& e) {
    logger.error(std::string("operation_failed error=") + e.what());
}
```

### Log Levels

The following log levels are available (in increasing order of severity), corresponding to the `LogLevel` enum values `Debug`, `Info`, `Warn`, `Error`, and `Off`:
- `debug`: Detailed information for debugging
- `info`: General information about operation
- `warn`: Warning about potential issues
- `error`: Error information when operations fail
- `Off`: Silence all logging

### Suppressing Logs

To suppress logs when running a service, raise the level so only warnings and above are shown:

```cpp
signalwire::Logger::instance().set_level(signalwire::LogLevel::Warn);  // Only show warnings and above
```

You can also set the level for the whole process via the environment before startup:

```bash
export SIGNALWIRE_LOG_LEVEL=warn
```

## SWML Document Creation

The `SWMLService` class provides methods for creating and manipulating SWML documents.

### Document Structure

SWML documents have the following basic structure:

```json
{
  "version": "1.0.0",
  "sections": {
    "main": [
      { "verb1": { /* configuration */ } },
      { "verb2": { /* configuration */ } }
    ],
    "section1": [
      { "verb3": { /* configuration */ } }
    ]
  }
}
```

### Document Methods

- `reset_document()`: Reset the document to an empty state
- `add_verb(section_name, verb_name, config)`: Add a verb to a named section (e.g. `"main"`)
- `add_section(section_name)`: Add a new section
- `add_verb_to_section(section_name, verb_name, config)`: Add a verb to a specific section
- `get_document()`: Get the current document as a dictionary
- `render_document()`: Get the current document as a JSON string

### Common Verb Shortcuts

- `add_verb(section_name, verb_name, config)`: Add any SWML verb to a section with configuration

## Verb Handling

The `SWMLService` class provides validation for SWML verbs using the SignalWire schema.

### Verb Validation

When adding a verb, the service validates it against the schema to ensure it has the correct structure and parameters.

```cpp
// This will validate the configuration against the schema
service.add_verb("main", "play", {
    {"url", "say:Hello, world!"},
    {"volume", 5}
});

// This would fail validation (invalid parameter)
service.add_verb("main", "play", {
    {"invalid_param", "value"}
});
```

### Custom Verb Handlers

You can register custom verb handlers for specialized verb processing. Subclass
`signalwire::core::SWMLVerbHandler` and register an instance with
`register_verb_handler`:

```cpp
#include <signalwire/core/swml_handler.hpp>
#include <signalwire/swml/service.hpp>

using namespace signalwire;
using json = nlohmann::json;

class CustomPlayHandler : public signalwire::core::SWMLVerbHandler {
public:
    std::string get_verb_name() const override { return "play"; }

    signalwire::core::VerbValidationResult validate_config(const json& config) const override {
        (void)config;
        // Custom validation logic: {valid, errors}
        return {true, {}};
    }

    json build_config(const json& kwargs = json::object()) const override {
        // Custom configuration building
        return kwargs;
    }
};

void register_handlers(signalwire::swml::Service& service) {
    service.register_verb_handler(std::make_shared<CustomPlayHandler>());
}
```

## Web Service Features

The `SWMLService` class includes built-in web service capabilities for serving SWML documents.

### Endpoints

By default, a service provides the following endpoints:

- `GET /route`: Return the SWML document
- `POST /route`: Process request data and return the SWML document
- `GET /route/`: Same as above but with trailing slash
- `POST /route/`: Same as above but with trailing slash

Where `route` is the route path specified when creating the service.

### Authentication

Basic authentication is automatically set up for all endpoints. Credentials are generated if not provided, or can be specified with `set_auth`:

```cpp
service.set_name("my-service")
       .set_auth("username", "password");
```

You can also set credentials using environment variables:
- `SWML_BASIC_AUTH_USER`
- `SWML_BASIC_AUTH_PASSWORD`

### Dynamic SWML Generation

You can override the `on_swml_request` method to customize SWML documents based on request data:

```cpp
class VipVoiceService : public signalwire::swml::Service {
protected:
    std::optional<json> on_swml_request(
        const std::optional<json>& request_data = std::nullopt,
        const std::optional<std::string>& callback_path = std::nullopt) override {
        (void)callback_path;
        if (!request_data) {
            return std::nullopt;
        }

        // Customize document based on request_data
        reset_document();
        add_verb("main", "answer", json::object());

        // Add custom verbs based on request_data
        if (request_data->value("caller_type", "") == "vip") {
            add_verb("main", "play", {{"url", "say:Welcome VIP caller!"}});
        } else {
            add_verb("main", "play", {{"url", "say:Welcome caller!"}});
        }

        // Return std::nullopt to use the document we've built without
        // further modifications.
        return std::nullopt;
    }
};
```

## Custom Routing Callbacks

The `SWMLService` class allows you to register custom routing callbacks that can examine incoming requests and determine where they should be routed.

### Registering a Routing Callback

You can use the `register_routing_callback` method to register a function that will be called to process requests to a specific path. The callback receives the parsed request `body` and the request `headers`, and returns the route to dispatch to (an empty string means "process normally"):

```cpp
// Callback signature: std::string(const json& body,
//                                  const std::map<std::string, std::string>& headers)
// Return a non-empty route to redirect (HTTP 307); return "" to process normally.
service.register_routing_callback(
    [](const json& body,
       const std::map<std::string, std::string>& headers) -> std::string {
        (void)headers;
        // Example: route based on a field in the request body.
        if (body.contains("customer_id")) {
            std::string customer_id = body["customer_id"].get<std::string>();
            return "/customer/" + customer_id;
        }
        // Process request normally.
        return "";
    },
    "/customer");
```

### How Routing Works

1. When a request is received at the registered path, the routing callback is executed
2. The callback inspects the request and can decide whether to redirect it
3. If the callback returns a non-empty route string, the request is redirected with HTTP 307 (temporary redirect)
4. If the callback returns an empty string, the request is processed normally by the `on_request` method

### Serving Different Content for Different Paths

You can override `on_request` and use the `callback_path` argument to serve different content for different paths. Return `std::nullopt` to use the default SWML rendering, or a JSON object to modify/augment the document:

<!-- snippet: no-compile method-override fragment shown outside its enclosing Service subclass -->
```cpp
std::optional<json> on_request(
    const std::optional<json>& request_data = std::nullopt,
    const std::optional<std::string>& callback_path = std::nullopt) override {
    (void)request_data;

    // Serve different content based on the callback path.
    if (callback_path == "/customer") {
        return json{
            {"sections", {
                {"main", {
                    {{"answer", json::object()}},
                    {{"play", {{"url", "say:Welcome to customer service!"}}}}
                }}
            }}
        };
    } else if (callback_path == "/product") {
        return json{
            {"sections", {
                {"main", {
                    {{"answer", json::object()}},
                    {{"play", {{"url", "say:Welcome to product support!"}}}}
                }}
            }}
        };
    }

    // Default content.
    return std::nullopt;
}
```

### Example: Multi-Section Service

Here's an example of a service that uses routing callbacks to handle different types of requests:

```cpp
#include <signalwire/swml/service.hpp>
#include <signalwire/logging.hpp>

using namespace signalwire;
using json = nlohmann::json;

class MultiSectionService : public signalwire::swml::Service {
public:
    MultiSectionService() {
        set_name("multi-section");
        set_route("/main");

        // Build the main document.
        reset_document();
        answer();
        play({{"url", "say:Hello from the main service!"}});
        hangup();

        register_customer_route();
        register_product_route();
    }

private:
    void register_customer_route() {
        // Register the callback at the /customer path.
        register_routing_callback(
            [](const json& body,
               const std::map<std::string, std::string>& headers) -> std::string {
                (void)headers;
                if (body.contains("customer_id")) {
                    // In a real implementation you might redirect to another
                    // service; here we just log it and process normally.
                    get_logger().info("routing_customer id=" +
                                      body["customer_id"].get<std::string>());
                }
                return "";  // process normally
            },
            "/customer");

        // Create the customer SWML section.
        add_section("customer_section");
        add_verb_to_section("customer_section", "answer", json::object());
        add_verb_to_section("customer_section", "play",
                            {{"url", "say:Welcome to customer service!"}});
        add_verb_to_section("customer_section", "hangup", json::object());
    }

    void register_product_route() {
        // Register the callback at the /product path.
        register_routing_callback(
            [](const json& body,
               const std::map<std::string, std::string>& headers) -> std::string {
                (void)headers;
                if (body.contains("product_id")) {
                    get_logger().info("routing_product id=" +
                                      body["product_id"].get<std::string>());
                }
                return "";
            },
            "/product");

        // Create the product SWML section.
        add_section("product_section");
        add_verb_to_section("product_section", "answer", json::object());
        add_verb_to_section("product_section", "play",
                            {{"url", "say:Welcome to product support!"}});
        add_verb_to_section("product_section", "hangup", json::object());
    }

    std::optional<json> on_request(
        const std::optional<json>& request_data = std::nullopt,
        const std::optional<std::string>& callback_path = std::nullopt) override {
        (void)request_data;
        // Serve different content based on the callback path.
        json doc = get_document();
        if (callback_path == "/customer") {
            return json{{"sections",
                         {{"main", doc["sections"]["customer_section"]}}}};
        } else if (callback_path == "/product") {
            return json{{"sections",
                         {{"main", doc["sections"]["product_section"]}}}};
        }
        return std::nullopt;
    }
};
```

In this example:
1. The service registers two custom route paths: `/customer` and `/product`
2. Each path has its own callback function to handle routing decisions
3. The `on_request` method uses the `callback_path` to determine which content to serve
4. Different SWML sections are served for different paths

## Advanced Usage

### Embedding the Service's Routes in a Host Application

You can obtain a mountable HTTP router for the service to embed in a larger
application without starting its own listener. `as_router()` returns a
`std::shared_ptr<httplib::Server>` pre-populated with every route this service
exposes; the caller owns it and can `listen()` on it directly or front it behind
its own TLS/proxy:

```cpp
#include <signalwire/swml/service.hpp>
#include <httplib.h>

signalwire::swml::Service router_service;
router_service.set_name("my-service");

auto router = router_service.as_router();
router->listen("0.0.0.0", 8080);
```

### Schema Validation

The SWML schema is bundled with the SDK and used automatically to validate verbs
as they are added — there is no external schema-file path to configure. Access
the service's schema helper via `schema_utils()` if you need to inspect verb
metadata directly:

```cpp
auto& utils = service.schema_utils();  // signalwire::utils::SchemaUtils
```

## API Reference

`swml::Service` is default-constructed and configured with fluent setters (each
returns `Service&` for chaining):

- `set_name(name)`: Service name/identifier (default: "service")
- `set_route(route)`: HTTP route path (default: "/")
- `set_host(host)`: Host to bind to (default: "0.0.0.0")
- `set_port(port)`: Port to bind to (default: 3000)
- `set_auth(username, password)`: Basic-auth credentials (auto-generated if not set)

### Document Methods

- `reset_document()`
- `add_verb(section, verb_name, config)` / the named verb methods (`answer()`, `play()`, `hangup()`, …) for the main section
- `add_section(section_name)`
- `add_verb_to_section(section_name, verb_name, config)`
- `get_document()`: the document as a JSON object
- `render_document()`: the document as a JSON string
- `render_swml()`: the document as a JSON object (`json`)

### Service Methods

- `as_router()`: Get a mountable `std::shared_ptr<httplib::Server>` for the service
- `serve()`: Start the HTTP server (blocking)
- `stop()`: Stop the HTTP server
- `get_basic_auth_credentials()` / `get_basic_auth_credentials_with_source()`: Get the basic-auth credentials
- `on_swml_request(request_data, callback_path)`: Called when SWML is requested
- `register_routing_callback(callback_fn, path)`: Register a callback for request routing

### Verb Helper Methods

- `add_verb(section, verb_name, config)`: Add a verb to a named section; the named verb methods (`answer()`, `play()`, `record()`, `connect()`, `hangup()`, …) add to the main section

## Examples

### Basic Voicemail Service

```cpp
#include <signalwire/swml/service.hpp>
#include <signalwire/logging.hpp>

using namespace signalwire;
using json = nlohmann::json;

class VoicemailService : public signalwire::swml::Service {
public:
    VoicemailService(const std::string& host = "0.0.0.0", int port = 3000) {
        set_name("voicemail");
        set_route("/voicemail");
        set_host(host);
        set_port(port);

        build_voicemail_document();
    }

private:
    void build_voicemail_document() {
        // Reset the document.
        reset_document();

        // Answer the call.
        answer();

        // Greeting.
        play({{"url", "say:Hello, you've reached the voicemail service. "
                      "Please leave a message after the beep."}});

        // Play a beep.
        play({{"url", "https://example.com/beep.wav"}});

        // Record the message.
        record({
            {"format", "mp3"},
            {"stereo", false},
            {"max_length", 120},  // 2 minutes max
            {"terminators", "#"}
        });

        // Thank the caller.
        play({{"url", "say:Thank you for your message. Goodbye!"}});

        // Hang up.
        hangup();

        get_logger().debug("voicemail_document_built");
    }
};
```

### Dynamic Call Routing Service

```cpp
#include <signalwire/swml/service.hpp>
#include <signalwire/logging.hpp>
#include <algorithm>

using namespace signalwire;
using json = nlohmann::json;

class CallRouterService : public signalwire::swml::Service {
protected:
    std::optional<json> on_swml_request(
        const std::optional<json>& request_data = std::nullopt,
        const std::optional<std::string>& callback_path = std::nullopt) override {
        (void)callback_path;
        // If there's no request data, use default routing.
        if (!request_data) {
            get_logger().debug("no_request_data_using_default");
            return std::nullopt;
        }

        // Build a new document.
        reset_document();
        answer();

        // Get routing parameters.
        std::string department = request_data->value("department", "");
        std::transform(department.begin(), department.end(), department.begin(),
                       [](unsigned char c) { return std::tolower(c); });

        // Greeting.
        play({{"url", "say:Thank you for calling our " + department +
                      " department. Please hold."}});

        // Route based on department.
        const std::map<std::string, std::string> phone_numbers = {
            {"sales", "+15551112222"},
            {"support", "+15553334444"},
            {"billing", "+15555556666"}
        };
        auto it = phone_numbers.find(department);
        std::string to_number =
            it != phone_numbers.end() ? it->second : "+15559990000";

        // Connect to the department.
        connect({
            {"to", to_number},
            {"timeout", 30},
            {"answer_on_bridge", true}
        });

        // Fallback message and hangup.
        play({{"url", "say:We're sorry, but all of our agents are currently "
                      "busy. Please try again later."}});
        hangup();

        return std::nullopt;  // Use the document we've built.
    }
};
```

For more examples, see the `examples` directory in the SignalWire C++ SDK repository. 