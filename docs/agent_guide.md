# SignalWire AI Agent Guide (C++)

## Table of Contents
- [Introduction](#introduction)
- [Architecture Overview](#architecture-overview)
- [Creating an Agent](#creating-an-agent)
- [Running Your Agent](#running-your-agent)
- [Prompt Building](#prompt-building)
- [SWAIG Functions (SignalWire AI Gateway)](#swaig-functions)
- [Skills System](#skills-system)
- [Multilingual Support](#multilingual-support)
- [Agent Configuration](#agent-configuration)
- [Dynamic Agent Configuration](#dynamic-agent-configuration)
- [Advanced Features](#advanced-features)
- [Prefab Agents](#prefab-agents)
- [Multi-Agent Hosting](#multi-agent-hosting)
- [API Reference](#api-reference)
- [Examples](#examples)

## Introduction

The `agent::AgentBase` class provides the foundation for creating AI-powered agents in the C++ SDK. It extends `swml::Service`, inheriting all of its SWML (SignalWire Markup Language) document creation and serving capabilities, while adding AI-specific functionality. SWML is the JSON document format that tells the SignalWire platform how an agent should behave during a call.

Key features of `AgentBase` include:

- Structured prompt building with POM (Prompt Object Model)
- SWAIG (SignalWire AI Gateway) function definitions -- SWAIG is the platform's AI tool-calling system with native access to the media stack
- Multilingual support
- Agent configuration (hint handling, pronunciation rules, etc.)
- Multi-step / multi-context conversation flows

This guide explains how to create and customize your own AI agents, with examples based on the SDK's sample implementations under `examples/`.

The header to include is:

<!-- snippet-setup -->
```cpp
#include <signalwire/agent/agent_base.hpp>
#include <signalwire/swaig/function_result.hpp>
#include <signalwire/swaig/parameter_schema.hpp>
#include <signalwire/datamap/datamap.hpp>
#include <signalwire/contexts/contexts.hpp>
#include <signalwire/prefabs/prefabs.hpp>
#include <signalwire/skills/skill_name.hpp>
#include <signalwire/server/agent_server.hpp>
#include <nlohmann/json.hpp>
#include <iostream>

using json = nlohmann::json;
signalwire::agent::AgentBase agent("my-agent");
```

In your own code you would write `using namespace signalwire;` so the `agent::`,
`swaig::`, and `datamap::` namespaces are in scope; the examples below spell the
namespaces out in full.

## Architecture Overview

The Agent SDK architecture consists of several layers:

1. **`swml::Service`**: The base layer for SWML document creation and serving
2. **`agent::AgentBase`**: Extends `swml::Service` with AI agent functionality
3. **Custom Agents**: Your specific agent implementations that extend `AgentBase`

Here's how these components relate to each other:

```
┌─────────────┐
│ Your Agent  │ (Extends AgentBase with your specific functionality)
└─────▲───────┘
      │
┌─────┴───────┐
│  AgentBase  │ (Adds AI functionality to swml::Service)
└─────▲───────┘
      │
┌─────┴───────┐
│ swml::Service │ (Provides SWML document creation and web service)
└─────────────┘
```

## Creating an Agent

To create an agent, subclass `agent::AgentBase` and configure it in the constructor. The constructor signature is `AgentBase(name = "agent", route = "/", host = "0.0.0.0", port = 3000)`:

```cpp
#include <signalwire/agent/agent_base.hpp>

using namespace signalwire;
using json = nlohmann::json;

class MyAgent : public agent::AgentBase {
public:
    MyAgent() : AgentBase("my-agent", "/agent", "0.0.0.0", 3000) {
        // Define agent personality and behavior
        prompt_add_section("Personality", "You are a helpful and friendly assistant.");
        prompt_add_section("Goal", "Help users with their questions and tasks.");
        prompt_add_section("Instructions", "", {
            "Answer questions clearly and concisely",
            "If you don't know, say so",
            "Use the provided tools when appropriate"
        });

        // Add a post-prompt for summary
        set_post_prompt("Please summarize the key points of this conversation.");
    }
};
```

POM (the Prompt Object Model) is enabled by default. You can toggle it with `set_use_pom(false)` if you prefer to set the prompt as raw text.

## Running Your Agent

`AgentBase` provides three lifecycle methods:

- `run()` — start the HTTP server (auto-detects a serverless environment when present, otherwise serves over HTTP)
- `serve()` — start the HTTP server directly
- `stop()` — stop the running server

<!-- snippet: no-compile depends on the MyAgent subclass defined in the preceding block -->
```cpp
int main() {
    MyAgent agent;
    std::cout << "Starting agent server at http://0.0.0.0:3000/agent\n";
    agent.run();
}
```

The agent exposes its SWML document at its route and handles SWAIG function calls and post-prompt delivery on the corresponding endpoints automatically.

## Prompt Building

There are several ways to build prompts for your agent.

### 1. Using Prompt Sections (POM)

The Prompt Object Model (POM) provides a structured way to build prompts. `prompt_add_section(title, body, bullets)` takes a title, an optional body string, and an optional vector of bullet strings:

```cpp
// Add a section with just body text
agent.prompt_add_section("Personality", "You are a friendly assistant.");

// Add a section with bullet points (empty body, then the bullets vector)
agent.prompt_add_section("Instructions", "", {
    "Answer questions clearly",
    "Be helpful and polite",
    "Use functions when appropriate"
});

// Add a section with both body and bullets
agent.prompt_add_section("Context",
    "The user is calling about technical support.",
    {"They may need help with their account", "Check for existing tickets"});
```

You can also nest a subsection under an existing section, or append to a section you have already created:

```cpp
agent.prompt_add_subsection("Instructions", "Escalation",
    "When to escalate to a human agent.",
    {"After two failed attempts", "On explicit request"});

agent.prompt_add_to_section("Instructions", "Always confirm the caller's identity first.");
```

### 2. Using Raw Text Prompts

For simpler agents, you can set the prompt directly as text. This switches the agent out of POM mode:

```cpp
agent.set_use_pom(false);
agent.set_prompt_text(
    "You are a helpful assistant. Your goal is to provide clear and concise "
    "information to the user. Answer their questions to the best of your ability.");
```

### 3. Setting a Post-Prompt

The post-prompt is sent to the AI after the conversation for summary or analysis:

```cpp
agent.set_post_prompt(
    "Analyze the conversation and extract:\n"
    "1. Main topics discussed\n"
    "2. Action items or follow-ups needed\n"
    "3. Whether the user's questions were answered satisfactorily");
```

## SWAIG Functions

SWAIG (SignalWire AI Gateway) functions allow the AI agent to perform actions and access external systems during a call. The AI decides when to call a function based on the conversation; SWAIG handles invocation, parameter passing, and delivering the result back to the AI.

### SWAIG functions ARE LLM tools — descriptions matter

Before writing your first SWAIG function, internalize this: a SWAIG function is **exactly the same concept** as a "tool" in native OpenAI / Anthropic tool calling. There is no separate "SWAIG layer" between your function and the model. Each SWAIG function is rendered into the OpenAI tool schema format on every turn:

```json
{
  "type": "function",
  "function": {
    "name":        "your_function_name",
    "description": "your description text",
    "parameters":  { /* your JSON schema */ }
  }
}
```

That schema is sent to the model as part of the same API call that produces the next assistant message. The model reads:

- the **function `description`** to decide WHEN to call this tool
- the **per-parameter `description` strings** inside `parameters` to decide HOW to fill in each argument

This means **descriptions are prompt engineering**, not developer documentation. They are instructions to the LLM that directly determine whether the model picks your tool when the user's request matches it.

Compare:

| Bad (model often misses the tool) | Good (model picks it reliably) |
|---|---|
| `description: "Lookup function"` | `description: "Look up a customer's account details by their account number. Use this BEFORE quoting any account-specific information (balance, plan, status, billing date). Don't use it for general product questions."` |
| `description: "the id"` (parameter) | `description: "The customer's 8-digit account number, no dashes or spaces. Ask the user if they don't provide it."` |

A vague description is the #1 cause of "the model has the right tool but doesn't call it" failures.

**Tool count matters too.** LLM tool selection accuracy degrades noticeably past ~7-8 simultaneously-active tools per call. If you have many tools, partition them across steps using `contexts::Step::set_functions(...)` so only the relevant subset is active at any moment. See the [Contexts](#multi-step-and-multi-context-flows) section.

### Defining a Tool

Register a tool with `define_tool(name, description, parameters, handler, secure = false)`. The `parameters` argument is a JSON-Schema object. The handler is a `swaig::ToolHandler`, which is `std::function<swaig::FunctionResult(const json& args, const json& raw_data)>`:

```cpp
agent.define_tool("get_weather", "Get the current weather for a location",
    {{"type", "object"}, {"properties", {
        {"location", {{"type", "string"}, {"description", "The city or location to get weather for"}}}
    }}},
    [](const json& args, const json& raw) -> signalwire::swaig::FunctionResult {
        (void)raw;
        std::string location = args.value("location", "Unknown location");
        // Here you would typically call a weather API; we return mock data.
        return signalwire::swaig::FunctionResult("It's sunny and 72F in " + location + ".");
    });
```

The handler receives the parsed function arguments (`args`) and the full raw request payload (`raw_data`).

### Building Parameter Schemas with `ParameterSchema`

Writing JSON Schema by hand is error-prone. The `swaig::ParameterSchema` builder produces byte-identical wire JSON via a typed fluent API and converts implicitly to `json`:

```cpp
auto params = signalwire::swaig::ParameterSchema{}
    .string("location", "The city or location to get weather for")
    .enum_of("units", {"celsius", "fahrenheit"}, "Temperature units")
    .required({"location"});

agent.define_tool("get_weather", "Get the current weather for a location", params,
    [](const json& args, const json& raw) -> signalwire::swaig::FunctionResult {
        (void)raw;
        return signalwire::swaig::FunctionResult("It's sunny in " + args.value("location", ""));
    });
```

`ParameterSchema` supports `string`, `integer`, `number`, `boolean`, `enum_of`, `array_of`, `object_of`, a raw `property(name, json)` escape hatch, and `required(...)` / `require(...)`.

### Function Parameters

The parameters for a SWAIG function are a JSON-Schema object. Each property can carry validation attributes:

```cpp
json parameters = {
    {"type", "object"},
    {"properties", {
        {"parameter_name", {
            {"type", "string"},          // string, number, integer, boolean, array, object
            {"description", "Description of the parameter"},
            {"enum", {"option1", "option2"}},  // for enumerated values
            {"pattern", "^[A-Z]+$"}            // for string validation
        }}
    }}
};
```

### Function Results

To return results from a SWAIG function, build a `swaig::FunctionResult`. Every action method returns `*this`, so calls chain:

```cpp
// Basic result with just text
signalwire::swaig::FunctionResult r1("Here's the result");

// Result with a single manual action
signalwire::swaig::FunctionResult r2 =
    signalwire::swaig::FunctionResult("Here's the result with an action")
    .add_action("say", "I found the information you requested.");

// Result with multiple manual actions
signalwire::swaig::FunctionResult r3 =
    signalwire::swaig::FunctionResult("Multiple actions example")
    .add_actions({
        {{"playback_bg", {{"file", "https://example.com/music.mp3"}}}},
        {{"set_global_data", {{"key", "value"}}}}
    });
```

`add_action(name, data)` adds a single action with the given name and data; `add_actions(actions)` adds several at once from a vector of JSON action objects. The full list of typed action helpers (`connect`, `send_sms`, `say`, `record_call`, and more) is documented in [swaig_reference.md](swaig_reference.md).

### Native Functions

The agent can enable SignalWire's built-in (native) functions:

```cpp
agent.set_native_functions({"check_time", "wait_seconds"});
```

### Function Includes

You can include functions from remote sources with `add_function_include(json)`:

```cpp
agent.add_function_include({
    {"url", "https://api.example.com/functions"},
    {"functions", {"get_weather", "get_news"}},
    {"meta_data", {{"session_id", "unique-session-123"}}}  // session tracking, NOT credentials
});
```

### SWAIG Function Security

SWAIG functions can be secured with a per-call token mechanism. Pass `secure = true` as the final argument to `define_tool(...)` for functions that should require a valid token:

```cpp
agent.define_tool("get_account_details", "Get customer account details",
    signalwire::swaig::ParameterSchema{}.string("account_id", "The customer's account ID").required({"account_id"}),
    [](const json& args, const json& raw) -> signalwire::swaig::FunctionResult {
        (void)raw;
        return signalwire::swaig::FunctionResult("Account " + args.value("account_id", "") + " is in good standing.");
    },
    /*secure=*/true);
```

When a function is marked secure, a token is generated and embedded in the function's URL when the SWML document is rendered, then validated before the function executes. These tokens are:

- **Stateless and self-contained**: the system does not store tokens or track sessions
- **Function-specific**: a token for one function cannot be used for another
- **Session-bound**: tied to a specific call/session ID
- **Cryptographically signed**: they cannot be tampered with or forged

The token system is backed by an HMAC-SHA256 `security::SessionManager`. You can mint and validate tokens directly:

```cpp
std::string call_id = "call-uuid";
std::string token = agent.create_tool_token("get_account_details", call_id);
bool ok = agent.validate_tool_token("get_account_details", token, call_id);
```

This stateless design lets tokens remain valid across server restarts and allows requests to be load-balanced across multiple servers without shared state.

## Skills System

The Skills System lets you extend agents with reusable capabilities via one-liner calls. Add a skill with `add_skill(name, params)`, where `params` is an optional JSON object:

```cpp
class SkillfulAgent : public signalwire::agent::AgentBase {
public:
    SkillfulAgent() : AgentBase("skillful-agent", "/skillful") {
        // Add skills with one-liners
        add_skill("web_search");   // Web search capability
        add_skill("datetime");     // Current date/time info
        add_skill("math");         // Mathematical calculations

        // Configure a skill with parameters
        add_skill("web_search", {
            {"num_results", 3},  // Get 3 search results instead of the default
            {"delay", 0.5}       // Add delay between requests
        });
    }
};
```

### Typed Skill Names

For built-in skills you can use the `skills::SkillName` enum instead of a bare string. The enum gives editor autocompletion and catches typos at the call site; it loads the identical skill as the string form:

```cpp
agent.add_skill(signalwire::skills::SkillName::Datetime);   // typed, autocompleted
agent.add_skill("datetime");                                // string still works
agent.add_skill("my_custom_skill");                         // open set: custom skills are fine
```

### Skill Management

```cpp
// Check what skills are loaded
std::vector<std::string> loaded = agent.list_skills();

// Check if a specific skill is loaded
if (agent.has_skill("web_search")) {
    // Web search is available
}

// Remove a skill
agent.remove_skill("math");
```

### Configuring Skills with Parameters

Skill parameters are passed as a JSON object. For example, the web-search skill accepts an API key, a search-engine ID, a result count, a delay, and a custom tool name:

```cpp
agent.add_skill("web_search", {
    {"api_key", "your-google-api-key"},
    {"search_engine_id", "your-search-engine-id"},
    {"num_results", 3},
    {"delay", 0.5},
    {"tool_name", "search_news"}  // custom name for the registered tool
});
```

Choose parameters for your use case — a single fast result for customer service, or several results with a delay for research:

```cpp
agent.add_skill("web_search", {{"num_results", 1}, {"delay", 0}});    // for speed
agent.add_skill("web_search", {{"num_results", 5}, {"delay", 1.0}});  // for research
```

## Multilingual Support

Agents can support multiple languages. `add_language(LanguageConfig)` takes a brace-initialized `agent::LanguageConfig` whose fields are `{name, code, voice, engine, fillers, params}`. Only `name`, `code`, and `voice` are required:

```cpp
// Simple form: name, code, voice
agent.add_language({"English", "en-US", "inworld.Mark"});
agent.add_language({"Spanish", "es", "rime.spore:multilingual"});

// With an explicit engine and per-language params
agent.add_language({"British English", "en-GB", "spore", "rime"});
```

You can replace the whole list with `set_languages(...)`, and set engine-specific tuning on an already-added language with `set_language_params(code, params)`:

```cpp
agent.set_languages({
    {"English", "en-US", "inworld.Mark"},
    {"Spanish", "es", "inworld.Sarah"}
});

agent.set_language_params("en-US", {{"speed", 1.1}});
```

## Agent Configuration

### Adding Hints

Hints help the AI recognize specific terms:

```cpp
// Simple hints
agent.add_hints({"SignalWire", "SWML", "SWAIG"});

// A single hint
agent.add_hint("DataSphere");

// A pattern hint: (hint, pattern, replace, ignore_case=false)
agent.add_pattern_hint("AI Agent", "AI\\s+Agent", "AI Agent", true);
```

### Adding Pronunciation Rules

Pronunciation rules help the AI speak certain terms correctly. `add_pronunciation(replace, with, ignore_case)`:

```cpp
agent.add_pronunciation("API", "A P I", false);
agent.add_pronunciation("SIP", "sip", true);
```

### Setting AI Parameters

Configure AI behavior parameters with `set_params(json)` (or `set_param(key, value)` for one at a time):

```cpp
agent.set_params({
    {"wait_for_user", false},
    {"end_of_speech_timeout", 1000},
    {"ai_volume", 5},
    {"languages_enabled", true},
    {"local_tz", "America/Los_Angeles"}
});
```

### Setting Global Data

Provide global data for the AI to reference. `set_global_data(json)` replaces the data; `update_global_data(json)` merges into it:

```cpp
agent.set_global_data({
    {"company_name", "SignalWire"},
    {"product", "AI Agent SDK"},
    {"supported_features", {"Voice AI", "Telephone integration", "SWAIG functions"}}
});

agent.update_global_data({{"service_level", "premium"}});
```

### Customizing LLM Parameters

`set_prompt_llm_params(json)` and `set_post_prompt_llm_params(json)` fine-tune the language model for the main prompt and the post-prompt respectively. The parameters are passed to the server, which validates them based on the model:

```cpp
// Main prompt
agent.set_prompt_llm_params({
    {"temperature", 0.7},        // Controls randomness
    {"top_p", 0.9},              // Nucleus sampling threshold
    {"barge_confidence", 0.6},   // ASR confidence to interrupt
    {"presence_penalty", 0.0},
    {"frequency_penalty", 0.0}
});

// Post-prompt (lower temperature for consistent summaries)
agent.set_post_prompt_llm_params({
    {"temperature", 0.3},
    {"top_p", 0.95}
});
```

**Common use cases:**

- **Customer Service**: low temperature (0.2-0.4) for consistent, professional responses
- **Creative Tasks**: higher temperature (0.7-0.9) for varied outputs
- **Technical Support**: very low temperature (0.1-0.3) for accuracy
- **General Assistant**: medium temperature (0.5-0.7) for balanced interaction

### Internal Fillers

Internal fillers are short phrases the AI speaks while a native function is running, so the caller does not hear dead air. Set them per native function and language with `add_internal_filler(function_name, language_code, phrases)` or in bulk with `set_internal_fillers(json)`:

```cpp
agent.add_internal_filler("check_time", "en-US", {"Let me check the time...", "One moment..."});

agent.set_internal_fillers({
    {"wait_seconds", {{"en-US", {"Hold on a second..."}}}}
});
```

Only the runtime's supported internal function names accept fillers (e.g. `hangup`, `check_time`, `wait_for_user`, `wait_seconds`, `next_step`, `change_context`); unknown names log a warning at registration time.

## Dynamic Agent Configuration

Dynamic agent configuration lets you configure an agent per-request based on the incoming HTTP request (query parameters, body data, headers). This enables multi-tenant applications, A/B testing, personalization, and localization.

### Static vs. Dynamic

With **static** configuration, everything is set once in the constructor and is identical for every caller.

With **dynamic** configuration, you register a callback that runs fresh for each request and configures a per-request copy of the agent. Register it with `set_dynamic_config_callback(cb)`, where `cb` is an `agent::DynamicConfigCallback`:

<!-- snippet: no-compile type-alias reference illustration for signalwire::agent::DynamicConfigCallback -->
```cpp
using agent::DynamicConfigCallback = std::function<void(
    const std::map<std::string, std::string>& query_params,
    const json& body_params,
    const std::map<std::string, std::string>& headers,
    agent::AgentBase& agent_copy)>;
```

### Setting Up Dynamic Configuration

```cpp
class DynamicAgent : public signalwire::agent::AgentBase {
public:
    DynamicAgent() : AgentBase("dynamic-agent", "/agent") {
        set_dynamic_config_callback(
            [](const std::map<std::string, std::string>& query_params,
               const json& body_params,
               const std::map<std::string, std::string>& headers,
               signalwire::agent::AgentBase& agent) {
                (void)body_params; (void)headers;

                // Look up a query parameter with a safe default
                std::string tier = "standard";
                if (auto it = query_params.find("tier"); it != query_params.end()) {
                    tier = it->second;
                }

                if (tier == "premium") {
                    agent.add_language({"English", "en-US", "rime.spore:mistv2"});
                    agent.set_params({{"end_of_speech_timeout", 300}});
                    agent.prompt_add_section("Role", "You are a premium customer service agent.");
                    agent.set_global_data({{"service_level", "premium"}});
                } else {
                    agent.add_language({"English", "en-US", "rime.spore:mistv2"});
                    agent.set_params({{"end_of_speech_timeout", 500}});
                    agent.prompt_add_section("Role", "You are a customer service agent.");
                    agent.set_global_data({{"service_level", "standard"}});
                }
            });
    }
};
```

The callback receives four arguments:
- **`query_params`**: a `std::map<std::string,std::string>` of URL query parameters
- **`body_params`**: the parsed JSON body (empty for GET requests)
- **`headers`**: a `std::map<std::string,std::string>` of HTTP headers
- **`agent`**: the per-request `AgentBase` copy to configure

Inside the callback you can call any of the same configuration methods you would use in the constructor: `add_language`, `prompt_add_section`, `set_params`, `set_global_data`, `update_global_data`, `add_hints`, `add_pronunciation`, `set_native_functions`, `add_function_include`, `add_skill`, and so on.

### Request Data Access

Because `query_params` and `headers` are `std::map`, read them with `find` and provide defaults:

```cpp
std::map<std::string, std::string> query_params;
json body_params;

std::string tier = "standard";
if (auto it = query_params.find("tier"); it != query_params.end()) {
    tier = it->second;
}

// Body params are JSON
std::string voice_speed = body_params.value("/preferences/voice_speed"_json_pointer, "normal");
```

### Best Practices

1. **Keep callbacks lightweight** — extract parameters and apply pre-computed configuration; avoid heavy work or blocking external calls in the hot path.
2. **Always provide defaults** — coerce unknown values to a safe default (e.g. an unknown `tier` becomes `"standard"`).
3. **Validate input** — clamp numeric parameters to a sensible range and reject unexpected values.
4. **Do not expose sensitive configuration via parameters** — never copy credentials from query parameters into global data.

## Advanced Features

### Debug Events

The debug events system POSTs structured JSON events to your agent throughout the call lifecycle — session start/end, barge interruptions, LLM errors, step changes, and more. Enable it with `enable_debug_events()`:

```cpp
#include <signalwire/agent/agent_base.hpp>

int main() {
    signalwire::agent::AgentBase agent("my_agent");
    agent.enable_debug_events();  // events are auto-logged
    agent.serve();
}
```

To act on specific events, register a handler with `on_debug_event(cb)`, where `cb` is a `agent::DebugEventCallback` (`std::function<void(const json& event)>`):

```cpp
agent.enable_debug_events();
agent.on_debug_event([](const json& event) {
    std::string type = event.value("type", "");
    std::string call_id = event.value("call_id", "");

    if (type == "barge") {
        std::cout << "[" << call_id << "] Caller interrupted after "
                  << event.value("barge_elapsed_ms", 0) << "ms\n";
    } else if (type == "llm_error") {
        std::cout << "[" << call_id << "] LLM error\n";
    } else if (type == "session_end") {
        std::cout << "[" << call_id << "] Call ended\n";
    }
});
agent.serve();
```

The handler is called for every event in addition to the default structured logging.

### Session Lifecycle Hooks

SignalWire calls two specially-named SWAIG functions automatically at the start and end of a voice session:
- `startup_hook`: called immediately when a new voice session begins
- `hangup_hook`: called when a voice session ends

Implement them as ordinary tools whose names are exactly `startup_hook` and `hangup_hook`:

```cpp
agent.define_tool("startup_hook", "Called when the voice session starts",
    {{"type", "object"}, {"properties", json::object()}},
    [](const json& args, const json& raw) -> signalwire::swaig::FunctionResult {
        (void)args;
        std::string call_id = raw.value("call_id", "");
        std::cout << "Session started: " << call_id << "\n";
        return signalwire::swaig::FunctionResult("Session initialized successfully");
    });

agent.define_tool("hangup_hook", "Called when the voice session ends",
    {{"type", "object"}, {"properties", json::object()}},
    [](const json& args, const json& raw) -> signalwire::swaig::FunctionResult {
        (void)args;
        std::string call_id = raw.value("call_id", "");
        std::cout << "Session ended: " << call_id << "\n";
        return signalwire::swaig::FunctionResult("Session cleanup completed");
    });
```

Both hooks must return a `swaig::FunctionResult`. `startup_hook` is called before the AI starts speaking to the caller.

### Multi-Step and Multi-Context Flows

For workflows that move through distinct phases, use the contexts API. `define_contexts()` returns a `contexts::ContextBuilder`; add a context, then add ordered steps to it. Each step carries its own task prompt, completion criteria, and (optionally) a whitelist of which functions are callable while it is active:

```cpp
auto& ctx = agent.define_contexts().add_context("default");

ctx.add_step("greeting")
   .add_section("Task", "Greet the caller and ask how you can help.")
   .set_step_criteria("The caller has stated their need.")
   .set_valid_steps({"resolve"});

ctx.add_step("resolve")
   .add_section("Task", "Resolve the caller's request using the available tools.")
   .set_functions(std::vector<std::string>{"get_weather"})
   .set_step_criteria("The request is resolved.");
```

Keep the per-step active set small — LLM tool selection accuracy degrades past ~7-8 active tools. If a step does not call `set_functions`, it **inherits** the previous step's active set, which is a common source of leaked tools.

`reset_contexts()` removes all contexts and returns the agent to a no-contexts state.

### SIP Routing

Enable SIP routing so the agent can receive voice calls via SIP addresses:

```cpp
// Enable SIP routing and auto-map usernames from the agent name/route
agent.enable_sip_routing(true);
agent.auto_map_sip_usernames(true);

// Register additional SIP usernames for this agent
agent.register_sip_username("support_agent");
agent.register_sip_username("help_desk");
```

### Authentication and Webhook Signing

Protect the agent's endpoints with HTTP basic auth, and validate inbound webhook signatures with a signing key:

```cpp
agent.set_auth("admin", "secret");

// Set the SignalWire Signing Key (Dashboard → API Credentials).
// When set, the server validates the signature on POST `/`, `/swaig`,
// and `/post_prompt`; unsigned or wrongly-signed requests get a 403.
agent.set_signing_key("your-signing-key");
```

If no signing key is set explicitly, the agent falls back to the `SIGNALWIRE_SIGNING_KEY` environment variable.

### Custom Webhook URLs

Override the default URLs for SWAIG function delivery and post-prompt delivery:

```cpp
// Send function calls to an external service instead of handling them locally
agent.set_webhook_url("https://external-service.example.com/handle-swaig");

// Send conversation summaries to an analytics service
agent.set_post_prompt_url("https://analytics.example.com/conversation-summaries");
```

You can also append extra query parameters to every SWAIG URL with `add_swaig_query_param(key, value)` (and clear them with `clear_swaig_query_params()`).

### Conversation Summary Handling

Register a callback to process conversation summaries posted to the agent. `on_summary(cb)` takes a `agent::SummaryCallback` (`std::function<void(const json& summary, const json& raw_data)>`):

```cpp
agent.on_summary([](const json& summary, const json& raw_data) {
    (void)raw_data;
    if (summary != nullptr) {
        std::cout << "Conversation summary: " << summary.dump() << "\n";
        // Save to a database, send notifications, etc.
    }
});
```

### MCP Integration

The agent can connect to Model Context Protocol (MCP) servers. Register a server with `add_mcp_server(url, ...)` and enable the integration with `enable_mcp_server()`:

```cpp
agent.add_mcp_server("https://mcp.example.com");
agent.enable_mcp_server(true);
```

## Prefab Agents

Prefab agents are pre-configured implementations for common use cases. They live in `signalwire::prefabs` and subclass `AgentBase`, so they accept the same `(name, route, host, port)` constructor and expose fluent setters.

```cpp
#include <signalwire/prefabs/prefabs.hpp>

using namespace signalwire;
```

#### InfoGathererAgent

Collects structured information from users:

```cpp
signalwire::prefabs::InfoGathererAgent gatherer("info-gatherer", "/info-gatherer");
gatherer.set_questions({
    {{"name", "full_name"}, {"prompt", "What is your full name?"}},
    {{"name", "email"}, {"prompt", "What is your email address?"}},
    {{"name", "reason"}, {"prompt", "How can I help you today?"}}
});
gatherer.set_completion_message("Thanks! I'll help you with your request.");
gatherer.run();
```

#### SurveyAgent

Conducts structured surveys with different question types:

```cpp
signalwire::prefabs::SurveyAgent survey("satisfaction-survey", "/survey");
survey.set_intro_message("We'd like to know about your recent experience.");
survey.set_questions({
    {{"id", "satisfaction"}, {"text", "How satisfied are you?"}, {"type", "rating"}, {"scale", 5}},
    {{"id", "feedback"}, {"text", "Any specific feedback?"}, {"type", "text"}}
});
survey.run();
```

#### ReceptionistAgent

Handles call routing and department transfers:

```cpp
signalwire::prefabs::ReceptionistAgent reception("acme-receptionist", "/reception");
reception.set_greeting("Thank you for calling ACME Corp. How may I direct your call?");
reception.set_departments({
    {{"name", "sales"}, {"description", "Product inquiries and pricing"}, {"number", "+15551235555"}},
    {{"name", "support"}, {"description", "Technical assistance"}, {"number", "+15551236666"}}
});
reception.run();
```

#### FAQBotAgent

Answers questions using keyword-matched FAQ entries:

```cpp
signalwire::prefabs::FAQBotAgent faqbot("knowledge-base", "/knowledge-base");
faqbot.set_faqs({
    {{"question", "What are your hours?"}, {"answer", "We are open 9am-5pm ET."}}
});
faqbot.set_no_match_message("I'm not sure about that one — let me connect you to a person.");
faqbot.run();
```

#### ConciergeAgent

Provides venue and amenity information:

```cpp
signalwire::prefabs::ConciergeAgent concierge("concierge", "/concierge");
concierge.set_venue_name("Grand Hotel");
concierge.set_amenities({
    {{"name", "Pool"}, {"description", "Open 6am-10pm on the roof deck."}}
});
concierge.run();
```

### Creating Your Own Prefabs

You can create your own prefab by subclassing `AgentBase` (or any existing prefab) and applying configuration in the constructor:

```cpp
class CustomerSupportAgent : public signalwire::agent::AgentBase {
public:
    explicit CustomerSupportAgent(const std::string& product_name)
        : AgentBase("voice-support", "/voice-support") {
        prompt_add_section("Personality", "I am a customer support agent for " + product_name + ".");
        prompt_add_section("Goal", "Help customers solve their problems effectively.");
        prompt_add_section("Instructions", "", {
            "Be professional but friendly.",
            "Verify the customer's identity before sharing account details."
        });

        define_tool("escalate_issue", "Escalate a customer issue to a human agent",
            signalwire::swaig::ParameterSchema{}
                .string("issue_summary", "Brief summary of the issue")
                .string("customer_email", "Customer's email address")
                .required({"issue_summary"}),
            [](const json& args, const json& raw) -> signalwire::swaig::FunctionResult {
                (void)args; (void)raw;
                return signalwire::swaig::FunctionResult("Issue escalated successfully.");
            });
    }
};
```

## Multi-Agent Hosting

To host several agents in one process, use `server::AgentServer`. Register each agent on its own route and serve them all from a single port:

```cpp
#include <signalwire/server/agent_server.hpp>
#include <signalwire/agent/agent_base.hpp>
#include <memory>

using namespace signalwire;

int main() {
    auto registration_agent = std::make_shared<agent::AgentBase>("registration", "/register");
    auto support_agent = std::make_shared<agent::AgentBase>("support", "/support");

    server::AgentServer srv("0.0.0.0", 3000);

    srv.register_agent(registration_agent, "/register");
    srv.register_agent(support_agent, "/support");

    // Central SIP routing across all hosted agents
    srv.enable_sip_routing();
    srv.map_sip_username("signup", "/register");  // signup@domain → registration agent
    srv.map_sip_username("help", "/support");     // help@domain → support agent

    srv.run();
}
```

`AgentServer` also exposes `list_routes()`, `unregister_agent(...)`, `set_static_dir(...)`, and `stop()`.

## API Reference

### Constructor

<!-- snippet: no-compile constructor-signature reference (declaration illustration) -->
```cpp
AgentBase(const std::string& name = "agent",
          const std::string& route = "/",
          const std::string& host = "0.0.0.0",
          int port = 3000);
```

### Prompt Methods

- `set_prompt_text(text)` / `set_use_pom(bool)` / `set_prompt_pom(vector<json>)`
- `set_post_prompt(text)` / `set_post_prompt_url(url)`
- `prompt_add_section(title, body = "", bullets = {})`
- `prompt_add_subsection(parent_title, title, body = "", bullets = {})`
- `prompt_add_to_section(title, body = "", bullets = {})`

### SWAIG / Tool Methods

- `define_tool(name, description, parameters, handler, secure = false)`
- `define_tool(const swaig::ToolDefinition&)`
- `register_swaig_function(json)`
- `list_tools()`
- `set_native_functions(funcs)` / `add_function_include(json)` / `set_function_includes(vector<json>)`
- `create_tool_token(tool_name, call_id)` / `validate_tool_token(function_name, token, call_id)`

### AI Configuration Methods

- `add_hint(hint)` / `add_hints(hints)` / `add_pattern_hint(pattern)`
- `add_pronunciation(replace, with, ignore_case = false)` / `set_pronunciations(vector)`
- `add_language(LanguageConfig)` / `set_languages(vector)` / `set_language_params(code, json)`
- `set_param(key, value)` / `set_params(json)`
- `set_global_data(json)` / `update_global_data(json)`
- `set_prompt_llm_params(json)` / `set_post_prompt_llm_params(json)`
- `set_internal_fillers(json)` / `add_internal_filler(function_name, language_code, fillers)`

### Skill Methods

- `add_skill(name, params = {})` / `add_skill(skills::SkillName, params = {})`
- `remove_skill(name)` / `has_skill(name)` / `list_skills()`

### Context Methods

- `define_contexts()` → `contexts::ContextBuilder&`
- `add_context(name)` / `has_contexts()` / `reset_contexts()`

### Verb Methods (5-phase pipeline)

- `add_pre_answer_verb(name, json)` / `add_answer_verb(name, json)`
- `add_post_answer_verb(name, json)` / `add_post_ai_verb(name, json)`
- `clear_pre_answer_verbs()` / `clear_post_answer_verbs()` / `clear_post_ai_verbs()`

### Web / SIP / Auth Methods

- `set_dynamic_config_callback(cb)`
- `set_webhook_url(url)` / `set_post_prompt_url_direct(url)`
- `add_swaig_query_param(key, value)` / `clear_swaig_query_params()`
- `enable_debug_routes(bool)` / `enable_debug_events(bool)`
- `enable_sip_routing(bool)` / `register_sip_username(name)` / `auto_map_sip_usernames(bool)`
- `set_auth(user, pass)` / `set_signing_key(key)`
- `add_mcp_server(url, ...)` / `enable_mcp_server(bool)`

### Lifecycle / Callbacks

- `run()` / `serve()` / `stop()`
- `on_summary(SummaryCallback)` / `on_debug_event(DebugEventCallback)`
- `render_swml()` / `render_swml_for_request(query_params, body_params, headers)`

## Examples

### Simple Question-Answering Agent

```cpp
#include <signalwire/agent/agent_base.hpp>
#include <ctime>

using namespace signalwire;
using json = nlohmann::json;

class SimpleAgent : public agent::AgentBase {
public:
    SimpleAgent() : AgentBase("simple", "/simple") {
        prompt_add_section("Personality", "You are a friendly and helpful assistant.");
        prompt_add_section("Goal", "Help users with basic tasks and answer questions.");
        prompt_add_section("Instructions", "", {
            "Be concise and direct in your responses.",
            "If you don't know something, say so clearly.",
            "Use the get_time function when asked about the current time."
        });

        define_tool("get_time", "Get the current time",
            {{"type", "object"}, {"properties", json::object()}},
            [](const json& args, const json& raw) -> swaig::FunctionResult {
                (void)args; (void)raw;
                auto t = std::time(nullptr);
                char buf[32];
                std::strftime(buf, sizeof(buf), "%H:%M:%S", std::localtime(&t));
                return swaig::FunctionResult(std::string("The current time is ") + buf);
            });
    }
};

int main() {
    SimpleAgent agent;
    std::cout << "Starting agent server at http://0.0.0.0:3000/simple\n";
    agent.run();
}
```

### Multi-Language Customer Service Agent

```cpp
class CustomerServiceAgent : public signalwire::agent::AgentBase {
public:
    CustomerServiceAgent() : AgentBase("customer-service", "/support") {
        prompt_add_section("Personality",
            "You are a helpful customer service representative for SignalWire.");
        prompt_add_section("Knowledge",
            "You can answer questions about SignalWire products and services.");
        prompt_add_section("Instructions", "", {
            "Greet customers politely",
            "Use check_account_status when the customer asks about their account",
            "Use create_support_ticket for unresolved issues"
        });

        // Language support
        add_language({"English", "en-US", "en-US-Neural2-F"});
        add_language({"Spanish", "es", "rime.spore:multilingual"});
        set_params({{"languages_enabled", true}});

        // Company information
        set_global_data({
            {"company_name", "SignalWire"},
            {"support_hours", "9am-5pm ET, Monday through Friday"},
            {"support_email", "support@signalwire.com"}
        });

        define_tool("check_account_status", "Check the status of a customer's account",
            signalwire::swaig::ParameterSchema{}
                .string("account_id", "The customer's account ID")
                .required({"account_id"}),
            [](const json& args, const json& raw) -> signalwire::swaig::FunctionResult {
                (void)raw;
                std::string id = args.value("account_id", "");
                return signalwire::swaig::FunctionResult("Account " + id + " is in good standing.");
            });

        define_tool("create_support_ticket", "Create a support ticket for an unresolved issue",
            signalwire::swaig::ParameterSchema{}
                .string("issue", "Brief description of the issue")
                .enum_of("priority", {"low", "medium", "high", "critical"}, "Ticket priority")
                .required({"issue"}),
            [](const json& args, const json& raw) -> signalwire::swaig::FunctionResult {
                (void)raw;
                std::string priority = args.value("priority", "medium");
                return signalwire::swaig::FunctionResult(
                    "Support ticket created with " + priority + " priority. "
                    "A representative will contact you shortly.");
            });
    }
};

int main() {
    CustomerServiceAgent agent;
    std::cout << "Starting customer service agent...\n";
    agent.run();
}
```

For more examples, see the `examples/` directory in the SDK repository — in particular `examples/simple_agent.cpp` and `examples/swaig_features_agent.cpp`.
