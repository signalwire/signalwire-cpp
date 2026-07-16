# SignalWire Agents Skills System (C++)

The SignalWire Agents SDK includes a modular skills system that lets you add capabilities to your agents with simple one-liner calls and configurable parameters.

## What's New

Instead of manually implementing every agent capability, you can now:

<!-- snippet-setup -->
```cpp
#include <signalwire/agent/agent_base.hpp>
#include <signalwire/swaig/function_result.hpp>
#include <signalwire/swaig/parameter_schema.hpp>
#include <signalwire/skills/skill_base.hpp>
#include <signalwire/skills/skill_registry.hpp>
#include <nlohmann/json.hpp>
#include <iostream>
using json = nlohmann::json;
signalwire::agent::AgentBase agent("my-agent");
signalwire::swaig::FunctionResult result("ok");
```

```cpp
#include <signalwire/agent/agent_base.hpp>

int main() {
// Create an agent
signalwire::agent::AgentBase agent("My Assistant");

// Add skills with one-liners!
agent.add_skill("web_search");   // Web search capability with default settings
agent.add_skill("datetime");     // Current date/time info
agent.add_skill("math");         // Mathematical calculations

// Add skills with custom parameters!
agent.add_skill("web_search", {
    {"num_results", 3},  // Get 3 search results instead of default 1
    {"delay", 0.5}       // Add 0.5s delay between requests instead of default 0
});

// Your agent now has all these capabilities automatically
}
```

## Architecture

The skills system consists of:

### Core Infrastructure
- **`skills::SkillBase`** - Abstract base class for all skills with parameter support
- **`skills::SkillManager`** - Handles loading/unloading and lifecycle management with parameters
- **`AgentBase::add_skill()`** - Simple method to add skills to agents with optional parameters

### Discovery & Registry
- **`skills::SkillRegistry`** - Global registry of skill factories
- **Auto-registration** - Built-in skills register themselves at link time via the `REGISTER_SKILL` macro
- **Validation** - Checks environment variables (`validate_env_vars()`); C++ links its dependencies at build time, so there is no runtime package check

### Built-in Skills
- **`web_search`** - Google Custom Search API integration with web scraping
- **`datetime`** - Current date/time information with timezone support
- **`math`** - Basic mathematical calculations

## Available Skills

### Web Search (`web_search`)
Search the internet and extract content from web pages.

**Requirements:**
- Environment variables: `GOOGLE_SEARCH_API_KEY`, `GOOGLE_SEARCH_ENGINE_ID`
- HTTP and HTML handling are provided by the SDK's vendored dependencies (no extra install)

**Parameters:**
- `num_results` (default: 1) - Number of search results to retrieve (1-10)
- `delay` (default: 0) - Delay in seconds between web requests

**Tools provided:**
- `web_search(query, num_results)` - Search and scrape web content

**Usage examples:**
```cpp
// Default: fast single result
agent.add_skill("web_search");

// Custom: multiple results with delay
agent.add_skill("web_search", {
    {"num_results", 3},
    {"delay", 0.5}
});

// Speed optimized: single result, no delay
agent.add_skill("web_search", {
    {"num_results", 1},
    {"delay", 0}
});
```

### Date/Time (`datetime`)  
Get current date and time information.

**Requirements:** None (timezone handling is built in)

**Parameters:** None (no configurable parameters)

**Tools provided:**
- `get_current_time(timezone)` - Current time in any timezone
- `get_current_date(timezone)` - Current date in any timezone

### Math (`math`)
Perform mathematical calculations.

**Requirements:** None

**Parameters:** None (no configurable parameters)

**Tools provided:**
- `calculate(expression)` - Evaluate mathematical expressions safely

### Native Vector Search (`native_vector_search`)
Search local document collections using vector similarity and keyword search.

**Requirements:** None beyond the SDK itself — the search backend is built into `libsignalwire.a`

**Parameters:**
- `tool_name` (default: "search_knowledge") - Custom name for the search tool
- `index_file` (optional) - Path to local `.swsearch` index file
- `remote_url` (optional) - URL of remote search server
- `index_name` (default: "default") - Index name on remote server
- `build_index` (default: False) - Auto-build index if missing
- `source_dir` (optional) - Source directory for auto-building
- `count` (default: 3) - Number of search results to return
- `distance_threshold` (default: 0.0) - Minimum similarity score
- `response_prefix` (optional) - Text to prepend to responses
- `response_postfix` (optional) - Text to append to responses

**Tools provided:**
- `search_knowledge(query, count)` - Search documents with hybrid vector/keyword search

**Usage examples:**
```cpp
// Local mode with auto-build from a docs directory
agent.add_skill("native_vector_search", {
    {"tool_name", "search_docs"},
    {"build_index", true},
    {"source_dir", "./docs"},  // Will build from directory
    {"index_file", "concepts.swsearch"}
});

// Or build from a specific pre-built index file
agent.add_skill("native_vector_search", {
    {"tool_name", "search_concepts"},
    {"index_file", "concepts.swsearch"}  // Pre-built index
});

// Remote mode
agent.add_skill("native_vector_search", {
    {"remote_url", "http://localhost:8001"},
    {"index_name", "knowledge"}
});

// Multiple instances for different document collections
agent.add_skill("native_vector_search", {
    {"tool_name", "search_examples"},
    {"index_file", "examples.swsearch"}
});
```

### SWML Transfer (`swml_transfer`)
Transfer calls between agents using pattern matching.

**Requirements:** None (no additional dependencies or environment variables required)

**Parameters:**
- `tool_name` (default: "transfer_call") - Custom name for the transfer function
- `description` (default: "Transfer call based on pattern matching") - Tool description
- `parameter_name` (default: "transfer_type") - Name of the parameter for the transfer function
- `parameter_description` (default: "The type of transfer to perform") - Parameter description
- `transfers` (required) - Dictionary mapping regex patterns to transfer configurations:
  - Pattern (key): Regex pattern to match (e.g., "/sales/i")
  - Configuration (value): Dictionary with:
    - `url` (required): Transfer destination URL
    - `message` (optional): Pre-transfer message
    - `return_message` (optional): Post-transfer message
    - `post_process` (optional, default: True): Enable post-processing
- `default_message` (default: "Please specify a valid transfer type.") - Message when no pattern matches
- `default_post_process` (default: False) - Post-processing flag for default case
- `required_fields` (default: {}) - Object mapping field names to descriptions for data collection before transfer

**Tools provided:**
- `transfer_call(transfer_type, ...required_fields)` (or custom tool_name) - Transfer based on pattern matching with optional required fields

**Usage examples:**
```cpp
// Simple transfer between departments
agent.add_skill("swml_transfer", {
    {"tool_name", "transfer_to_department"},
    {"transfers", {
        {"/sales/i", {
            {"url", "https://example.com/sales"},
            {"message", "Transferring to sales..."},
            {"return_message", "Sales transfer complete."}
        }},
        {"/support/i", {
            {"url", "https://example.com/support"},
            {"message", "Transferring to support..."},
            {"return_message", "Support transfer complete."}
        }}
    }}
});

// Multiple instances for different transfer types
agent.add_skill("swml_transfer", {
    {"tool_name", "route_call"},
    {"parameter_name", "department"},
    {"transfers", {
        {"/sales|billing/i", {
            {"url", "https://api.company.com/sales"},
            {"message", "Connecting to sales team..."},
            {"post_process", true}
        }},
        {"/technical|support/i", {
            {"url", "https://api.company.com/support"},
            {"message", "Connecting to support team..."},
            {"post_process", true}
        }}
    }},
    {"default_message", "Would you like sales or support?"}
});
```

## Usage Examples

### Basic Usage
```cpp
#include <signalwire/agent/agent_base.hpp>

int main() {
// Create agent and add skills
signalwire::agent::AgentBase agent("Assistant", "/assistant");
agent.add_skill("datetime");
agent.add_skill("math");
agent.add_skill("web_search");  // Uses defaults: 1 result, no delay

// Start the agent
agent.run();
}
```

### Skills with Custom Parameters
```cpp
#include <signalwire/agent/agent_base.hpp>

int main() {
// Create agent
signalwire::agent::AgentBase agent("Research Assistant", "/research");

// Add web search optimized for research (more results)
agent.add_skill("web_search", {
    {"num_results", 5},   // Get more comprehensive results
    {"delay", 1.0}        // Be respectful to websites
});

// Add other skills without parameters
agent.add_skill("datetime");
agent.add_skill("math");

// Start the agent
agent.run();
}
```

### Different Parameter Configurations
```cpp
// Speed-optimized for quick responses
agent.add_skill("web_search", {
    {"num_results", 1},
    {"delay", 0}
});

// Comprehensive research mode
agent.add_skill("web_search", {
    {"num_results", 5},
    {"delay", 1.0}
});

// Balanced approach
agent.add_skill("web_search", {
    {"num_results", 3},
    {"delay", 0.5}
});
```

### Check Available Skills
```cpp
// List all registered skills
for (const std::string& name : signalwire::skills::SkillRegistry::instance().list_skills()) {
    std::cout << "- " << name << "\n";
}

// Or get every skill's parameter schema keyed by name
json schema = signalwire::skills::SkillRegistry::instance().get_all_skills_schema();
```

### Runtime Skill Management
```cpp
#include <signalwire/agent/agent_base.hpp>
#include <iostream>

int main() {
signalwire::agent::AgentBase agent("Dynamic Agent");

// Add skills with different configurations
agent.add_skill("math");
agent.add_skill("datetime");
agent.add_skill("web_search", {{"num_results", 2}, {"delay", 0.3}});

// Check what's loaded
std::vector<std::string> loaded = agent.list_skills();

// Remove a skill
agent.remove_skill("math");

// Check if a specific skill is loaded
if (agent.has_skill("datetime")) {
    std::cout << "Date/time capabilities available\n";
}
}
```

## Creating Custom Skills

Create a new skill by extending `skills::SkillBase` with parameter support. Override the required virtuals and register the class with the `REGISTER_SKILL` macro:

```cpp
// my_skill.cpp
#include <signalwire/skills/skill_base.hpp>
#include <signalwire/skills/skill_registry.hpp>

namespace signalwire {
namespace skills {

class MyCustomSkill : public SkillBase {
 public:
    std::string skill_name() const override { return "my_skill"; }
    std::string skill_description() const override {
        return "Does something awesome with configurable parameters";
    }
    std::string skill_version() const override { return "1.0.0"; }
    std::vector<std::string> required_env_vars() const override { return {"API_KEY"}; }

    // Initialize the skill with parameters. Return true on success.
    bool setup(const json& params) override {
        params_ = params;
        if (!validate_env_vars() || !validate_packages()) {
            return false;
        }
        // Use parameters with defaults
        max_items_ = get_param<int>(params, "max_items", 10);
        timeout_ = get_param<int>(params, "timeout", 30);
        retry_count_ = get_param<int>(params, "retry_count", 3);
        return true;
    }

    // Register SWAIG tools with the agent.
    std::vector<swaig::ToolDefinition> register_tools() override {
        return {define_tool(
            "my_function", "Does something cool",
            json::object({{"type", "object"},
                          {"properties",
                           json::object({{"input",
                                          json::object({{"type", "string"},
                                                        {"description", "Input parameter"}})}})}}),
            [this](const json& args, const json&) -> swaig::FunctionResult {
                // Use max_items_, timeout_, retry_count_ in your logic
                return swaig::FunctionResult("Processed with max_items=" +
                                             std::to_string(max_items_));
            })};
    }

    // Speech recognition hints
    std::vector<std::string> get_hints() const override {
        return {"custom", "skill", "awesome"};
    }

    // Prompt sections to add to the agent
    std::vector<SkillPromptSection> get_prompt_sections() const override {
        return {{"Custom Capability",
                 "You can do custom things with my_skill.",
                 {}}};
    }

 private:
    int max_items_ = 10;
    int timeout_ = 30;
    int retry_count_ = 3;
};

REGISTER_SKILL(MyCustomSkill)

}  // namespace skills
}  // namespace signalwire
```

Once linked into your program, the skill is available by name:
```cpp
// Use defaults
agent.add_skill("my_skill");

// Use custom parameters
agent.add_skill("my_skill", {
    {"max_items", 20},
    {"timeout", 60},
    {"retry_count", 5}
});
```

## Quick Start

1. **Build the library** (all dependencies are vendored — no package manager needed):
   ```bash
   mkdir -p build && cd build && cmake .. && make -j$(nproc)
   ```

2. **Build and run the demo:**
   ```bash
   g++ -std=c++20 -I include -I deps examples/skills_demo.cpp -L build -lsignalwire -lssl -lcrypto -lpthread -o skills_demo
   ./skills_demo
   ```

3. **For web search, set environment variables:**
   ```bash
   export GOOGLE_SEARCH_API_KEY="your_api_key"
   export GOOGLE_SEARCH_ENGINE_ID="your_engine_id"
   ```

## Testing

Test the skills system with parameters — build and run a small program:

```cpp
#include <signalwire/agent/agent_base.hpp>
#include <signalwire/skills/skill_registry.hpp>
#include <iostream>

using namespace signalwire;

int main() {
    // Show registered skills
    for (const std::string& name : skills::SkillRegistry::instance().list_skills()) {
        std::cout << "Available skill: " << name << "\n";
    }

    // Create agent and load skills with parameters
    agent::AgentBase agent("Test", "/test");
    agent.add_skill("datetime");
    agent.add_skill("math");
    agent.add_skill("web_search", {{"num_results", 2}, {"delay", 0.5}});

    std::cout << "Loaded skills: " << agent.list_skills().size() << "\n";
    std::cout << "Skills system with parameters working!\n";
}
```

## Benefits

- **One-liner integration** - `agent.add_skill("skill_name")`
- **Configurable parameters** - `agent.add_skill("skill_name", {"param": "value"})`
- **Automatic discovery** - Drop skills in the directory and they're available
- **Dependency validation** - Checks packages and environment variables
- **Modular architecture** - Skills are self-contained and reusable
- **Extensible** - Easy to create custom skills with parameters
- **Clean separation** - Skills don't interfere with each other
- **Performance tuning** - Configure skills for speed vs. comprehensiveness

## Migration Guide

**Before (manual implementation):**
<!-- snippet: no-compile illustration referencing undefined params_json/handler placeholders -->
```cpp
// Had to manually implement every capability
class WebSearchAgent : public signalwire::agent::AgentBase {
public:
    WebSearchAgent() : AgentBase("WebSearchAgent") {
        // Wire up the search backend by hand
        define_tool("web_search", "Search the web", params_json, handler);
        // Lots of manual code...
    }
};
```

**After (skills system with parameters):**
```cpp
#include <signalwire/agent/agent_base.hpp>

int main() {
// Simple one-liner with custom configuration
signalwire::agent::AgentBase agent("WebSearchAgent");
agent.add_skill("web_search", {
    {"num_results", 3},  // Get more results
    {"delay", 0.5}       // Be respectful to servers
});
// Done! Full web search capability with custom settings.
}
```

The skills system makes SignalWire agents more modular, maintainable, and configurable. 