# Third-Party Skills Integration Guide

This guide explains how to create and integrate third-party skills with the SignalWire AI Agents C++ SDK. The SDK supports multiple methods for loading external skills, making it easy to extend agent capabilities without modifying the core SDK.

## Overview

Third-party skills can be integrated using three different methods:

1. **Direct Registration** - Register skill factories programmatically with `SkillRegistry`
2. **Directory Registration** - Add directories containing skill collections
3. **Environment Variables** - Configure skill paths via environment

All third-party skills are discovered and indexed the same way as built-in skills, appearing in `SkillRegistry::instance().get_all_skills_schema()` output with their parameter schemas.

## Creating a Third-Party Skill

Third-party skills follow the same structure as built-in skills. Here's a minimal example:

```cpp
// my_weather_skill/weather_skill.cpp
#include "signalwire/skills/skill_base.hpp"
#include "signalwire/skills/skill_registry.hpp"

namespace signalwire {
namespace skills {

/// Custom weather information skill
class WeatherSkill : public SkillBase {
 public:
  std::string skill_name() const override { return "weather"; }
  std::string skill_description() const override {
    return "Get weather information for any location";
  }
  std::string skill_version() const override { return "1.0.0"; }

  /// Define configuration parameters
  json get_parameter_schema() const override {
    return json::object({
        {"api_key", json::object({{"type", "string"},
                                  {"description", "Weather API key"},
                                  {"required", true},
                                  {"hidden", true},
                                  {"env_var", "WEATHER_API_KEY"}})},
        {"units", json::object({{"type", "string"},
                                {"description", "Temperature units"},
                                {"default", "celsius"},
                                {"required", false},
                                {"enum", {"celsius", "fahrenheit", "kelvin"}}})},
        {"cache_timeout", json::object({{"type", "integer"},
                                        {"description", "Cache timeout in seconds"},
                                        {"default", 300},
                                        {"required", false},
                                        {"min", 0},
                                        {"max", 3600}})},
    });
  }

  /// Initialize the skill
  bool setup(const json& params) override {
    params_ = params;
    api_key_ = get_param_or_env(params, "api_key", "WEATHER_API_KEY");
    if (api_key_.empty()) {
      return false;  // Weather API key is required
    }
    units_ = get_param<std::string>(params, "units", "celsius");
    cache_timeout_ = get_param<int>(params, "cache_timeout", 300);
    return true;
  }

  /// Register weather tools with the agent
  std::vector<swaig::ToolDefinition> register_tools() override {
    return {define_tool(
        "get_weather", "Get current weather for a location",
        json::object({{"location", json::object({{"type", "string"},
                                                 {"description", "City name or coordinates"}})}}),
        [this](const json& args, const json&) -> swaig::FunctionResult {
          std::string location = args.value("location", "");
          if (location.empty()) {
            return swaig::FunctionResult("Please provide a location");
          }
          // Implementation would call weather API here; this is just an example.
          std::string unit_letter(1, static_cast<char>(std::toupper(units_[0])));
          return swaig::FunctionResult("The weather in " + location + " is sunny and 22°" +
                                       unit_letter);
        })};
  }

 private:
  std::string api_key_;
  std::string units_ = "celsius";
  int cache_timeout_ = 300;
};

REGISTER_SKILL(WeatherSkill)

}  // namespace skills
}  // namespace signalwire
```

## Integration Methods

### Method 1: Direct Registration

Register individual skill factories programmatically with the global `SkillRegistry`. (The `REGISTER_SKILL(WeatherSkill)` macro above already does this at load time; calling `register_skill` directly is the explicit equivalent.)

```cpp
#include "signalwire/agent/agent_base.hpp"
#include "signalwire/skills/skill_registry.hpp"

using namespace signalwire;

// Register the skill globally (name + factory that constructs an instance)
skills::SkillRegistry::instance().register_skill(
    "weather", []() -> std::unique_ptr<skills::SkillBase> {
      return std::make_unique<skills::WeatherSkill>();
    });

// Now use it in any agent
class MyAgent : public agent::AgentBase {
 public:
  MyAgent() : AgentBase("my-agent") {
    // Add the registered skill
    add_skill("weather", {{"api_key", "your-api-key"}, {"units", "fahrenheit"}});
  }
};
```

### Method 2: Directory Registration

Register directories containing multiple skills:

```cpp
#include "signalwire/skills/skill_registry.hpp"

using namespace signalwire;

// Add a directory of custom skills
skills::SkillRegistry::instance().add_skill_directory("/opt/custom_skills");

// Directory structure should be:
// /opt/custom_skills/
//   weather/
//     skill.so       # Contains a registered WeatherSkill
//   stock_market/
//     skill.so       # Contains a registered StockMarketSkill
//   translation/
//     skill.so       # Contains a registered TranslationSkill

// Now use any skill from the directory
agent.add_skill("weather", {{"api_key", "..."}});
agent.add_skill("stock_market", {{"api_key", "..."}});
```

### Method 3: Compile-Time Registration

Any translation unit that ends in a `REGISTER_SKILL(...)` macro self-registers its
skill with the global `SkillRegistry` the moment it is linked into your program (or
loaded as a shared library). Package a collection of skills as their own static or
shared library and link it — no runtime discovery step is required:

```cpp
// weather_skill.cpp
#include "signalwire/skills/skill_base.hpp"
#include "signalwire/skills/skill_registry.hpp"

namespace signalwire {
namespace skills {

class WeatherSkill : public SkillBase {
  // ... skill_name(), setup(), register_tools(), etc.
};

// This line registers the factory automatically at load time.
REGISTER_SKILL(WeatherSkill)

}  // namespace skills
}  // namespace signalwire
```

Once the object file (or library) is linked, the skill is available everywhere:

```cpp
// No manual registration needed — REGISTER_SKILL already ran at load time.
agent.add_skill("weather", {{"api_key", "..."}});
```

### Method 4: Environment Variable

Set the `SIGNALWIRE_SKILL_PATHS` environment variable:

```bash
# Single directory
export SIGNALWIRE_SKILL_PATHS=/opt/my_skills

# Multiple directories (colon-separated)
export SIGNALWIRE_SKILL_PATHS=/opt/my_skills:/home/user/custom_skills
```

Skills in these directories are added to the registry's search path (equivalent to
calling `add_skill_directory` for each one). After the paths are registered, use any
skill by name:

```cpp
// Skills from the configured directories are found by name.
agent.add_skill("weather", {{"api_key", "..."}});
```

## Directory Structure

Skills loaded from directories must follow this structure:

```
my_skills_directory/
├── weather/                 # Skill directory (matches skill_name())
│   ├── skill.so            # Required: Contains a registered skill class
│   └── README.md           # Optional: Documentation
├── translation/
│   ├── skill.so
│   └── resources/          # Optional: Additional files
│       └── languages.json
└── stock_market/
    └── skill.so
```

## Skill Discovery and Schema

Third-party skills are fully integrated with the SDK's discovery system:

```cpp
#include "signalwire/skills/skill_registry.hpp"

using namespace signalwire;
using json = nlohmann::json;

// Get the schema for all skills, including third-party ones, keyed by name
json all_skills = skills::SkillRegistry::instance().get_all_skills_schema();

// Inspect a single skill's schema
std::cout << all_skills["weather"].dump(2) << "\n";
```

The `weather` entry looks like this:

```json
{
    "name": "weather",
    "description": "Get weather information for any location",
    "version": "1.0.0",
    "supports_multiple_instances": false,
    "required_env_vars": [],
    "parameters": {
        "api_key": {
            "type": "string",
            "description": "Weather API key",
            "required": true,
            "hidden": true,
            "env_var": "WEATHER_API_KEY"
        },
        "units": {
            "type": "string",
            "description": "Temperature units",
            "default": "celsius",
            "required": false,
            "enum": ["celsius", "fahrenheit", "kelvin"]
        }
    },
    "source": "external"
}
```

## Best Practices

### 1. Skill Naming

- Use lowercase, underscore-separated names
- Choose unique names to avoid conflicts with built-in skills
- Match directory name to `SKILL_NAME` for directory-based loading

### 2. Parameter Design

- Always implement `get_parameter_schema()` for GUI compatibility
- Mark sensitive parameters as `hidden`
- Provide sensible defaults
- Use `env_var` for parameters that can come from environment

### 3. Error Handling

```cpp
/// Proper setup with error handling
bool setup(const json& params) override {
  params_ = params;

  // Validate required parameters
  api_key_ = get_param_or_env(params, "api_key", "MY_API_KEY");
  if (api_key_.empty()) {
    return false;  // API key is required
  }

  // Test connectivity
  try {
    test_api_connection();
  } catch (const std::exception& e) {
    return false;  // Failed to connect to API: e.what()
  }

  return true;
}
```

### 4. Documentation

Include a README.md in your skill directory:

```markdown
# Weather Skill

Provides weather information for any location.

## Configuration

- `api_key` (required): Your weather API key
- `units` (optional): Temperature units (celsius, fahrenheit, kelvin)
- `cache_timeout` (optional): Cache timeout in seconds

## Usage

```cpp
agent.add_skill("weather", {{"api_key", "your-api-key"}, {"units", "fahrenheit"}});
```
```

## Advanced Features

### Multiple Instances

Support multiple instances of your skill:

```cpp
class WeatherSkill : public SkillBase {
 public:
  std::string skill_name() const override { return "weather"; }

  // Enable multiple instances
  bool supports_multiple_instances() const override { return true; }

  /// Create a unique key for this instance
  std::string get_instance_key() const override {
    std::string service = get_param<std::string>(params_, "service", "default");
    return skill_name() + "_" + service;
  }
};
```

Usage:

```cpp
// Add multiple weather services
agent.add_skill("weather", {{"tool_name", "openweather"},
                            {"service", "openweathermap"},
                            {"api_key", "key1"}});

agent.add_skill("weather", {{"tool_name", "weatherapi"},
                            {"service", "weatherapi"},
                            {"api_key", "key2"}});
```

### Dynamic Tool Names

Customize tool names for better agent prompts:

```cpp
std::vector<swaig::ToolDefinition> register_tools() override {
  std::string tool_name = get_param<std::string>(params_, "tool_name", "get_weather");
  std::string service = get_param<std::string>(params_, "service", "default");

  return {define_tool(
      tool_name, "Get weather using " + service,
      json::object({{"location", json::object({{"type", "string"}})}}),
      [this](const json& args, const json& raw) { return weather_handler(args, raw); })};
}
```

### Skill Dependencies

Load skills that depend on other skills:

```cpp
bool setup(const json& params) override {
  params_ = params;
  // Check that a required skill is registered and available
  if (!skills::SkillRegistry::instance().has_skill("translation")) {
    return false;  // This skill requires the translation skill
  }
  return true;
}
```

## Testing Third-Party Skills

Test your skills before distribution:

```cpp
// test_my_skill.cpp — using the project's TEST / ASSERT_* macros
#include "signalwire/agent/agent_base.hpp"
#include "signalwire/skills/skill_registry.hpp"

using namespace signalwire;

TEST(weather_skill_registration) {
  // Register the factory directly
  skills::SkillRegistry::instance().register_skill(
      "weather", []() -> std::unique_ptr<skills::SkillBase> {
        return std::make_unique<skills::WeatherSkill>();
      });

  // Adding the skill to an agent should succeed
  agent::AgentBase agent("test-agent");
  agent.add_skill("weather", {{"api_key", "test-key"}});
  ASSERT_TRUE(agent.has_skill("weather"));
}

TEST(weather_skill_parameter_schema) {
  skills::WeatherSkill skill;
  json schema = skill.get_parameter_schema();
  ASSERT_TRUE(schema.contains("api_key"));
  ASSERT_TRUE(schema["api_key"]["required"].get<bool>());
  ASSERT_TRUE(schema["api_key"]["hidden"].get<bool>());
}
```

## Troubleshooting

### Skill Not Found

If your skill isn't being discovered:

1. Check the skill directory structure
2. Verify `skill_name()` matches the directory name
3. Ensure the skill object/library is linked and ends in a `REGISTER_SKILL(...)` macro
4. Check logs for loading errors

### Linkage Errors

If a `REGISTER_SKILL(...)` translation unit is in a static library, the linker may
drop the object file because nothing references it directly, and the skill never
registers. Force the object in — link the skill's `.cpp` directly into the
executable, use `--whole-archive` (GCC/Clang) / `/WHOLEARCHIVE` (MSVC) for the
skill library, or call `ensure_builtin_skills_registered()` for built-in skills.

### Environment Variables

Debug environment variable loading:

```cpp
#include "signalwire/skills/skill_registry.hpp"
#include <cstdlib>
#include <iostream>

using namespace signalwire;
using json = nlohmann::json;

const char* paths = std::getenv("SIGNALWIRE_SKILL_PATHS");
std::cout << "Skill paths: " << (paths ? paths : "Not set") << "\n";

json sources = skills::SkillRegistry::instance().list_all_skill_sources();
std::cout << "External skills: " << sources["external"].dump() << "\n";
```

## Example: Complete Third-Party Skill Package

Here's a complete example of a distributable skill package:

```
my-signalwire-skills/
├── CMakeLists.txt
├── README.md
├── include/
│   └── my_signalwire_skills/
│       └── utils.hpp
├── src/
│   ├── weather_skill.cpp        # WeatherSkill + REGISTER_SKILL(WeatherSkill)
│   └── translation_skill.cpp    # TranslationSkill + REGISTER_SKILL(TranslationSkill)
└── tests/
    ├── test_weather.cpp
    └── test_translation.cpp
```

Build the skills into their own library and link it against `libsignalwire`:

```cmake
# CMakeLists.txt
cmake_minimum_required(VERSION 3.15)
project(my_signalwire_skills CXX)

set(CMAKE_CXX_STANDARD 17)

add_library(my_signalwire_skills
    src/weather_skill.cpp
    src/translation_skill.cpp)

target_include_directories(my_signalwire_skills PUBLIC include)
target_link_libraries(my_signalwire_skills PUBLIC signalwire)
```

Link and use — the `REGISTER_SKILL(...)` macros make each skill available by name:

```cpp
#include "signalwire/agent/agent_base.hpp"

using namespace signalwire;

int main() {
  agent::AgentBase agent("my-agent");
  agent.add_skill("weather", {{"api_key", "..."}});
  agent.add_skill("translate", {{"api_key", "..."}});
  agent.run();
}
```