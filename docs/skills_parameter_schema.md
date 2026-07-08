# Skills Parameter Schema System (C++)

This guide explains the parameter schema system for the SignalWire AI Agents C++ SDK skills, which enables GUI configuration tools and programmatic skill discovery.

## Overview

The parameter schema system allows skills to declare their configurable parameters with metadata including types, descriptions, default values, and security hints. This enables:

- **GUI Configuration Tools** - Automatically generate configuration forms
- **API Documentation** - Document all available parameters
- **Validation** - Type checking and constraint validation
- **Security** - Mark sensitive parameters as hidden
- **Environment Variables** - Indicate which parameters can be sourced from environment

## Using the Schema System

### Getting All Skills Schema

Use `skills::SkillRegistry::get_all_skills_schema()` to get a complete schema of all registered skills, keyed by skill name:

<!-- snippet-setup -->
```cpp
#include <signalwire/agent/agent_base.hpp>
#include <signalwire/swaig/function_result.hpp>
#include <signalwire/skills/skill_base.hpp>
#include <signalwire/skills/skill_registry.hpp>
#include <nlohmann/json.hpp>
#include <iostream>
using json = nlohmann::json;
signalwire::agent::AgentBase agent("my-agent");
signalwire::swaig::FunctionResult result("ok");
```

```cpp
#include <signalwire/skills/skill_registry.hpp>

using namespace signalwire;
using json = nlohmann::json;

// Get the complete schema for all skills
json schema = skills::SkillRegistry::instance().get_all_skills_schema();
```

The returned JSON has this structure (one entry per registered skill):

```json
{
    "web_search": {
        "name": "web_search",
        "description": "Search the web for information using Google Custom Search API",
        "version": "1.0.0",
        "supports_multiple_instances": true,
        "required_env_vars": [],
        "parameters": {
            "api_key": {
                "type": "string",
                "description": "Google Custom Search API key",
                "required": true,
                "hidden": true,
                "env_var": "GOOGLE_SEARCH_API_KEY"
            },
            "search_engine_id": {
                "type": "string",
                "description": "Google Custom Search Engine ID",
                "required": true,
                "hidden": true,
                "env_var": "GOOGLE_SEARCH_ENGINE_ID"
            },
            "num_results": {
                "type": "integer",
                "description": "Default number of search results to return",
                "default": 1,
                "required": false,
                "min": 1,
                "max": 10
            }
        }
    },
    "datetime": {
        "name": "datetime",
        "description": "Get current date, time, and timezone information",
        "version": "1.0.0",
        "supports_multiple_instances": false,
        "required_env_vars": [],
        "parameters": {
            "swaig_fields": {
                "type": "object",
                "description": "Additional SWAIG function metadata to merge into tool definitions",
                "default": {},
                "required": false
            }
        }
    }
}
```

### Using Schema for GUI Configuration

Here's an example of how to use the schema to generate a configuration form:

```cpp
#include <signalwire/skills/skill_registry.hpp>
#include <iostream>
#include <string>

using namespace signalwire;
using json = nlohmann::json;

// Generate an HTML form field based on a parameter schema entry
std::string generate_form_field(const std::string& param_name, const json& param_info) {
    std::string html = "<div class=\"form-group\">\n";
    html += "  <label for=\"" + param_name + "\">" +
            param_info.value("description", "") + "</label>\n";

    // Mark required fields
    std::string required = param_info.value("required", false) ? "required" : "";

    // Hide sensitive fields
    std::string input_type = param_info.value("hidden", false) ? "password" : "text";

    std::string type = param_info.value("type", "string");
    if (type == "string") {
        std::string def = param_info.value("default", std::string{});
        html += "  <input type=\"" + input_type + "\" id=\"" + param_name +
                "\" name=\"" + param_name + "\" value=\"" + def + "\" " + required + ">\n";
    } else if (type == "integer") {
        std::string def = std::to_string(param_info.value("default", 0));
        std::string min_val =
            param_info.contains("min") ? "min=\"" + std::to_string(param_info["min"].get<int>()) + "\"" : "";
        std::string max_val =
            param_info.contains("max") ? "max=\"" + std::to_string(param_info["max"].get<int>()) + "\"" : "";
        html += "  <input type=\"number\" id=\"" + param_name + "\" name=\"" + param_name +
                "\" value=\"" + def + "\" " + min_val + " " + max_val + " " + required + ">\n";
    } else if (type == "boolean") {
        std::string checked = param_info.value("default", false) ? "checked" : "";
        html += "  <input type=\"checkbox\" id=\"" + param_name + "\" name=\"" + param_name +
                "\" " + checked + ">\n";
    }

    // Show environment variable hint
    if (param_info.contains("env_var")) {
        html += "  <small>Can also be set via " + param_info["env_var"].get<std::string>() +
                " environment variable</small>\n";
    }

    html += "</div>\n";
    return html;
}

int main() {
    // Get the skills schema and pick the web_search skill
    json schema = skills::SkillRegistry::instance().get_all_skills_schema();
    json web_search_schema = schema["web_search"];

    std::cout << "<form>\n";
    for (auto& [param_name, param_info] : web_search_schema["parameters"].items()) {
        std::cout << generate_form_field(param_name, param_info);
    }
    std::cout << "</form>\n";
}
```

### Programmatic Skill Configuration

Use the schema to validate and configure skills programmatically:

```cpp
#include "signalwire/agent/agent_base.hpp"
#include "signalwire/skills/skill_registry.hpp"
#include <stdexcept>

using namespace signalwire;
using json = nlohmann::json;

class MyAgent : public agent::AgentBase {
 public:
  MyAgent() : AgentBase("my-agent") {
    // Get the schema to validate configuration
    json schema = skills::SkillRegistry::instance().get_all_skills_schema();

    // Configure the web_search skill
    json web_search_params = {{"api_key", "your-api-key"},
                              {"search_engine_id", "your-engine-id"},
                              {"num_results", 3},
                              {"max_content_length", 3000}};

    // Validate required parameters
    json web_search_schema = schema["web_search"]["parameters"];
    for (auto& [param, info] : web_search_schema.items()) {
      if (info.value("required", false) && !web_search_params.contains(param)) {
        throw std::invalid_argument("Missing required parameter: " + param);
      }
    }

    // Add the skill with validated parameters
    add_skill("web_search", web_search_params);
  }
};
```

## Parameter Schema Reference

Each parameter in the schema can have the following properties:

| Property | Type | Description |
|----------|------|-------------|
| `type` | string | Parameter type: "string", "integer", "number", "boolean", "object", "array" |
| `description` | string | Human-readable description of the parameter |
| `default` | any | Default value if not provided |
| `required` | boolean | Whether the parameter is required (default: false) |
| `hidden` | boolean | Whether to hide this field in UIs (for secrets/API keys) |
| `env_var` | string | Environment variable that can provide this value |
| `enum` | array | List of allowed values (for string types) |
| `min` | number | Minimum value (for numeric types) |
| `max` | number | Maximum value (for numeric types) |

## Implementing Parameter Schema in Skills

To add parameter schema support to a skill, override the `get_parameter_schema()` method:

```cpp
#include "signalwire/skills/skill_base.hpp"

namespace signalwire {
namespace skills {

class MyCustomSkill : public SkillBase {
 public:
  std::string skill_name() const override { return "my_custom_skill"; }
  std::string skill_description() const override { return "My custom skill"; }
  std::string skill_version() const override { return "1.0.0"; }

  /// Get the parameter schema for this skill
  json get_parameter_schema() const override {
    return json::object({
        {"api_endpoint", json::object({{"type", "string"},
                                       {"description", "API endpoint URL"},
                                       {"required", true},
                                       {"default", "https://api.example.com"}})},
        {"api_key", json::object({{"type", "string"},
                                  {"description", "API authentication key"},
                                  {"required", true},
                                  {"hidden", true},  // Mark as sensitive
                                  {"env_var", "MY_API_KEY"}})},  // Can be set via environment
        {"timeout", json::object({{"type", "integer"},
                                  {"description", "Request timeout in seconds"},
                                  {"default", 30},
                                  {"required", false},
                                  {"min", 1},
                                  {"max", 300}})},
        {"retry_count", json::object({{"type", "integer"},
                                      {"description", "Number of retries on failure"},
                                      {"default", 3},
                                      {"required", false},
                                      {"min", 0},
                                      {"max", 10}})},
        {"output_format", json::object({{"type", "string"},
                                        {"description", "Output format for results"},
                                        {"default", "json"},
                                        {"required", false},
                                        {"enum", {"json", "xml", "text"}}})},  // Allowed values
        {"enable_cache", json::object({{"type", "boolean"},
                                       {"description", "Enable response caching"},
                                       {"default", true},
                                       {"required", false}})},
    });
  }

  /// Setup the skill using parameters
  bool setup(const json& params) override {
    params_ = params;
    // Access parameters via the params argument
    api_endpoint_ = get_param<std::string>(params, "api_endpoint", "");
    api_key_ = get_param_or_env(params, "api_key", "MY_API_KEY");
    timeout_ = get_param<int>(params, "timeout", 30);
    // ... etc
    return true;
  }

  std::vector<swaig::ToolDefinition> register_tools() override { return {}; }

 private:
  std::string api_endpoint_;
  std::string api_key_;
  int timeout_ = 30;
};

}  // namespace skills
}  // namespace signalwire
```

## Common Parameter Patterns

### API Keys and Secrets

Always mark sensitive parameters as `hidden` and provide an `env_var` option:

```json
"api_key": {
    "type": "string",
    "description": "API key for authentication",
    "required": true,
    "hidden": true,
    "env_var": "SERVICE_API_KEY"
}
```

### Numeric Parameters with Constraints

Use `min` and `max` to enforce valid ranges:

```json
"port": {
    "type": "integer",
    "description": "Server port number",
    "default": 8080,
    "required": false,
    "min": 1,
    "max": 65535
}
```

### Enumerated Values

Use `enum` to restrict to specific values:

```json
"log_level": {
    "type": "string",
    "description": "Logging level",
    "default": "info",
    "required": false,
    "enum": ["debug", "info", "warning", "error"]
}
```

### Optional Features

Use boolean parameters for optional features:

```json
"enable_analytics": {
    "type": "boolean",
    "description": "Enable analytics tracking",
    "default": false,
    "required": false
}
```

## Base Parameters

All skills automatically inherit these base parameters from `SkillBase`:

- **`swaig_fields`** (object) - Additional SWAIG function metadata to merge into tool definitions
- **`tool_name`** (string) - Custom name for skill instances (only for skills whose `supports_multiple_instances()` returns `true`)

## Examples

### Simple Skill (No Parameters)

Skills like `datetime` and `math` that don't need configuration:

<!-- snippet: no-compile method-override body illustration (belongs inside a SkillBase subclass) -->
```cpp
json get_parameter_schema() const override {
  // No configurable parameters — return an empty schema
  return json::object();
}
```

### Complex Skill (Many Parameters)

Skills like `web_search` with multiple configuration options:

<!-- snippet: no-compile method-override body illustration (belongs inside a SkillBase subclass) -->
```cpp
json get_parameter_schema() const override {
  return json::object({
      // API credentials (hidden)
      {"api_key", {/* ... */}},
      {"api_secret", {/* ... */}},

      // Configuration options
      {"timeout", {/* ... */}},
      {"retry_count", {/* ... */}},

      // Feature flags
      {"enable_cache", {/* ... */}},
      {"debug_mode", {/* ... */}},

      // Customization
      {"response_template", {/* ... */}},
      {"error_messages", {/* ... */}},
  });
}
```

## Best Practices

1. **Always provide descriptions** - Make parameters self-documenting
2. **Set sensible defaults** - Allow skills to work with minimal configuration
3. **Mark secrets as hidden** - Protect sensitive information in UIs
4. **Use appropriate types** - Enable proper validation and UI controls
5. **Document environment variables** - Show alternative configuration methods
6. **Validate in setup()** - Ensure all required parameters are present
7. **Support backward compatibility** - Handle deprecated parameters gracefully

## Future Enhancements

The parameter schema system is designed to be extensible. Future enhancements may include:

- **Conditional parameters** - Show/hide based on other parameter values
- **Complex validation** - Cross-parameter validation rules
- **Nested schemas** - Support for complex object parameters
- **Internationalization** - Localized descriptions and error messages
- **Runtime parameter updates** - Modify configuration without restart