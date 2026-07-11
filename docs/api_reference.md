# SignalWire AI Agents SDK - Complete API Reference

This document provides a comprehensive reference for all public APIs in the SignalWire AI Agents SDK.

## Table of Contents

1. [AgentBase Class](#agentbase-class) - Core agent functionality
2. [FunctionResult Class](#functionresult-class) - SWAIG (SignalWire AI Gateway) function response handling
3. [DataMap Class](#datamap-class) - Serverless API tools that execute on SignalWire's servers
4. [Context System](#context-system) - Structured workflows
5. [State Management](#state-management) - Persistent state
6. [Skills System](#skills-system) - Modular capabilities
7. [Utility Classes](#utility-classes) - Supporting classes

---

## AgentBase Class

The `agent::AgentBase` class is the foundation for creating AI agents. It extends `swml::Service` (the base class for generating SWML -- SignalWire Markup Language -- documents) and provides comprehensive functionality for building conversational AI agents.

The header to include is:

<!-- snippet-setup -->
```cpp
#include <signalwire/agent/agent_base.hpp>
#include <signalwire/swaig/function_result.hpp>
#include <signalwire/swaig/parameter_schema.hpp>
#include <signalwire/datamap/datamap.hpp>
#include <signalwire/contexts/contexts.hpp>
#include <signalwire/skills/skill_base.hpp>
#include <signalwire/skills/skill_name.hpp>
#include <signalwire/skills/skill_registry.hpp>
#include <signalwire/utils/serverless.hpp>
#include <nlohmann/json.hpp>
#include <iostream>

using json = nlohmann::json;
signalwire::agent::AgentBase agent("my-agent");
signalwire::swaig::FunctionResult result("ok");
signalwire::datamap::DataMap data_map("tool");
```

In your own code you would write `using namespace signalwire;` so `agent::`, `swaig::`,
and `datamap::` are in scope; the examples below spell the namespaces out in full.

### Constructor

<!-- snippet: no-compile constructor signature listing (class-member declaration shown out of class context) -->
```cpp
explicit AgentBase(const std::string& name = "agent",
                   const std::string& route = "/",
                   const std::string& host = "0.0.0.0",
                   int port = 3000);
```

**Parameters:**
- `name` (`std::string`): Human-readable name for the agent (default: `"agent"`)
- `route` (`std::string`): HTTP route path for the agent (default: `"/"`)
- `host` (`std::string`): Host address to bind to (default: `"0.0.0.0"`)
- `port` (`int`): Port number to listen on (default: `3000`)

You normally create an agent by subclassing `agent::AgentBase` and configuring it in the constructor:

```cpp
class MyAgent : public signalwire::agent::AgentBase {
public:
    MyAgent() : AgentBase("my-agent", "/agent", "0.0.0.0", 3000) {
        prompt_add_section("Personality", "You are a helpful assistant.");
    }
};
```

The Prompt Object Model (POM) is enabled by default; toggle it with `set_use_pom(false)`. Recording, native functions, custom schema paths, and webhook URLs are configured after construction via the fluent setters documented below (`set_native_functions`, `set_webhook_url`, etc.) rather than as constructor arguments. Basic auth is set with `set_auth(username, password)`; the webhook signing key with `set_signing_key(key)`.

### Core Methods

#### Deployment and Execution

##### `run()`
Start the agent. Auto-detects a serverless environment when one is present (dispatching via `handle_serverless_request`), otherwise serves over HTTP on the constructor's host/port.

**Usage:**
```cpp
agent.run();  // serve over HTTP, or dispatch a serverless request when detected
```

For serverless platforms, dispatch a single request explicitly and inspect the response:

```cpp
json event = json::object();    // the platform's request event
json context = json::object();  // the platform's invocation context
signalwire::utils::ServerlessResponse resp = agent.handle_serverless_request(event, context);
```

`handle_serverless_request(event, context, mode)` auto-detects the platform (lambda / google_cloud_function / azure_function / cgi) when `mode` is empty, or forces a specific handler when `mode` is set.

##### `serve()`
Explicitly run as an HTTP server. Binds to the host/port supplied to the constructor.

**Usage:**
```cpp
agent.serve();  // use constructor host/port
```

Call `stop()` to shut the running server down.

### Prompt Configuration

#### Text-Based Prompts

##### `set_prompt_text(const std::string& text) -> AgentBase&`
Set the agent's prompt as raw text (switches the agent out of POM mode).

**Parameters:**
- `text` (`std::string`): The complete prompt text

**Usage:**
```cpp
agent.set_prompt_text("You are a helpful customer service agent.");
```

##### `set_post_prompt(const std::string& text) -> AgentBase&`
Set text sent to the AI after the conversation for summary/analysis.

**Parameters:**
- `text` (`std::string`): The post-prompt text

**Usage:**
```cpp
agent.set_post_prompt("Always be polite and professional.");
```

#### LLM Parameter Configuration

##### `set_prompt_llm_params`

```cpp
signalwire::agent::AgentBase& set_prompt_llm_params(const json& params = json::object());
```
Set Language Model parameters for the main prompt. Accepts any parameters which will be passed through to the SignalWire server. The server validates and applies parameters based on the target model's capabilities.

**Common Parameters:**
- `temperature`: Controls randomness. Lower = more focused
- `top_p`: Nucleus sampling threshold
- `barge_confidence`: ASR confidence to interrupt
- `presence_penalty`: Topic diversity control
- `frequency_penalty`: Repetition control

Note: No defaults are sent unless explicitly set. Invalid parameters for the selected model will be handled/ignored by the server.

**Usage:**
```cpp
// Configure for consistent, professional responses
agent.set_prompt_llm_params({
    {"temperature", 0.3},
    {"top_p", 0.9},
    {"barge_confidence", 0.7},
    {"presence_penalty", 0.1},
    {"frequency_penalty", 0.2}
});
```

##### `set_post_prompt_llm_params`

```cpp
signalwire::agent::AgentBase& set_post_prompt_llm_params(const json& params = json::object());
```
Set Language Model parameters for the post-prompt. Accepts any parameters which will be passed through to the SignalWire server. The server validates and applies parameters based on the target model's capabilities.

**Common Parameters:**
- `temperature`: Controls randomness. Lower = more focused
- `top_p`: Nucleus sampling threshold
- `presence_penalty`: Topic diversity control
- `frequency_penalty`: Repetition control

Note: barge_confidence is not applicable to post-prompt. No defaults are sent unless explicitly set.

**Usage:**
```cpp
// Configure for focused summaries
agent.set_post_prompt_llm_params({
    {"temperature", 0.2},
    {"top_p", 0.9}
});
```

#### Structured Prompts (POM)

##### `prompt_add_section`

```cpp
signalwire::agent::AgentBase& prompt_add_section(const std::string& title,
                              const std::string& body = "",
                              const std::vector<std::string>& bullets = {});
```
Add a structured section to the prompt using the Prompt Object Model.

**Parameters:**
- `title` (`std::string`): Section title/heading
- `body` (`std::string`): Main section content (default: `""`)
- `bullets` (`std::vector<std::string>`): List of bullet points (default: empty)

**Usage:**
```cpp
// Simple section
agent.prompt_add_section("Role", "You are a customer service representative.");

// Section with bullets (empty body, then the bullets vector)
agent.prompt_add_section("Guidelines", "Follow these principles:",
    {"Be helpful", "Stay professional", "Listen carefully"});

// A process section with ordered steps
agent.prompt_add_section("Process", "Follow these steps:",
    {"Greet the customer", "Identify their need", "Provide solution"});
```

##### `prompt_add_to_section`

```cpp
signalwire::agent::AgentBase& prompt_add_to_section(const std::string& title,
                                 const std::string& body = "",
                                 const std::vector<std::string>& bullets = {});
```
Add content to an existing prompt section.

**Parameters:**
- `title` (`std::string`): Title of the existing section to modify
- `body` (`std::string`): Additional body text to append (default: `""`)
- `bullets` (`std::vector<std::string>`): Bullet points to add (default: empty)

**Usage:**
```cpp
// Add body text to existing section
agent.prompt_add_to_section("Guidelines", "Remember to always verify customer identity.");

// Add bullets to an existing section
agent.prompt_add_to_section("Process", "", {"Document the interaction"});
agent.prompt_add_to_section("Process", "", {"Follow up", "Close ticket"});
```

##### `prompt_add_subsection`

```cpp
signalwire::agent::AgentBase& prompt_add_subsection(const std::string& parent_title,
                                 const std::string& title,
                                 const std::string& body = "",
                                 const std::vector<std::string>& bullets = {});
```
Add a subsection to an existing prompt section.

**Parameters:**
- `parent_title` (`std::string`): Title of the parent section
- `title` (`std::string`): Subsection title
- `body` (`std::string`): Subsection content (default: `""`)
- `bullets` (`std::vector<std::string>`): Subsection bullet points (default: empty)

**Usage:**
```cpp
agent.prompt_add_subsection("Guidelines", "Escalation Rules",
    "Escalate when:",
    {"Customer is angry", "Technical issue beyond scope"});
```

### Voice and Language Configuration

##### `add_language`

```cpp
signalwire::agent::AgentBase& add_language(const signalwire::agent::LanguageConfig& lang);
```
Configure voice and language settings for the agent. `agent::LanguageConfig` is a struct whose fields are `{name, code, voice, engine, model, speech_fillers, function_fillers, params}`; only `name`, `code`, and `voice` are required, and it is brace-initialized at the call site.

**LanguageConfig fields:**
- `name` (`std::string`): Human-readable language name
- `code` (`std::string`): Language code (e.g., `"en-US"`, `"es-ES"`)
- `voice` (`std::string`): Voice identifier (e.g., `"rime.spore"`, `"nova.luna"`)
- `engine` (`std::string`): TTS engine to use (optional)
- `model` (`std::string`): Explicit TTS model name (optional)
- `speech_fillers` (`std::vector<std::string>`): Filler phrases during speech processing (optional)
- `function_fillers` (`std::vector<std::string>`): Filler phrases during function execution (optional)
- `params` (`json`): Per-language engine-specific tuning (optional)

**Usage:**
```cpp
// Basic language setup (name, code, voice)
agent.add_language({"English", "en-US", "rime.spore"});

// With an explicit engine and both filler kinds
signalwire::agent::LanguageConfig lang;
lang.name = "English";
lang.code = "en-US";
lang.voice = "nova.luna";
lang.speech_fillers = {"Let me think...", "One moment..."};
lang.function_fillers = {"Processing...", "Looking that up..."};
agent.add_language(lang);
```

##### `set_languages(const std::vector<LanguageConfig>& langs) -> AgentBase&`
Set multiple language configurations at once, replacing any existing list.

**Parameters:**
- `langs` (`std::vector<LanguageConfig>`): List of language configurations

**Usage:**
```cpp
agent.set_languages({
    {"English", "en-US", "rime.spore"},
    {"Spanish", "es-ES", "nova.luna"}
});
```

Set engine-specific tuning on an already-added language with `set_language_params(code, params)`:

```cpp
agent.set_language_params("en-US", {{"speed", 1.1}});
```

### Speech Recognition Configuration

##### `add_hint(const std::string& hint) -> AgentBase&`
Add a single speech recognition hint.

**Parameters:**
- `hint` (`std::string`): Word or phrase to improve recognition accuracy

**Usage:**
```cpp
agent.add_hint("SignalWire");
```

##### `add_hints(const std::vector<std::string>& hints) -> AgentBase&`
Add multiple speech recognition hints.

**Parameters:**
- `hints` (`std::vector<std::string>`): List of words/phrases for better recognition

**Usage:**
```cpp
agent.add_hints({"SignalWire", "SWML", "API", "webhook", "SIP"});
```

##### `add_pattern_hint`

```cpp
signalwire::agent::AgentBase& add_pattern_hint(const std::string& hint,
                            const std::string& pattern,
                            const std::string& replace,
                            bool ignore_case = false);
```
Add a structured pattern-based hint for speech recognition. No-op unless `hint`, `pattern`, and `replace` are all non-empty.

**Parameters:**
- `hint` (`std::string`): The hint phrase
- `pattern` (`std::string`): Regex pattern to match
- `replace` (`std::string`): Replacement text
- `ignore_case` (`bool`): Case-insensitive matching (default: `false`)

**Usage:**
```cpp
agent.add_pattern_hint("phone number",
                       "(\\d{3})-(\\d{3})-(\\d{4})",
                       "(\\1) \\2-\\3");
```

##### `add_pronunciation`

```cpp
signalwire::agent::AgentBase& add_pronunciation(const std::string& replace_val,
                             const std::string& with_val,
                             bool ignore_case = false);
```
Add a pronunciation rule for text-to-speech.

**Parameters:**
- `replace_val` (`std::string`): Text to replace
- `with_val` (`std::string`): Replacement pronunciation
- `ignore_case` (`bool`): Case-insensitive replacement (default: `false`)

**Usage:**
```cpp
agent.add_pronunciation("API", "A P I");
agent.add_pronunciation("SWML", "swim-el");
```

##### `set_pronunciations`

```cpp
signalwire::agent::AgentBase& set_pronunciations(const std::vector<signalwire::agent::Pronunciation>& pronuns);
```
Set multiple pronunciation rules at once. `agent::Pronunciation` is a struct with fields `{replace_val, with_val, ignore_case}`.

**Parameters:**
- `pronuns` (`std::vector<Pronunciation>`): List of pronunciation rules

**Usage:**
```cpp
agent.set_pronunciations({
    {"API", "A P I", false},
    {"SWML", "swim-el", true}
});
```

### AI Parameters Configuration

##### `set_param(const std::string& key, const json& value) -> AgentBase&`
Set a single AI parameter.

**Parameters:**
- `key` (`std::string`): Parameter name
- `value` (`json`): Parameter value

**Usage:**
```cpp
agent.set_param("ai_model", "gpt-4.1-nano");
agent.set_param("end_of_speech_timeout", 500);
```

##### `set_params(const json& params) -> AgentBase&`
Set multiple AI parameters at once.

**Parameters:**
- `params` (`json`): Object of parameter key-value pairs

**Common Parameters:**
- `ai_model`: AI model to use ("gpt-4.1-nano", "gpt-4.1-mini", etc.)
- `end_of_speech_timeout`: Milliseconds to wait for speech end (default: 1000)
- `attention_timeout`: Milliseconds before attention timeout (default: 30000)
- `background_file_volume`: Volume for background audio (-60 to 0 dB)
- `temperature`: AI creativity/randomness (0.0 to 2.0)
- `max_tokens`: Maximum response length
- `top_p`: Nucleus sampling parameter (0.0 to 1.0)

**Usage:**
```cpp
agent.set_params({
    {"ai_model", "gpt-4.1-nano"},
    {"end_of_speech_timeout", 500},
    {"attention_timeout", 15000},
    {"background_file_volume", -20},
    {"temperature", 0.7}
});
```

### Global Data Management

##### `set_global_data(const json& data) -> AgentBase&`
Set global data available to the AI and functions.

**Parameters:**
- `data` (`json`): Global data object

**Usage:**
```cpp
agent.set_global_data({
    {"company_name", "Acme Corp"},
    {"support_hours", "9 AM - 5 PM EST"},
    {"escalation_number", "+1-555-0123"}
});
```

##### `update_global_data(const json& data) -> AgentBase&`
Update existing global data (merge with existing).

**Parameters:**
- `data` (`json`): Data to merge with existing global data

**Usage:**
```cpp
agent.update_global_data({
    {"current_promotion", "20% off all services"},
    {"promotion_expires", "2024-12-31"}
});
```

### Function Definition

##### `define_tool`

```cpp
signalwire::agent::AgentBase& define_tool(const std::string& name,
                       const std::string& description,
                       const json& parameters,
                       signalwire::swaig::ToolHandler handler,
                       bool secure = false);

// Overload taking a fully-built ToolDefinition
signalwire::agent::AgentBase& define_tool(const signalwire::swaig::ToolDefinition& tool);
```
Define a custom SWAIG function/tool. The handler is a `swaig::ToolHandler`, which is `std::function<swaig::FunctionResult(const json& args, const json& raw_data)>`.

**Parameters:**
- `name` (`std::string`): Function name
- `description` (`std::string`): Function description for the AI (prompt engineering — it tells the model WHEN to call the tool)
- `parameters` (`json`): JSON-Schema object for function parameters
- `handler` (`swaig::ToolHandler`): Callable executed when the tool is invoked
- `secure` (`bool`): Require a valid per-call security token (default: `false`)

**Usage:**
```cpp
agent.define_tool("get_weather", "Get current weather for a location",
    {{"type", "object"}, {"properties", {
        {"location", {{"type", "string"}, {"description", "City name"}}}
    }}, {"required", {"location"}}},
    [](const json& args, const json& raw) -> signalwire::swaig::FunctionResult {
        (void)raw;
        std::string location = args.value("location", "Unknown");
        return signalwire::swaig::FunctionResult("The weather in " + location + " is sunny and 75F");
    });
```

##### Building parameter schemas with `swaig::ParameterSchema`

Writing JSON Schema by hand is error-prone. The `swaig::ParameterSchema` builder produces byte-identical wire JSON via a typed fluent API and converts implicitly to `json`:

```cpp
auto params = signalwire::swaig::ParameterSchema{}
    .string("location", "City name")
    .enum_of("units", {"celsius", "fahrenheit"}, "Temperature units")
    .required({"location"});

agent.define_tool("get_weather", "Get current weather for a location", params,
    [](const json& args, const json& raw) -> signalwire::swaig::FunctionResult {
        (void)raw;
        return signalwire::swaig::FunctionResult("Weather in " + args.value("location", ""));
    });
```

The C++ SDK has no method-decorator mechanism. Tools that Python defines as decorated class methods are registered the same way as any other tool — call `define_tool(...)` in the agent's constructor, passing a lambda (or a bound member function) as the handler:

```cpp
class MyAgent : public signalwire::agent::AgentBase {
public:
    MyAgent() : AgentBase("my-agent", "/agent") {
        define_tool("get_time", "Get current time",
            {{"type", "object"}, {"properties", json::object()}},
            [](const json& args, const json& raw) -> signalwire::swaig::FunctionResult {
                (void)args; (void)raw;
                auto t = std::time(nullptr);
                char buf[32];
                std::strftime(buf, sizeof(buf), "%H:%M:%S", std::localtime(&t));
                return signalwire::swaig::FunctionResult(std::string("Current time: ") + buf);
            });
    }
};
```

##### `register_swaig_function`

```cpp
signalwire::agent::AgentBase& register_swaig_function(const json& func_def);
```
Register a pre-built SWAIG function definition (for example, one produced by `DataMap::to_swaig_function()`).

**Parameters:**
- `func_def` (`json`): Complete SWAIG function definition

**Usage:**
```cpp
// Register a DataMap tool
auto weather_tool = signalwire::datamap::DataMap("get_weather")
    .webhook("GET", "https://api.weather.com/...");
agent.register_swaig_function(weather_tool.to_swaig_function());
```

### Session Lifecycle Hooks

SignalWire AI agents support special SWAIG functions that are automatically called at specific points in the conversation lifecycle:

##### `startup_hook`
Called when a new conversation/call begins. Implement it as an ordinary tool whose name is exactly `startup_hook`.

**Implementation:**
```cpp
agent.define_tool("startup_hook", "Called when a new conversation starts to initialize state",
    {{"type", "object"}, {"properties", json::object()}},
    [](const json& args, const json& raw) -> signalwire::swaig::FunctionResult {
        (void)args;
        std::string call_id = raw.value("call_id", "");
        // Initialize session resources, load user data, etc.
        return signalwire::swaig::FunctionResult("Session initialized");
    });
```

##### `hangup_hook`
Called when a conversation/call ends. Implement it as an ordinary tool whose name is exactly `hangup_hook`.

**Implementation:**
```cpp
agent.define_tool("hangup_hook", "Called when conversation ends to clean up resources",
    {{"type", "object"}, {"properties", json::object()}},
    [](const json& args, const json& raw) -> signalwire::swaig::FunctionResult {
        (void)args;
        std::string call_id = raw.value("call_id", "");
        // Clean up resources, save session data, etc.
        return signalwire::swaig::FunctionResult("Session ended");
    });
```

**Common Use Cases:**
- Loading user preferences at session start
- Initializing session-specific resources
- Logging conversation metrics
- Cleaning up temporary data
- Saving conversation summaries

### Skills System

##### `add_skill`

```cpp
signalwire::agent::AgentBase& add_skill(const std::string& skill_name,
                     const json& params = json::object());

// Typed-enum overload for the built-in skill closed set
signalwire::agent::AgentBase& add_skill(signalwire::skills::SkillName skill_name,
                     const json& params = json::object());
```
Add a modular skill to the agent. `params` is an optional JSON object of configuration.

**Parameters:**
- `skill_name` (`std::string` or `skills::SkillName`): Name of the skill to add
- `params` (`json`): Skill configuration parameters (default: empty object)

**Available Skills:**
- `datetime`: Current date/time information
- `math`: Mathematical calculations
- `web_search`: Google Custom Search integration
- `datasphere`: SignalWire DataSphere search
- `native_vector_search`: Local document search

**Usage:**
```cpp
// Simple skill
agent.add_skill("datetime");
agent.add_skill("math");

// Typed-enum form (autocompleted, typo-checked) loads the identical skill
agent.add_skill(signalwire::skills::SkillName::Datetime);

// Skill with configuration
agent.add_skill("web_search", {
    {"api_key", "your-google-api-key"},
    {"search_engine_id", "your-search-engine-id"},
    {"num_results", 3}
});

// Multiple instances with different tool names
agent.add_skill("web_search", {
    {"api_key", "your-api-key"},
    {"search_engine_id", "general-engine"},
    {"tool_name", "search_general"}
});

agent.add_skill("web_search", {
    {"api_key", "your-api-key"},
    {"search_engine_id", "news-engine"},
    {"tool_name", "search_news"}
});
```

##### `remove_skill(const std::string& skill_name) -> AgentBase&`
Remove a skill from the agent.

**Parameters:**
- `skill_name` (`std::string`): Name of skill to remove

**Usage:**
```cpp
agent.remove_skill("web_search");
```

##### `list_skills() -> std::vector<std::string>`
Get the list of currently added skills.

**Returns:**
- `std::vector<std::string>`: Names of active skills

**Usage:**
```cpp
std::vector<std::string> active_skills = agent.list_skills();
```

##### `has_skill(const std::string& skill_name) -> bool`
Check whether a skill is currently added.

**Parameters:**
- `skill_name` (`std::string`): Name of skill to check

**Returns:**
- `bool`: `true` if the skill is active

**Usage:**
```cpp
if (agent.has_skill("web_search")) {
    // Web search is available
}
```

### Native Functions

##### `set_native_functions`

```cpp
signalwire::agent::AgentBase& set_native_functions(const std::vector<std::string>& funcs);
```
Enable specific native SWML functions.

**Parameters:**
- `funcs` (`std::vector<std::string>`): List of native function names to enable

**Available Native Functions:**
- `transfer`: Transfer calls
- `hangup`: End calls
- `play`: Play audio files
- `record`: Record audio
- `send_sms`: Send SMS messages

**Usage:**
```cpp
agent.set_native_functions({"transfer", "hangup", "send_sms"});
```

##### `set_internal_fillers`

```cpp
signalwire::agent::AgentBase& set_internal_fillers(const json& fillers);
```
Set custom filler phrases for internal/native SWAIG functions.

**Parameters:**
- `fillers` (`json`): Object mapping function name → language code → filler phrases

**Available Internal Functions:**
- `next_step`: Moving between workflow steps (contexts system)
- `change_context`: Switching contexts in workflows  
- `check_time`: Getting current time
- `wait_for_user`: Waiting for user input
- `wait_seconds`: Pausing for specified duration
- `get_visual_input`: Processing visual data

**Usage:**
```cpp
agent.set_internal_fillers({
    {"next_step", {
        {"en-US", {"Moving to the next step...", "Let's continue..."}},
        {"es", {"Pasando al siguiente paso...", "Continuemos..."}}
    }},
    {"check_time", {
        {"en-US", {"Let me check the time...", "Getting current time..."}}
    }}
});
```

##### `add_internal_filler`

```cpp
signalwire::agent::AgentBase& add_internal_filler(const std::string& function_name,
                               const std::string& language_code,
                               const std::vector<std::string>& fillers);
```
Add internal fillers for a specific function and language.

**Parameters:**
- `function_name` (`std::string`): Name of the internal function
- `language_code` (`std::string`): Language code (e.g., `"en-US"`, `"es"`, `"fr"`)
- `fillers` (`std::vector<std::string>`): List of filler phrases

**Usage:**
```cpp
agent.add_internal_filler("next_step", "en-US", {
    "Great! Let's move to the next step...",
    "Perfect! Moving forward..."
});
```

### Function Includes

##### `add_function_include`

```cpp
signalwire::agent::AgentBase& add_function_include(const json& include);
```
Include external SWAIG functions from another service. The `include` object carries `url`, `functions`, and optional `meta_data` keys.

**Include object keys:**
- `url` (string): URL of the external SWAIG service
- `functions` (array of strings): Function names to include
- `meta_data` (object, optional): Additional metadata

**Usage:**
```cpp
agent.add_function_include({
    {"url", "https://external-service.com/swaig"},
    {"functions", {"external_function1", "external_function2"}},
    {"meta_data", {{"service", "external"}, {"version", "1.0"}}}
});
```

##### `set_function_includes`

```cpp
signalwire::agent::AgentBase& set_function_includes(const std::vector<json>& includes);
```
Set multiple function includes at once.

**Parameters:**
- `includes` (`std::vector<json>`): List of function-include configurations

**Usage:**
```cpp
agent.set_function_includes({
    {
        {"url", "https://service1.com/swaig"},
        {"functions", {"func1", "func2"}}
    },
    {
        {"url", "https://service2.com/swaig"},
        {"functions", {"func3"}},
        {"meta_data", {{"priority", "high"}}}
    }
});
```

### Webhook Configuration

##### `set_web_hook_url(const std::string& url) -> AgentBase&`
Set the default webhook URL for SWAIG functions. (Alias of `set_webhook_url`; the Python spelling splits `web_hook`.)

**Parameters:**
- `url` (`std::string`): Default webhook URL

**Usage:**
```cpp
agent.set_web_hook_url("https://myserver.com/webhook");
```

##### `set_post_prompt_url(const std::string& url) -> AgentBase&`
Set the URL for post-prompt processing.

**Parameters:**
- `url` (`std::string`): Post-prompt webhook URL

**Usage:**
```cpp
agent.set_post_prompt_url("https://myserver.com/post-prompt");
```

##### `add_swaig_query_param(const std::string& key, const std::string& value)` / `add_swaig_query_params(const json& params)`
Add query parameters to be included in all SWAIG webhook URLs. `add_swaig_query_param` adds one at a time; `add_swaig_query_params` adds several from a JSON object.

This is useful for preserving dynamic configuration state across SWAIG callbacks. For example, if your dynamic config adds skills based on query parameters, you can pass those same parameters through to the SWAIG webhook so the same configuration is applied.

**Usage:**
```cpp
// In a dynamic config callback, preserve configuration parameters
agent.set_dynamic_config_callback(
    [](const std::map<std::string, std::string>& query_params,
       const json& body, const std::map<std::string, std::string>& headers,
       signalwire::agent::AgentBase& agent) {
        (void)body; (void)headers;
        if (auto it = query_params.find("customer_id"); it != query_params.end()) {
            // Pass through to SWAIG callbacks
            agent.add_swaig_query_param("customer_id", it->second);
            agent.add_skill("customer_lookup", {{"customer_id", it->second}});
        }
    });
```

##### `clear_swaig_query_params() -> AgentBase&`
Clear all SWAIG query parameters.

**Usage:**
```cpp
agent.clear_swaig_query_params();
```

### Debug Events

##### `enable_debug_events`

```cpp
signalwire::agent::AgentBase& enable_debug_events(bool enable = true);
```
Enable the debug event webhook for this agent. When enabled, the AI module will POST real-time debug events to a `/debug_events` endpoint on this agent during calls. Events are automatically logged via the agent's structured logger and can optionally be handled with a custom callback via `on_debug_event()`.

**Parameters:**
- `enable` (`bool`): Enable (`true`) or disable (`false`) debug events. Default: `true`

**Usage:**
```cpp
agent.enable_debug_events();       // enable
agent.enable_debug_events(false);  // disable
```

**How it works:**
- Registers a `/debug_events` POST endpoint on the agent's HTTP server
- Auto-sets `debug_webhook_url` and `debug_webhook_level` in the SWML `params` during rendering
- The URL is built automatically using the same auth/proxy logic as other webhook URLs
- No manual URL configuration needed

**Event types at level 1:**

| Event label | Description |
|-------------|-------------|
| `session_start` | AI session started (model, TTS engine, voice, language) |
| `session_end` | AI session ended (reason, duration, token counts) |
| `barge` | User interrupted AI speech (barge type, elapsed ms) |
| `step_change` | Conversation step changed |
| `context_change` | Conversation context changed |
| `llm_error` | LLM error (fatal, retry, max_retries) |
| `voice_error` | TTS voice configuration or runtime error |
| `hold` | Call placed on hold or taken off hold |
| `filler` | Filler phrase spoken (thinking or function filler) |
| `consolidation` | Token consolidation triggered |
| `process_action` | Webhook action being processed |
| `gather_start` | Gather flow started |
| `gather_complete` | Gather flow completed |

**Additional events at level 2+:**

| Event label | Description |
|-------------|-------------|
| `llm_request` | LLM API request initiated (input tokens) |
| `llm_response` | LLM API response received (duration, output tokens) |
| `conversation_add` | Entry added to conversation history |

### Call Flow Verb Insertion

These methods allow you to customize the SWML call flow by inserting verbs at different stages of the call lifecycle.

##### `add_pre_answer_verb(const std::string& verb_name, const json& params) -> AgentBase&`
Add a verb to run before the call is answered (while still ringing).

**Safe pre-answer verbs:** `transfer`, `execute`, `return`, `label`, `goto`, `request`, `switch`, `cond`, `if`, `eval`, `set`, `unset`, `hangup`, `send_sms`, `sleep`, `stop_record_call`, `stop_denoise`, `stop_tap`

**Parameters:**
- `verb_name` (`std::string`): The SWML verb name
- `params` (`json`): Verb configuration object

**Usage:**
```cpp
// Send SMS before answering
agent.add_pre_answer_verb("send_sms", {
    {"to", "+15551234567"},
    {"from", "+15559876543"},
    {"body", "Incoming call from AI agent"}
});

// Set variables before answer
agent.add_pre_answer_verb("set", {{"call_start", "${system.timestamp}"}});
```

##### `add_answer_verb(const std::string& verb_name, const json& params) -> AgentBase&`
Configure the answer verb that connects the call.

**Parameters:**
- `verb_name` (`std::string`): The SWML verb name (typically `"answer"`)
- `params` (`json`): Answer verb configuration (e.g., `{{"max_duration", 3600}}`)

**Usage:**
```cpp
// Set maximum call duration to 1 hour
agent.add_answer_verb("answer", {{"max_duration", 3600}});
```

##### `add_post_answer_verb(const std::string& verb_name, const json& params) -> AgentBase&`
Add a verb to run after the call is answered but before the AI starts.

**Parameters:**
- `verb_name` (`std::string`): The SWML verb name (e.g., `"play"`, `"sleep"`)
- `params` (`json`): Verb configuration object

**Usage:**
```cpp
// Play welcome message before AI starts
agent.add_post_answer_verb("play", {
    {"url", "say:Welcome to our AI assistant. This call may be recorded."}
});

// Add a brief pause
agent.add_post_answer_verb("sleep", {{"duration", 1}});
```

##### `add_post_ai_verb(const std::string& verb_name, const json& params) -> AgentBase&`
Add a verb to run after the AI conversation ends.

**Parameters:**
- `verb_name` (`std::string`): The SWML verb name (e.g., `"hangup"`, `"transfer"`, `"request"`)
- `params` (`json`): Verb configuration object

**Usage:**
```cpp
// Clean hangup after AI ends
agent.add_post_ai_verb("hangup", json::object());

// Transfer to human after AI conversation
agent.add_post_ai_verb("transfer", {{"to", "+15551234567"}});

// Log call completion
agent.add_post_ai_verb("request", {
    {"url", "https://myserver.com/call-complete"},
    {"method", "POST"}
});
```

##### `clear_pre_answer_verbs() -> AgentBase&`
Remove all pre-answer verbs.

##### `clear_post_answer_verbs() -> AgentBase&`
Remove all post-answer verbs.

##### `clear_post_ai_verbs() -> AgentBase&`
Remove all post-AI verbs.

**Method Chaining Example:**
```cpp
agent.add_pre_answer_verb("set", {{"source", "ai_agent"}})
     .add_answer_verb("answer", {{"max_duration", 1800}})
     .add_post_answer_verb("play", {{"url", "say:Hello!"}})
     .add_post_ai_verb("hangup", json::object());
```

### Dynamic Configuration

##### `set_dynamic_config_callback`

```cpp
signalwire::agent::AgentBase& set_dynamic_config_callback(signalwire::agent::DynamicConfigCallback cb);
```
Set a callback for per-request dynamic configuration. `agent::DynamicConfigCallback` is `std::function<void(const std::map<std::string,std::string>& query_params, const json& body_params, const std::map<std::string,std::string>& headers, AgentBase& agent_copy)>` — the callback runs fresh for each request and configures a per-request copy of the agent.

**Parameters:**
- `cb` (`DynamicConfigCallback`): Function that receives `(query_params, body_params, headers, agent_copy)`

**Usage:**
```cpp
agent.set_dynamic_config_callback(
    [](const std::map<std::string, std::string>& query_params,
       const json& body_params,
       const std::map<std::string, std::string>& headers,
       signalwire::agent::AgentBase& agent) {
        (void)body_params;

        // Configure based on request
        if (auto it = query_params.find("language");
            it != query_params.end() && it->second == "spanish") {
            agent.add_language({"Spanish", "es-ES", "nova.luna"});
        }

        // Set customer-specific data
        if (auto it = headers.find("X-Customer-ID"); it != headers.end()) {
            agent.set_global_data({{"customer_id", it->second}});
        }
    });
```

### SIP Integration

##### `enable_sip_routing`

```cpp
signalwire::agent::AgentBase& enable_sip_routing(bool enable = true);
signalwire::agent::AgentBase& auto_map_sip_usernames(bool enable = true);
```
Enable SIP-based routing for voice calls. Automatic mapping of SIP usernames from the agent name/route is toggled separately with `auto_map_sip_usernames`.

**Parameters:**
- `enable` (`bool`): Enable (`true`) or disable (`false`) SIP routing. Default: `true`

**Usage:**
```cpp
agent.enable_sip_routing(true);
agent.auto_map_sip_usernames(true);
```

##### `register_sip_username(const std::string& username) -> AgentBase&`
Register a specific SIP username for this agent.

**Parameters:**
- `username` (`std::string`): SIP username to register

**Usage:**
```cpp
agent.register_sip_username("support");
agent.register_sip_username("sales");
```

##### `register_routing_callback`

```cpp
using RoutingCallback = std::function<std::string(
    const json& body, const std::map<std::string, std::string>& headers)>;

signalwire::agent::AgentBase& register_routing_callback(RoutingCallback callback,
                                     const std::string& path = "/");
```
Register custom routing logic for inbound requests. The callback receives the parsed request `body` and `headers` and returns the route to dispatch to (empty string = no override).

**Parameters:**
- `callback` (`RoutingCallback`): Function that returns an agent route based on the request
- `path` (`std::string`): Routing endpoint path (default: `"/"`)

**Usage:**
```cpp
agent.register_routing_callback(
    [](const json& body, const std::map<std::string, std::string>& headers) -> std::string {
        (void)headers;
        std::string sip_username = body.value("sip_username", "");
        if (sip_username == "support") return "/support-agent";
        if (sip_username == "sales") return "/sales-agent";
        return "";  // no override
    },
    "/sip");
```

### Utility Methods

##### `get_name() -> str`
Get the agent's name.

**Returns:**
- str: Agent name

### Event Handlers

Handlers are registered as `std::function` callbacks via the `on_*` setters rather than by subclass override.

##### `on_summary`

```cpp
using SummaryCallback = std::function<void(const json& summary, const json& raw_data)>;

signalwire::agent::AgentBase& on_summary(SummaryCallback cb);
```
Register a handler for conversation summaries. This callback is triggered when the AI generates a summary based on your `set_post_prompt(...)` configuration.

**Parameters:**
- `summary` (`json`): Parsed summary data (from `post_prompt_data.parsed[0]`)
- `raw_data` (`json`): Complete raw POST data including `post_prompt_data` with both `raw` and `parsed` fields

**Usage:**
```cpp
class MyAgent : public signalwire::agent::AgentBase {
public:
    MyAgent() : AgentBase("summary-agent", "/agent") {
        // Configure post-prompt to request JSON summary
        set_post_prompt(R"(
        Return a JSON summary of the conversation:
        {
            "topic": "MAIN_TOPIC",
            "satisfied": true/false,
            "follow_up_needed": true/false,
            "key_points": ["point1", "point2"]
        }
        )");

        on_summary([this](const json& summary, const json& raw_data) {
            if (!summary.is_null()) {
                // Access parsed JSON fields directly
                std::string topic = summary.value("topic", "Unknown");
                bool satisfied = summary.value("satisfied", false);

                std::cout << "Call about: " << topic
                          << ", Customer satisfied: " << satisfied << "\n";

                // Save to database, send to CRM, trigger follow-up, etc.
                if (summary.value("follow_up_needed", false)) {
                    // ...your own follow-up logic here (e.g. enqueue a callback)...
                }
            }

            // Access raw summary text if needed
            if (raw_data.contains("post_prompt_data")) {
                std::string raw_text = raw_data["post_prompt_data"].value("raw", "");
                std::cout << "Raw summary: " << raw_text << "\n";
            }
        });
    }
};
```

##### `on_debug_event`

```cpp
using DebugEventCallback = std::function<void(const json& event)>;

signalwire::agent::AgentBase& on_debug_event(DebugEventCallback cb);
```
Register a handler for debug webhook events. Requires `enable_debug_events()` to be called first.

The callback receives the full event payload as a `json` object, including `label` (the event type, e.g. `"barge"`, `"llm_error"`, `"session_start"`), `call_id`, and event-specific fields.

**Usage:**
```cpp
class MyAgent : public signalwire::agent::AgentBase {
public:
    MyAgent() : AgentBase("debug-agent", "/agent") {
        enable_debug_events();

        on_debug_event([this](const json& event) {
            std::string label = event.value("label", "");
            std::string call_id = event.value("call_id", "");
            if (label == "llm_error") {
                std::cout << "LLM error on call " << call_id << ": "
                          << event.value("event", "") << "\n";
            } else if (label == "barge") {
                std::cout << "Barge after " << event.value("barge_elapsed_ms", 0) << "ms\n";
            } else if (label == "session_end") {
                std::cout << "Call ended: " << event.value("reason", "")
                          << ", duration: " << event.value("duration_ms", 0) << "ms\n";
            }
        });
    }
};
```

> **Note:** Even without registering a handler, all debug events are automatically logged via the agent's structured logger when `enable_debug_events()` is called.

##### Handling function calls

In the C++ SDK each SWAIG function is handled by the `swaig::ToolHandler` lambda you register with `define_tool(...)` — there is no single dispatch-override hook. Register a handler per function and return a `swaig::FunctionResult`:

```cpp
agent.define_tool(
    "get_weather", "Look up the weather for a location",
    json{{"type", "object"},
         {"properties", {{"location", {{"type", "string"}}}}},
         {"required", json::array({"location"})}},
    [](const json& args, const json& raw) -> signalwire::swaig::FunctionResult {
        (void)raw;
        std::string location = args.value("location", "");
        // Custom weather logic
        return signalwire::swaig::FunctionResult("Weather in " + location + ": Sunny");
    });
```

##### Request customization via dynamic config

The Python `on_request` / `on_swml_request` override hooks have no direct C++ equivalent. To customize the SWML document per request, register a dynamic-config callback with `set_dynamic_config_callback(...)`; it receives the parsed request data and a mutable agent handle you reconfigure before the document is rendered:

```cpp
agent.set_dynamic_config_callback(
    [](const std::map<std::string, std::string>& query_params, const json& body_params,
       const std::map<std::string, std::string>& headers, signalwire::agent::AgentBase& cfg) {
        (void)query_params;
        (void)headers;
        if (body_params.value("tier", "") == "premium") {
            cfg.prompt_add_section("Tier", "This is a premium customer.");
        }
    });
```

### Authentication

##### `set_auth(const std::string& username, const std::string& password) -> AgentBase&`
Configure HTTP Basic authentication credentials. The agent validates every inbound request against these credentials automatically.

**Parameters:**
- `username` (`std::string`): Basic auth username
- `password` (`std::string`): Basic auth password

**Usage:**
```cpp
agent.set_auth("admin", "secret");
```

### Context System

##### `define_contexts() -> ContextBuilder&`
Define structured workflow contexts for the agent.

**Returns:**
- `ContextBuilder&`: Builder for creating contexts and steps

**Usage:**
```cpp
auto& contexts = agent.define_contexts();

auto& greeting = contexts.add_context("greeting");
greeting.add_step("welcome")
    .add_section("Task", "Welcome! How can I help?")
    .set_valid_steps({"menu"});

auto& main_menu = contexts.add_context("main_menu");
main_menu.add_step("menu")
    .add_section("Task", "Choose: 1) Support 2) Sales 3) Billing")
    .set_functions(std::vector<std::string>{"transfer_to_support", "transfer_to_sales"});
```

This concludes Part 1 of the API reference covering the AgentBase class. The document continues with `swaig::FunctionResult`, DataMap, and other components in subsequent parts.

---

## FunctionResult Class

The `swaig::FunctionResult` class is used to create structured responses from SWAIG functions. It handles both natural language responses and structured actions that the agent should execute.

### Constructor

<!-- snippet: no-compile constructor signature listing (class-member declaration shown out of class context) -->
```cpp
explicit FunctionResult(const std::string& response = "", bool post_process = false);
```

**Parameters:**
- `response` (`std::string`): Natural language response text for the AI to speak
- `post_process` (`bool`): Whether to let AI take another turn before executing actions (default: `false`)

**Post-processing Behavior:**
- `post_process=false` (default): Execute actions immediately after AI response
- `post_process=true`: Let AI respond to user one more time, then execute actions

**Usage:**
```cpp
// Simple response
result = signalwire::swaig::FunctionResult("The weather is sunny and 75°F");

// Response with post-processing enabled
auto result2 = signalwire::swaig::FunctionResult("I'll transfer you now", /*post_process=*/true);

// Empty response (actions only)
auto result3 = signalwire::swaig::FunctionResult();
```

### Core Methods

#### Response Configuration

##### `set_response(const std::string& response) -> FunctionResult&`
Set or update the natural language response text.

**Parameters:**
- `response` (`std::string`): The text the AI should speak

**Usage:**
```cpp
result = signalwire::swaig::FunctionResult();
result.set_response("I found your order information");
```

##### `set_post_process(bool pp) -> FunctionResult&`
Enable or disable post-processing for this result.

**Parameters:**
- `pp` (`bool`): `true` to let AI respond once more before executing actions

**Usage:**
```cpp
result = signalwire::swaig::FunctionResult("I'll help you with that");
result.set_post_process(true);  // Let AI handle follow-up questions first
```

#### Action Management

##### `add_action(const std::string& name, const json& data) -> FunctionResult&`
Add a structured action to execute.

**Parameters:**
- `name` (`std::string`): Action name/type (e.g., "play", "transfer", "set_global_data")
- `data` (`json`): Action data — can be a string, boolean, object, or array

**Usage:**
```cpp
// Simple action with boolean
result.add_action("hangup", true);

// Action with string data
result.add_action("play", "welcome.mp3");

// Action with object data
result.add_action("set_global_data", json{{"customer_id", "12345"}, {"status", "verified"}});

// Action with array data
result.add_action("send_sms", json::array({"+15551234567", "Your order is ready!"}));
```

##### `add_actions(const std::vector<json>& actions) -> FunctionResult&`
Add multiple actions at once.

**Parameters:**
- `actions` (`std::vector<json>`): List of action objects

**Usage:**
```cpp
result.add_actions({
    json{{"play", "hold_music.mp3"}},
    json{{"set_global_data", {{"status", "on_hold"}}}},
    json{{"wait", 5000}}
});
```

### Call Control Actions

#### Call Transfer and Connection

##### `connect(const std::string& destination, bool final = true, const std::string& from_addr = "") -> FunctionResult&`
Transfer or connect the call to another destination.

**Parameters:**
- `destination` (`std::string`): Phone number, SIP address, or other destination
- `final` (`bool`): Permanent transfer (`true`) vs temporary transfer (`false`) (default: `true`)
- `from_addr` (`std::string`): Override caller ID (empty = unchanged)

**Transfer Types:**
- `final=true`: Permanent transfer — call exits agent completely
- `final=false`: Temporary transfer — call returns to agent if far end hangs up

**Usage:**
```cpp
// Permanent transfer to phone number
result.connect("+15551234567", /*final=*/true);

// Temporary transfer to SIP address with custom caller ID
result.connect("support@company.com", /*final=*/false, "+15559876543");

// Transfer with response
auto result2 = signalwire::swaig::FunctionResult("Transferring you to our sales team");
result2.connect("sales@company.com");
```

##### `swml_transfer(const std::string& dest, const std::string& ai_response, bool final = true) -> FunctionResult&`
Create a SWML-based transfer with AI response setup.

**Parameters:**
- `dest` (`std::string`): Transfer destination
- `ai_response` (`std::string`): AI response when transfer completes

**Usage:**
```cpp
result.swml_transfer(
    "+15551234567",
    "You've been transferred back to me. How else can I help?");
```

##### `sip_refer(const std::string& to_uri) -> FunctionResult&`
Perform a SIP REFER transfer.

**Parameters:**
- `to_uri` (`std::string`): SIP URI to transfer to

**Usage:**
```cpp
result.sip_refer("sip:support@company.com");
```

#### Call Management

##### `hangup() -> FunctionResult&`
End the call immediately.

**Usage:**
```cpp
result = signalwire::swaig::FunctionResult("Thank you for calling. Goodbye!");
result.hangup();
```

##### `hold(int timeout = 300) -> FunctionResult&`
Put the call on hold.

**Parameters:**
- `timeout` (`int`): Hold timeout in seconds (default: 300)

**Usage:**
```cpp
result = signalwire::swaig::FunctionResult("Please hold while I look that up");
result.hold(/*timeout=*/60);
```

##### `stop() -> FunctionResult&`
Stop current audio playback or recording.

**Usage:**
```cpp
result.stop();
```

#### Audio Control

##### `say(const std::string& text) -> FunctionResult&`
Add text for the AI to speak.

**Parameters:**
- `text` (`std::string`): Text to speak

**Usage:**
```cpp
result.say("Please wait while I process your request");
```

##### `play_background_file(const std::string& filename, bool wait = false) -> FunctionResult&`
Play an audio file in the background.

**Parameters:**
- `filename` (`std::string`): Audio file path or URL
- `wait` (`bool`): Wait for file to finish before continuing (default: `false`)

**Usage:**
```cpp
// Play hold music in background
result.play_background_file("hold_music.mp3");

// Play announcement and wait for completion
result.play_background_file("important_announcement.wav", /*wait=*/true);
```

##### `stop_background_file() -> FunctionResult&`
Stop background audio playback.

**Usage:**
```cpp
result.stop_background_file();
```

### Data Management Actions

##### `update_global_data(const json& data) -> FunctionResult&`
Set or merge global data for the conversation. Keys present in `data` are added or overwritten; existing keys not mentioned are preserved.

**Parameters:**
- `data` (`json`): Global data to set/merge

**Usage:**
```cpp
result.update_global_data({
    {"customer_id", "12345"},
    {"order_status", "shipped"},
    {"tracking_number", "1Z999AA1234567890"}
});

// Merge additional fields later in the same result
result.update_global_data({
    {"last_interaction", "2024-01-15T10:30:00Z"},
    {"agent_notes", "Customer satisfied with resolution"}
});
```

##### `remove_global_data(const json& keys) -> FunctionResult&`
Remove specific keys from global data.

**Parameters:**
- `keys` (`json`): A single key name (string) or an array of key names to remove

**Usage:**
```cpp
// Remove single key
result.remove_global_data("temporary_data");

// Remove multiple keys
result.remove_global_data(json::array({"temp1", "temp2", "cache_data"}));
```

##### `set_metadata(const json& data) -> FunctionResult&`
Set metadata for the conversation.

**Parameters:**
- `data` (`json`): Metadata to set

**Usage:**
```cpp
result.set_metadata({
    {"call_type", "support"},
    {"priority", "high"},
    {"department", "technical"}
});
```

##### `remove_metadata(const json& keys) -> FunctionResult&`
Remove specific metadata keys.

**Parameters:**
- `keys` (`json`): A single key name (string) or an array of key names to remove

**Usage:**
```cpp
result.remove_metadata(json::array({"temporary_flag", "debug_info"}));
```

### AI Behavior Control

##### `set_end_of_speech_timeout(int milliseconds) -> FunctionResult&`
Adjust how long to wait for speech to end.

**Parameters:**
- `milliseconds` (`int`): Timeout in milliseconds

**Usage:**
```cpp
// Shorter timeout for quick responses
result.set_end_of_speech_timeout(300);

// Longer timeout for thoughtful responses
result.set_end_of_speech_timeout(2000);
```

##### `set_speech_event_timeout(int milliseconds) -> FunctionResult&`
Set timeout for speech events.

**Parameters:**
- `milliseconds` (`int`): Timeout in milliseconds

**Usage:**
```cpp
result.set_speech_event_timeout(5000);
```

##### `wait_for_user(std::optional<bool> enabled = std::nullopt, std::optional<int> timeout = std::nullopt, bool answer_first = false) -> FunctionResult&`
Control whether to wait for user input.

**Parameters:**
- `enabled` (`std::optional<bool>`): Enable/disable waiting for user
- `timeout` (`std::optional<int>`): Timeout in milliseconds
- `answer_first` (`bool`): Answer call before waiting (default: `false`)

**Usage:**
```cpp
// Wait for user input with 10 second timeout
result.wait_for_user(/*enabled=*/true, /*timeout=*/10000);

// Don't wait for user (immediate response)
result.wait_for_user(/*enabled=*/false);
```

##### `toggle_functions(const json& function_toggles) -> FunctionResult&`
Enable or disable specific functions.

**Parameters:**
- `function_toggles` (`json`): Array of function toggle configurations

**Usage:**
```cpp
result.toggle_functions(json::array({
    {{"name", "transfer_to_sales"}, {"enabled", true}},
    {{"name", "end_call"}, {"enabled", false}},
    {{"name", "escalate"}, {"enabled", true}, {"timeout", 30000}}
}));
```

##### `enable_functions_on_timeout(bool enabled = true) -> FunctionResult&`
Control whether functions are enabled when timeout occurs.

**Parameters:**
- `enabled` (`bool`): Enable functions on timeout (default: `true`)

**Usage:**
```cpp
result.enable_functions_on_timeout(false);  // Disable functions on timeout
```

##### `enable_extensive_data(bool enabled = true) -> FunctionResult&`
Enable extensive data collection.

**Parameters:**
- `enabled` (`bool`): Enable extensive data (default: `true`)

**Usage:**
```cpp
result.enable_extensive_data(true);
```

##### `update_settings(const json& settings) -> FunctionResult&`
Update various AI settings.

**Parameters:**
- `settings` (`json`): Settings to update

**Usage:**
```cpp
result.update_settings({
    {"temperature", 0.8},
    {"max_tokens", 150},
    {"end_of_speech_timeout", 800}
});
```

### Context and Conversation Control

##### `switch_context(const std::string& system_prompt = "", const std::string& user_prompt = "", bool consolidate = false, bool full_reset = false) -> FunctionResult&`
Switch conversation context or reset the conversation.

**Parameters:**
- `system_prompt` (`std::string`): New system prompt
- `user_prompt` (`std::string`): New user prompt
- `consolidate` (`bool`): Consolidate conversation history (default: `false`)
- `full_reset` (`bool`): Completely reset conversation (default: `false`)

**Usage:**
```cpp
// Switch to technical support context
result.switch_context(
    "You are now a technical support specialist",
    "The customer needs technical help");

// Reset conversation completely
result.switch_context("", "", /*consolidate=*/false, /*full_reset=*/true);

// Consolidate conversation history
result.switch_context("", "", /*consolidate=*/true);
```

##### `simulate_user_input(const std::string& text) -> FunctionResult&`
Simulate user input for testing or automation.

**Parameters:**
- `text` (`std::string`): Text to simulate as user input

**Usage:**
```cpp
result.simulate_user_input("I need help with my order");
```

### Communication Actions

##### `send_sms(const std::string& to, const std::string& from, const std::string& body = "", const std::vector<std::string>& media = {}, const std::vector<std::string>& tags = {}, const std::string& region = "") -> FunctionResult&`
Send an SMS message.

**Parameters:**
- `to` (`std::string`): Recipient phone number
- `from` (`std::string`): Sender phone number
- `body` (`std::string`): SMS message text
- `media` (`std::vector<std::string>`): List of media URLs
- `tags` (`std::vector<std::string>`): Message tags
- `region` (`std::string`): SignalWire region

**Usage:**
```cpp
// Simple text message
result.send_sms(
    "+15551234567",
    "+15559876543",
    "Your order #12345 has shipped!");

// Message with media and tags
result.send_sms(
    "+15551234567",
    "+15559876543",
    "Here's your receipt",
    {"https://example.com/receipt.pdf"},
    {"receipt", "order_12345"});
```

### Recording and Media

##### `record_call(const std::string& control_id = "", bool stereo = false, const std::string& format = "wav", const std::string& direction = "both", const std::string& terminators = "", bool beep = false, double input_sensitivity = 44.0, std::optional<double> initial_timeout = std::nullopt, std::optional<double> end_silence_timeout = std::nullopt, std::optional<double> max_length = std::nullopt, const std::string& status_url = "") -> FunctionResult&`
Start call recording.

**Parameters:**
- `control_id` (`std::string`): Unique identifier for this recording
- `stereo` (`bool`): Record in stereo (default: `false`)
- `format` (`std::string`): Recording format: "wav", "mp3", "mp4" (default: "wav")
- `direction` (`std::string`): Recording direction: "both", "inbound", "outbound" (default: "both")
- `terminators` (`std::string`): DTMF keys to stop recording
- `beep` (`bool`): Play beep before recording (default: `false`)
- `input_sensitivity` (`double`): Input sensitivity level (default: 44.0)
- `initial_timeout` (`std::optional<double>`): Initial timeout in seconds
- `end_silence_timeout` (`std::optional<double>`): End silence timeout in seconds
- `max_length` (`std::optional<double>`): Maximum recording length in seconds
- `status_url` (`std::string`): Webhook URL for recording status

**Usage:**
```cpp
// Basic recording
result.record_call(/*control_id=*/"", /*stereo=*/false, "mp3", "both");

// Recording with control ID and settings
result.record_call(
    "customer_call_001",
    /*stereo=*/true,
    "wav",
    "both",
    "#*",
    /*beep=*/true,
    /*input_sensitivity=*/44.0,
    /*initial_timeout=*/std::nullopt,
    /*end_silence_timeout=*/std::nullopt,
    /*max_length=*/300.0);
```

##### `stop_record_call(const std::string& control_id = "") -> FunctionResult&`
Stop call recording.

**Parameters:**
- `control_id` (`std::string`): Control ID of recording to stop

**Usage:**
```cpp
result.stop_record_call();
result.stop_record_call("customer_call_001");
```

### Conference and Room Management

##### `join_room(const std::string& name) -> FunctionResult&`
Join a SignalWire room.

**Parameters:**
- `name` (`std::string`): Room name to join

**Usage:**
```cpp
result.join_room("support_room_1");
```

##### `join_conference(const std::string& name, bool muted = false, const std::string& beep = "true", bool start_on_enter = true, bool end_on_exit = false, std::optional<std::string> wait_url = std::nullopt, int max_participants = 250, const std::string& record = "do-not-record", std::optional<std::string> region = std::nullopt, const std::string& trim = "trim-silence", std::optional<std::string> coach = std::nullopt, std::optional<std::string> status_callback_event = std::nullopt, std::optional<std::string> status_callback = std::nullopt, const std::string& status_callback_method = "POST", std::optional<std::string> recording_status_callback = std::nullopt, const std::string& recording_status_callback_method = "POST", const std::string& recording_status_callback_event = "completed", std::optional<json> result = std::nullopt) -> FunctionResult&`
Join a conference call.

**Parameters:**
- `name` (`std::string`): Conference name
- `muted` (`bool`): Join muted (default: `false`)
- `beep` (`std::string`): Beep setting: "true", "false", "onEnter", "onExit" (default: "true")
- `start_on_enter` (`bool`): Start conference when this participant enters (default: `true`)
- `end_on_exit` (`bool`): End conference when this participant exits (default: `false`)
- `wait_url` (`std::optional<std::string>`): URL for hold music/content
- `max_participants` (`int`): Maximum participants (default: 250)
- `record` (`std::string`): Recording setting (default: "do-not-record")
- `region` (`std::optional<std::string>`): SignalWire region
- `trim` (`std::string`): Trim setting for recordings (default: "trim-silence")
- `coach` (`std::optional<std::string>`): Coach participant identifier
- `status_callback_event` (`std::optional<std::string>`): Status callback events
- `status_callback` (`std::optional<std::string>`): Status callback URL
- `status_callback_method` (`std::string`): Status callback HTTP method (default: "POST")
- `recording_status_callback` (`std::optional<std::string>`): Recording status callback URL
- `recording_status_callback_method` (`std::string`): Recording status callback method (default: "POST")
- `recording_status_callback_event` (`std::string`): Recording status callback events (default: "completed")

In C++ the 18 optional parameters can be passed either as flat positional arguments (mirroring the signature above) or via the `JoinConferenceOptions` options-struct overload — `join_conference(const std::string& name, const JoinConferenceOptions& opts)`.

**Usage:**
```cpp
// Basic conference join
result.join_conference("sales_meeting");

// Conference with recording and settings (options-object overload)
signalwire::swaig::JoinConferenceOptions opts;
opts.muted = false;
opts.beep = "onEnter";
opts.record = "record-from-start";
opts.max_participants = 10;
result.join_conference("support_conference", opts);
```

### Payment Processing

##### `pay(const std::string& payment_connector_url, const std::string& input_method = "dtmf", const std::string& status_url = "", const std::string& payment_method = "credit-card", int timeout = 5, int max_attempts = 1, bool security_code = true, const std::string& postal_code = "true", int min_postal_code_length = 0, const std::string& token_type = "reusable", const std::string& charge_amount = "", const std::string& currency = "usd", const std::string& language = "en-US", const std::string& voice = "woman", const std::string& description = "", const std::string& valid_card_types = "visa mastercard amex", const std::vector<json>& parameters = {}, const std::vector<json>& prompts = {}) -> FunctionResult&`
Process a payment through the call.

**Parameters:**
- `payment_connector_url` (`std::string`): Payment processor webhook URL
- `input_method` (`std::string`): Input method: "dtmf", "speech" (default: "dtmf")
- `status_url` (`std::string`): Payment status webhook URL
- `payment_method` (`std::string`): Payment method: "credit-card" (default: "credit-card")
- `timeout` (`int`): Input timeout in seconds (default: 5)
- `max_attempts` (`int`): Maximum retry attempts (default: 1)
- `security_code` (`bool`): Require security code (default: `true`)
- `postal_code` (`std::string`): Require postal code (default: "true")
- `min_postal_code_length` (`int`): Minimum postal code length (default: 0)
- `token_type` (`std::string`): Token type: "reusable", "one-time" (default: "reusable")
- `charge_amount` (`std::string`): Amount to charge
- `currency` (`std::string`): Currency code (default: "usd")
- `language` (`std::string`): Language for prompts (default: "en-US")
- `voice` (`std::string`): Voice for prompts (default: "woman")
- `description` (`std::string`): Payment description
- `valid_card_types` (`std::string`): Accepted card types (default: "visa mastercard amex")
- `parameters` (`std::vector<json>`): Additional parameters
- `prompts` (`std::vector<json>`): Custom prompts

**Usage:**
```cpp
// Basic payment processing
result.pay(
    "https://payment-processor.com/webhook",
    /*input_method=*/"dtmf",
    /*status_url=*/"",
    /*payment_method=*/"credit-card",
    /*timeout=*/5,
    /*max_attempts=*/1,
    /*security_code=*/true,
    /*postal_code=*/"true",
    /*min_postal_code_length=*/0,
    /*token_type=*/"reusable",
    /*charge_amount=*/"29.99",
    /*currency=*/"usd",
    /*language=*/"en-US",
    /*voice=*/"woman",
    /*description=*/"Monthly subscription");
```

### Call Monitoring

##### `tap(const std::string& uri, const std::string& control_id = "", const std::string& direction = "both", const std::string& codec = "PCMU", int rtp_ptime = 20, const std::string& status_url = "") -> FunctionResult&`
Start call tapping/monitoring.

**Parameters:**
- `uri` (`std::string`): URI to send tapped audio to
- `control_id` (`std::string`): Unique identifier for this tap
- `direction` (`std::string`): Tap direction: "speak", "hear", "both" (default: "both")
- `codec` (`std::string`): Audio codec: "PCMU", "PCMA" (default: "PCMU")
- `rtp_ptime` (`int`): RTP packet time in milliseconds (default: 20)
- `status_url` (`std::string`): Status webhook URL

**Usage:**
```cpp
// Basic call tapping
result.tap("sip:monitor@company.com");

// Tap with specific settings
result.tap(
    "sip:quality@company.com",
    "quality_monitor_001",
    "both",
    "PCMA");
```

##### `stop_tap(const std::string& control_id = "") -> FunctionResult&`
Stop call tapping.

**Parameters:**
- `control_id` (`std::string`): Control ID of tap to stop

**Usage:**
```cpp
result.stop_tap();
result.stop_tap("quality_monitor_001");
```

### Advanced SWML Execution

##### `execute_swml(const json& swml_content, bool transfer = false) -> FunctionResult&`
Execute custom SWML content.

**Parameters:**
- `swml_content` (`json`): SWML document or content to execute
- `transfer` (`bool`): Whether this is a transfer operation (default: `false`)

**Usage:**
```cpp
// Execute custom SWML
json custom_swml = {
    {"version", "1.0.0"},
    {"sections", {
        {"main", json::array({
            {{"play", {{"url", "https://example.com/custom.mp3"}}}},
            {{"say", {{"text", "Custom SWML execution"}}}}
        })}
    }}
};
result.execute_swml(custom_swml);
```

### Utility Methods

##### `to_json() -> json`
Convert the result to a JSON object for serialization.

**Returns:**
- `json`: JSON representation of the result

**Usage:**
```cpp
result = signalwire::swaig::FunctionResult("Hello world");
result.add_action("play", "music.mp3");
json result_json = result.to_json();
std::cout << result_json.dump() << "\n";
// Output: {"response": "Hello world", "action": [{"play": "music.mp3"}]}
```

### Static Helper Methods

##### `static create_payment_prompt(const std::string& for_situation, const std::vector<json>& actions, const std::string& card_type = "", const std::string& error_type = "") -> json`
Create a payment prompt configuration.

**Parameters:**
- `for_situation` (`std::string`): Situation identifier
- `actions` (`std::vector<json>`): List of action configurations
- `card_type` (`std::string`): Card type for prompts
- `error_type` (`std::string`): Error type for error prompts

**Usage:**
```cpp
json prompt = signalwire::swaig::FunctionResult::create_payment_prompt(
    "card_number",
    {signalwire::swaig::FunctionResult::create_payment_action("say", "Please enter your card number")});
```

##### `static create_payment_action(const std::string& action_type, const std::string& phrase) -> json`
Create a payment action configuration.

**Parameters:**
- `action_type` (`std::string`): Action type
- `phrase` (`std::string`): Action phrase

**Usage:**
```cpp
json action = signalwire::swaig::FunctionResult::create_payment_action("say", "Enter your card number");
```

##### `static create_payment_parameter(const std::string& name, const std::string& value) -> json`
Create a payment parameter configuration.

**Parameters:**
- `name` (`std::string`): Parameter name
- `value` (`std::string`): Parameter value

**Usage:**
```cpp
json param = signalwire::swaig::FunctionResult::create_payment_parameter("merchant_id", "12345");
```

### Method Chaining

All action methods return `FunctionResult&`, enabling fluent method chaining:

```cpp
result = signalwire::swaig::FunctionResult("I'll help you with that")
    .set_post_process(true)
    .update_global_data({{"status", "helping"}})
    .set_end_of_speech_timeout(800)
    .add_action("play", "thinking.mp3");

// Complex workflow
auto result2 = signalwire::swaig::FunctionResult("Processing your payment")
    .set_post_process(true)
    .update_global_data({{"payment_status", "processing"}})
    .pay(
        "https://payments.com/webhook",
        /*input_method=*/"dtmf",
        /*status_url=*/"",
        /*payment_method=*/"credit-card",
        /*timeout=*/5,
        /*max_attempts=*/1,
        /*security_code=*/true,
        /*postal_code=*/"true",
        /*min_postal_code_length=*/0,
        /*token_type=*/"reusable",
        /*charge_amount=*/"99.99",
        /*currency=*/"usd",
        /*language=*/"en-US",
        /*voice=*/"woman",
        /*description=*/"Service payment")
    .send_sms(
        "+15551234567",
        "+15559876543",
        "Payment confirmation will be sent shortly");
```

This concludes Part 2 of the API reference covering the `swaig::FunctionResult` class. The document continues with DataMap and other components in subsequent parts.

---

## DataMap Class

The `DataMap` class provides a declarative approach to creating SWAIG tools that integrate with REST APIs without requiring webhook infrastructure. DataMap tools execute on SignalWire's server infrastructure, eliminating the need to expose webhook endpoints.

### Constructor

<!-- snippet: no-compile constructor signature listing (class-member declaration shown out of class context) -->
```cpp
explicit DataMap(const std::string& function_name);
```

**Parameters:**
- `function_name` (`std::string`): Name of the SWAIG function this DataMap will create

**Usage:**
```cpp
// Create a new DataMap tool
auto weather_map = signalwire::datamap::DataMap("get_weather");
auto search_map = signalwire::datamap::DataMap("search_docs");
```

### Core Configuration Methods

#### Function Metadata

##### `purpose(const std::string& desc) -> DataMap&`
Set the function description/purpose.

**Parameters:**
- `desc` (`std::string`): Human-readable description of what this function does

**Usage:**
```cpp
data_map = signalwire::datamap::DataMap("get_weather")
    .purpose("Get current weather information for any city");
```

##### `description(const std::string& desc) -> DataMap&`
Alias for `purpose()` — set the function description.

**Parameters:**
- `desc` (`std::string`): Function description

**Usage:**
```cpp
data_map = signalwire::datamap::DataMap("search_api")
    .description("Search our knowledge base for information");
```

#### Parameter Definition

##### `parameter(const std::string& name, const std::string& param_type, const std::string& desc, bool required = false, const std::vector<std::string>& enum_values = {}) -> DataMap&`
Add a function parameter with JSON schema validation.

**Parameters:**
- `name` (`std::string`): Parameter name
- `param_type` (`std::string`): JSON schema type: "string", "number", "boolean", "array", "object"
- `desc` (`std::string`): Parameter description for the AI
- `required` (`bool`): Whether parameter is required (default: `false`)
- `enum_values` (`std::vector<std::string>`): List of allowed values for validation

**Usage:**
```cpp
// Required string parameter
data_map.parameter("location", "string", "City name or ZIP code", /*required=*/true);

// Optional number parameter
data_map.parameter("days", "number", "Number of forecast days", /*required=*/false);

// Enum parameter with allowed values
data_map.parameter("units", "string", "Temperature units",
                   /*required=*/false, {"celsius", "fahrenheit"});

// Boolean parameter
data_map.parameter("include_alerts", "boolean", "Include weather alerts", /*required=*/false);

// Array parameter
data_map.parameter("categories", "array", "Search categories to include");
```

### API Integration Methods

#### HTTP Webhook Configuration

##### `webhook(const std::string& method, const std::string& url, const json& headers = json::object(), const std::string& form_param = "", bool input_args_as_params = false, const std::vector<std::string>& require_args = {}) -> DataMap&`
Configure an HTTP API call.

**Parameters:**
- `method` (`std::string`): HTTP method: "GET", "POST", "PUT", "DELETE", "PATCH"
- `url` (`std::string`): API endpoint URL (supports `${variable}` substitution)
- `headers` (`json`): HTTP headers to send
- `form_param` (`std::string`): Send JSON body as single form parameter with this name
- `input_args_as_params` (`bool`): Merge function arguments into URL parameters (default: `false`)
- `require_args` (`std::vector<std::string>`): Only execute if these arguments are present

**Variable Substitution in URLs:**
- `${args.parameter_name}`: Function argument values
- `${global_data.key}`: Call-wide data store (user info, call state - NOT credentials)
- `${meta_data.call_id}`: Call and function metadata

**Usage:**
```cpp
// Simple GET request with parameter substitution
data_map.webhook("GET", "https://api.weather.com/v1/current?key=API_KEY&q=${args.location}");

// POST request with authentication headers
data_map.webhook(
    "POST",
    "https://api.company.com/search",
    json{
        {"Authorization", "Bearer YOUR_TOKEN"},
        {"Content-Type", "application/json"}
    });

// Webhook that requires specific arguments
data_map.webhook(
    "GET",
    "https://api.service.com/data?id=${args.customer_id}",
    json::object(),
    /*form_param=*/"",
    /*input_args_as_params=*/false,
    /*require_args=*/{"customer_id"});

// Use global data for call-related info (NOT credentials)
data_map.webhook(
    "GET",
    "https://api.service.com/customer/${global_data.customer_id}/orders",
    json{{"Authorization", "Bearer YOUR_API_TOKEN"}});  // Use static credentials
```

##### `body(const json& data) -> DataMap&`
Set the JSON body for the last-added webhook (POST/PUT requests).

**Parameters:**
- `data` (`json`): JSON body data (supports `${variable}` substitution)

**Usage:**
```cpp
// Static body with parameter substitution
data_map.body({
    {"query", "${args.search_term}"},
    {"limit", 5},
    {"filters", {
        {"category", "${args.category}"},
        {"active", true}
    }}
});

// Body with call-related data (NOT sensitive info)
data_map.body({
    {"customer_id", "${global_data.customer_id}"},
    {"request_id", "${meta_data.call_id}"},
    {"search", "${args.query}"}
});
```

##### `params(const json& data) -> DataMap&`
Set request params for the last-added webhook (alias for `body`).

**Parameters:**
- `data` (`json`): Query parameters (supports `${variable}` substitution)

**Usage:**
```cpp
// URL parameters with substitution
data_map.params({
    {"api_key", "YOUR_API_KEY"},
    {"q", "${args.location}"},
    {"units", "${args.units}"},
    {"lang", "en"}
});
```

#### Multiple Webhooks and Fallbacks

DataMap supports multiple webhook configurations for fallback scenarios:

```cpp
// Primary API with fallback
data_map = signalwire::datamap::DataMap("search_with_fallback")
    .purpose("Search with multiple API fallbacks")
    .parameter("query", "string", "Search query", /*required=*/true)

    // Primary API
    .webhook("GET", "https://api.primary.com/search?q=${args.query}")
    .output(signalwire::swaig::FunctionResult("Primary result: ${response.title}"))

    // Fallback API
    .webhook("GET", "https://api.fallback.com/search?q=${args.query}")
    .output(signalwire::swaig::FunctionResult("Fallback result: ${response.title}"))

    // Final fallback if all APIs fail
    .fallback_output(signalwire::swaig::FunctionResult("Sorry, all search services are currently unavailable"));
```

### Response Processing

#### Basic Output

##### `output(const swaig::FunctionResult& result) -> DataMap&`
Set the response template for the most recent webhook.

**Parameters:**
- `result` (`swaig::FunctionResult`): Response template with variable substitution

**Variable Substitution in Outputs:**
- `${response.field}`: API response fields
- `${response.nested.field}`: Nested response fields
- `${response.array[0].field}`: Array element fields
- `${args.parameter}`: Original function arguments
- `${global_data.key}`: Call-wide data store (user info, call state)

**Usage:**
```cpp
// Simple response with an interpolated string
data_map.output(signalwire::swaig::FunctionResult(
    "Weather in ${args.location}: ${response.current.condition.text}, ${response.current.temp_f}°F"));

// Response with actions
data_map.output(
    signalwire::swaig::FunctionResult("Found ${response.total_results} results")
    .update_global_data({{"last_search", "${args.query}"}})
    .add_action("play", "search_complete.mp3"));

// Complex response with nested data
data_map.output(signalwire::swaig::FunctionResult(
    "Order ${response.order.id} status: ${response.order.status}. "
    "Estimated delivery: ${response.order.delivery.estimated_date}"));
```

##### `fallback_output(const swaig::FunctionResult& result) -> DataMap&`
Set the response when all webhooks fail.

**Parameters:**
- `result` (`swaig::FunctionResult`): Fallback response

**Usage:**
```cpp
data_map.fallback_output(
    signalwire::swaig::FunctionResult("Sorry, the service is temporarily unavailable. Please try again later.")
    .add_action("play", "service_unavailable.mp3"));
```

#### Array Processing

##### `foreach(const json& foreach_config) -> DataMap&`
Process array responses by iterating over elements.

**Parameters:**
- `foreach_config` (`json`): Array path (string) or configuration object

**Simple Array Processing:**
```cpp
// Process array of search results
data_map = signalwire::datamap::DataMap("search_docs")
    .webhook("GET", "https://api.docs.com/search?q=${args.query}")
    .foreach("${response.results}")  // Iterate over results array
    .output(signalwire::swaig::FunctionResult("Found: ${foreach.title} - ${foreach.summary}"));
```

**Advanced Array Processing:**
```cpp
// Complex foreach configuration
data_map.foreach(json{
    {"array", "${response.items}"},
    {"limit", 3},  // Process only first 3 items
    {"filter", {
        {"field", "status"},
        {"value", "active"}
    }}
});
```

**Foreach Variable Access:**
- `${foreach.field}`: Current array element field
- `${foreach.nested.field}`: Nested fields in current element
- `${foreach_index}`: Current iteration index (0-based)
- `${foreach_count}`: Total number of items being processed

### Pattern-Based Processing

#### Expression Matching

##### `expression(const std::string& test_value, const std::string& pattern, const swaig::FunctionResult& output, const swaig::FunctionResult* nomatch_output = nullptr) -> DataMap&`
Add pattern-based responses without API calls.

**Parameters:**
- `test_value` (`std::string`): Template string to test against pattern
- `pattern` (`std::string`): Regex pattern
- `output` (`swaig::FunctionResult`): Response when pattern matches
- `nomatch_output` (`const swaig::FunctionResult*`): Response when pattern doesn't match (optional; `nullptr` for none)

**Usage:**
```cpp
// Command-based responses
auto control_map = signalwire::datamap::DataMap("file_control")
    .purpose("Control file playback")
    .parameter("command", "string", "Playback command", /*required=*/true)
    .parameter("filename", "string", "File to control")

    // Start commands
    .expression(
        "${args.command}",
        "start|play|begin",
        signalwire::swaig::FunctionResult("Starting playback")
        .add_action("start_playback", json{{"file", "${args.filename}"}}))

    // Stop commands
    .expression(
        "${args.command}",
        "stop|pause|halt",
        signalwire::swaig::FunctionResult("Stopping playback")
        .add_action("stop_playback", true))

    // Volume commands
    .expression(
        "${args.command}",
        "volume (\\d+)",
        signalwire::swaig::FunctionResult("Setting volume to ${match.1}")
        .add_action("set_volume", "${match.1}"));
```

**Pattern Matching Variables:**
- `${match.0}`: Full match
- `${match.1}`, `${match.2}`, etc.: Capture groups
- `${match.group_name}`: Named capture groups

### Error Handling

##### `error_keys(const std::vector<std::string>& keys) -> DataMap&`
Specify response fields that indicate errors.

**Parameters:**
- `keys` (`std::vector<std::string>`): List of field names that indicate API errors

**Usage:**
```cpp
// Treat these response fields as errors
data_map.error_keys({"error", "error_message", "status_code"});

// If API returns {"error": "Not found"}, DataMap will treat this as an error
```

##### `global_error_keys(const std::vector<std::string>& keys) -> DataMap&`
Set global error keys for all webhooks in this DataMap.

**Parameters:**
- `keys` (`std::vector<std::string>`): Global error field names

**Usage:**
```cpp
data_map.global_error_keys({"error", "message", "code"});
```

### Advanced Configuration

##### `webhook_expressions(const std::vector<json>& expressions) -> DataMap&`
Add expression-based webhook selection.

**Parameters:**
- `expressions` (`std::vector<json>`): List of expression configurations

**Usage:**
```cpp
// Different APIs based on input
data_map.webhook_expressions({
    json{
        {"test", "${args.type}"},
        {"pattern", "weather"},
        {"webhook", {
            {"method", "GET"},
            {"url", "https://weather-api.com/current?q=${args.location}"}
        }}
    },
    json{
        {"test", "${args.type}"},
        {"pattern", "news"},
        {"webhook", {
            {"method", "GET"},
            {"url", "https://news-api.com/search?q=${args.query}"}
        }}
    }
});
```

### Complete DataMap Examples

#### Simple Weather API

```cpp
auto weather_tool = signalwire::datamap::DataMap("get_weather")
    .purpose("Get current weather information")
    .parameter("location", "string", "City name or ZIP code", /*required=*/true)
    .parameter("units", "string", "Temperature units", /*required=*/false, {"celsius", "fahrenheit"})
    .webhook("GET", "https://api.weather.com/v1/current?key=API_KEY&q=${args.location}&units=${args.units}")
    .output(signalwire::swaig::FunctionResult(
        "Weather in ${args.location}: ${response.current.condition.text}, ${response.current.temp_f}°F"))
    .error_keys({"error"});

// Register with agent
agent.register_swaig_function(weather_tool.to_swaig_function());
```

#### Search with Array Processing

```cpp
auto search_tool = signalwire::datamap::DataMap("search_knowledge")
    .purpose("Search company knowledge base")
    .parameter("query", "string", "Search query", /*required=*/true)
    .parameter("category", "string", "Search category", /*required=*/false, {"docs", "faq", "policies"})
    .webhook(
        "POST",
        "https://api.company.com/search",
        json{{"Authorization", "Bearer TOKEN"}})
    .body({
        {"query", "${args.query}"},
        {"category", "${args.category}"},
        {"limit", 5}
    })
    .foreach("${response.results}")
    .output(signalwire::swaig::FunctionResult("Found: ${foreach.title} - ${foreach.summary}"))
    .fallback_output(signalwire::swaig::FunctionResult("Search service is temporarily unavailable"));
```

#### Command Processing (No API)

```cpp
auto no_match = signalwire::swaig::FunctionResult("Please specify a valid action");
auto control_tool = signalwire::datamap::DataMap("system_control")
    .purpose("Control system functions")
    .parameter("action", "string", "Action to perform", /*required=*/true)
    .parameter("target", "string", "Target for the action")

    // Restart commands
    .expression(
        "${args.action}",
        "restart|reboot",
        signalwire::swaig::FunctionResult("Restarting ${args.target}")
        .add_action("restart_service", json{{"service", "${args.target}"}}))

    // Status commands
    .expression(
        "${args.action}",
        "status|check",
        signalwire::swaig::FunctionResult("Checking status of ${args.target}")
        .add_action("check_status", json{{"service", "${args.target}"}}))

    // Default for unrecognized commands
    .expression(
        "${args.action}",
        ".*",
        signalwire::swaig::FunctionResult("Unknown command: ${args.action}"),
        &no_match);
```

### Conversion and Registration

##### `to_swaig_function() -> json`
Convert the DataMap to a SWAIG function definition for registration.

**Returns:**
- `json`: Complete SWAIG function definition

**Usage:**
```cpp
// Build DataMap
auto weather_map = signalwire::datamap::DataMap("get_weather")
    .purpose("Get weather")
    .parameter("location", "string", "City", /*required=*/true);

// Convert to SWAIG function and register
json swaig_function = weather_map.to_swaig_function();
agent.register_swaig_function(swaig_function);
```

### Building Common Patterns

The C++ SDK does not ship the Python module-level `create_simple_api_tool` / `create_expression_tool` helpers. Build the same tools directly with the fluent `datamap::DataMap` builder:

**Simple API integration tool:**
```cpp
auto weather = signalwire::datamap::DataMap("get_weather")
    .parameter("location", "string", "City name", /*required=*/true)
    .webhook("GET", "https://api.weather.com/v1/current?key=API_KEY&q=${location}")
    .output(signalwire::swaig::FunctionResult(
        "Weather in ${location}: ${response.current.condition.text}"));

agent.register_swaig_function(weather.to_swaig_function());
```

**Pattern-based tool without API calls:**
```cpp
auto file_control = signalwire::datamap::DataMap("file_control")
    .parameter("command", "string", "Playback command", /*required=*/true)
    .expression("${args.command}", "start.*",
                signalwire::swaig::FunctionResult().add_action("start_playback", true))
    .expression("${args.command}", "stop.*",
                signalwire::swaig::FunctionResult().add_action("stop_playback", true));

agent.register_swaig_function(file_control.to_swaig_function());
```

### Method Chaining

All DataMap methods return `DataMap&`, enabling fluent method chaining:

```cpp
auto complete_tool = signalwire::datamap::DataMap("comprehensive_search")
    .purpose("Comprehensive search with fallbacks")
    .parameter("query", "string", "Search query", /*required=*/true)
    .parameter("category", "string", "Search category", /*required=*/false, {"all", "docs", "faq"})
    .webhook("GET", "https://primary-api.com/search?q=${args.query}&cat=${args.category}")
    .output(signalwire::swaig::FunctionResult("Primary: ${response.title}"))
    .webhook("GET", "https://backup-api.com/search?q=${args.query}")
    .output(signalwire::swaig::FunctionResult("Backup: ${response.title}"))
    .fallback_output(signalwire::swaig::FunctionResult("All search services unavailable"))
    .error_keys({"error", "message"});
```

This concludes Part 3 of the API reference covering the DataMap class. The document will continue with Context System and other components in subsequent parts. 

---

## Context System

The Context System enhances traditional prompt-based agents by adding structured workflows with sequential steps on top of a base prompt. Each step contains its own guidance, completion criteria, and function restrictions while building upon the agent's foundational prompt.

### ContextBuilder Class

The `ContextBuilder` is accessed via `agent.define_contexts()` and provides the main interface for creating structured workflows.

#### Getting Started

```cpp
// Access the context builder
auto& contexts = agent.define_contexts();

// Create contexts and steps
contexts.add_context("greeting")
    .add_step("welcome")
    .set_text("Welcome! How can I help you today?")
    .set_step_criteria("User has stated their need")
    .set_valid_steps({"next"});
```

##### `add_context(const std::string& name) -> Context&`
Create a new context in the workflow.

**Parameters:**
- `name` (`std::string`): Unique context name

**Returns:**
- `Context&`: Context object for method chaining

**Usage:**
```cpp
auto& contexts = agent.define_contexts();

// Create multiple contexts
auto& greeting_context = contexts.add_context("greeting");
auto& main_menu_context = contexts.add_context("main_menu");
auto& support_context = contexts.add_context("support");
```

### Context Class

The `Context` class represents a conversation context containing multiple steps. Its principal methods (all return `Context&` for chaining unless noted):

<!-- snippet: no-compile class-shape reference (member-signature listing with elided defaults, not standalone code) -->
```cpp
class Context {
    // Create a new step in this context (returns Step&)
    Step& add_step(const std::string& name, const std::string& task = "", /* ... */);

    // Set which contexts can be accessed from this context
    Context& set_valid_contexts(const std::vector<std::string>& ctxs);

    // Context entry parameters (for context-switching behavior)
    Context& set_post_prompt(const std::string& pp);      // Override agent's post prompt
    Context& set_system_prompt(const std::string& sp);    // Trigger a context switch
    Context& set_consolidate(bool c);                     // Consolidate history on entry
    Context& set_full_reset(bool fr);                     // Full replacement vs injection
    Context& set_user_prompt(const std::string& up);      // Inject a user message on entry

    // Context prompts (guidance for all steps in the context)
    Context& set_prompt(const std::string& prompt);       // Simple string prompt
    Context& add_section(const std::string& title, const std::string& body);
    Context& add_bullets(const std::string& title, const std::vector<std::string>& bullets);
};
```

**Context Types:**

1. **Workflow Container Context** (no `system_prompt`): Organizes steps without conversation state changes
2. **Context Switch Context** (has `system_prompt`): Triggers conversation state changes when entered, processing entry parameters like a `context_switch` SWAIG action

**Prompt Hierarchy:** Base Agent Prompt → Context Prompt → Step Prompt

#### Usage Examples

```cpp
auto& contexts = agent.define_contexts();

// Workflow container context (just organizes steps)
auto& main_context = contexts.add_context("main");
main_context.set_prompt("Follow standard customer service protocols");

// Context switch context (changes AI behavior)
auto& billing_context = contexts.add_context("billing");
billing_context.set_system_prompt("You are now a billing specialist")
    .set_consolidate(true)
    .set_user_prompt("Customer needs billing assistance")
    .add_section("Department", "Billing Department")
    .add_bullets("Services", {"Account inquiries", "Payments", "Refunds"});

// Full reset context (complete conversation reset)
auto& manager_context = contexts.add_context("manager");
manager_context.set_system_prompt("You are a senior manager")
    .set_full_reset(true)
    .set_consolidate(true);
```

---

## Skills System

The Skills System provides modular, reusable capabilities that can be easily added to any agent.

### Available Built-in Skills

#### `datetime` Skill
Provides current date and time information.

**Parameters:**
- `timezone` (string, optional): Timezone for date/time (default: system timezone)
- `format` (string, optional): Custom date/time format string

**Usage:**
```cpp
// Basic datetime skill
agent.add_skill("datetime");

// With timezone
agent.add_skill("datetime", {{"timezone", "America/New_York"}});

// With custom format
agent.add_skill("datetime", {
    {"timezone", "UTC"},
    {"format", "%Y-%m-%d %H:%M:%S %Z"}
});
```

#### `math` Skill
Safe mathematical expression evaluation.

**Parameters:**
- `precision` (int, optional): Decimal precision for results (default: 2)
- `max_expression_length` (int, optional): Maximum expression length (default: 100)

**Usage:**
```cpp
// Basic math skill
agent.add_skill("math");

// With custom precision
agent.add_skill("math", {{"precision", 4}});
```

#### `web_search` Skill
Google Custom Search API integration with web scraping.

**Parameters:**
- `api_key` (string, required): Google Custom Search API key
- `search_engine_id` (string, required): Google Custom Search Engine ID
- `num_results` (int, optional): Number of results to return (default: 3)
- `tool_name` (string, optional): Custom tool name for multiple instances
- `delay` (number, optional): Delay between requests in seconds
- `no_results_message` (string, optional): Custom message when no results found

**Usage:**
```cpp
// Basic web search
agent.add_skill("web_search", {
    {"api_key", "your-google-api-key"},
    {"search_engine_id", "your-search-engine-id"}
});

// Multiple search instances
agent.add_skill("web_search", {
    {"api_key", "your-api-key"},
    {"search_engine_id", "general-engine-id"},
    {"tool_name", "search_general"},
    {"num_results", 5}
});

agent.add_skill("web_search", {
    {"api_key", "your-api-key"},
    {"search_engine_id", "news-engine-id"},
    {"tool_name", "search_news"},
    {"num_results", 3},
    {"delay", 0.5}
});
```

#### `datasphere` Skill
SignalWire DataSphere knowledge search integration.

**Parameters:**
- `space_name` (string, required): DataSphere space name
- `project_id` (string, required): DataSphere project ID
- `token` (string, required): DataSphere access token
- `document_id` (string, optional): Specific document to search
- `tool_name` (string, optional): Custom tool name for multiple instances
- `count` (int, optional): Number of results to return (default: 3)
- `tags` (array of string, optional): Filter by document tags

**Usage:**
```cpp
// Basic DataSphere search
agent.add_skill("datasphere", {
    {"space_name", "my-space"},
    {"project_id", "my-project"},
    {"token", "my-token"}
});

// Multiple DataSphere instances
agent.add_skill("datasphere", {
    {"space_name", "my-space"},
    {"project_id", "my-project"},
    {"token", "my-token"},
    {"document_id", "drinks-menu"},
    {"tool_name", "search_drinks"},
    {"count", 5}
});

agent.add_skill("datasphere", {
    {"space_name", "my-space"},
    {"project_id", "my-project"},
    {"token", "my-token"},
    {"tool_name", "search_policies"},
    {"tags", json::array({"HR", "Policies"})}
});
```

#### `native_vector_search` Skill
Local document search with vector similarity and keyword search.

**Parameters:**
- `index_path` (string, required): Path to search index file
- `tool_name` (string, optional): Custom tool name (default: "search_documents")
- `max_results` (int, optional): Maximum results to return (default: 5)
- `similarity_threshold` (number, optional): Minimum similarity score 0.0-1.0 (default: 0.0). Higher values are stricter, lower values are more permissive. Typical range: 0.2-0.4 for all-MiniLM-L6-v2, 0.3-0.5 for all-mpnet-base-v2

**Usage:**
```cpp
// Basic local search
agent.add_skill("native_vector_search", {
    {"index_path", "./knowledge.swsearch"}
});

// With custom settings
agent.add_skill("native_vector_search", {
    {"index_path", "./docs.swsearch"},
    {"tool_name", "search_docs"},
    {"max_results", 10},
    {"similarity_threshold", 0.25}
});
```

### Creating Custom Skills

#### Skill Structure

Create a new skill by extending `skills::SkillBase` and overriding its virtual methods:

```cpp
#include <signalwire/signalwire.hpp>

using namespace signalwire;
using json = nlohmann::json;

class CustomSkill : public signalwire::skills::SkillBase {
 public:
    std::string skill_name() const override { return "custom_skill"; }
    std::string skill_description() const override {
        return "Description of what this skill does";
    }
    std::string skill_version() const override { return "1.0.0"; }
    std::vector<std::string> required_env_vars() const override { return {"API_KEY"}; }

    // Validate and store configuration
    bool setup(const json& params) override {
        api_key_ = params.value("api_key", "");
        return !api_key_.empty();
    }

    // DataMap-based skills expose their functions via get_datamap_functions()
    std::vector<json> get_datamap_functions() const override {
        auto tool = signalwire::datamap::DataMap("custom_function")
            .description("Custom API integration")
            .parameter("query", "string", "Search query", /*required=*/true)
            .webhook("GET", "https://api.example.com/search?key=" + api_key_ + "&q=${args.query}")
            .output(signalwire::swaig::FunctionResult("Found: ${response.title}"));
        return {tool.to_swaig_function()};
    }

    // Function-handler skills return ToolDefinitions here instead
    std::vector<signalwire::swaig::ToolDefinition> register_tools() override { return {}; }

    // Speech recognition hints
    std::vector<std::string> get_hints() const override {
        return {"custom search", "find information"};
    }

    // Global data for DataMap substitution
    json get_global_data() const override {
        return json{{"skill_version", skill_version()}};
    }

    // Prompt sections to add
    std::vector<signalwire::skills::SkillPromptSection> get_prompt_sections() const override {
        return {{"Custom Search Capability",
                 "You can search our custom database for information.",
                 {"Use the custom_function to search", "Results are real-time"}}};
    }

 private:
    std::string api_key_;
};
```

#### Skill Registration

Register the skill's class with the `skills::SkillRegistry` (the `REGISTER_SKILL` macro wires up a factory keyed on `skill_name()`), then attach an instance to an agent by name with `add_skill(...)`:

<!-- snippet: no-compile references CustomSkill, the reader's SkillBase subclass defined in the preceding example (cross-block) -->
```cpp
// At file scope: auto-register the factory with the global registry.
REGISTER_SKILL(CustomSkill)

// Elsewhere: attach an instance to an agent by name.
agent.add_skill("custom_skill", {{"api_key", "your-api-key"}});
```

Equivalently, register a factory lambda directly:
<!-- snippet: no-compile references CustomSkill, the reader's SkillBase subclass defined in the preceding example (cross-block) -->
```cpp
signalwire::skills::SkillRegistry::instance().register_skill(
    "custom_skill",
    []() -> std::unique_ptr<signalwire::skills::SkillBase> { return std::make_unique<CustomSkill>(); });
```

---

## Utility Classes

### ToolDefinition Struct

Represents a SWAIG function (tool) definition with its metadata and handler. This is the C++ equivalent of the Python `SWAIGFunction`.

#### Fields

```cpp
struct ToolDefinition {
    std::string name;          // Function name
    std::string description;   // Function description
    json parameters;           // JSON schema for parameters
    signalwire::swaig::ToolHandler handler;// Lambda invoked when the function is called
    bool secure = false;       // Require request signing

    // Render to the SWAIG function JSON format (for inclusion in SWML)
    json to_swaig_json(const std::string& web_hook_url = "") const;
};
```

#### Usage

Usually you construct tools through `agent.define_tool(...)`, but you can also build a `ToolDefinition` directly and register it:

```cpp
signalwire::swaig::ToolDefinition tool;
tool.name = "get_weather";
tool.description = "Get current weather";
tool.parameters = {
    {"type", "object"},
    {"properties", {
        {"location", {{"type", "string"}, {"description", "City name"}}}
    }},
    {"required", json::array({"location"})}
};
tool.secure = true;
tool.handler = [](const json& args, const json& raw) -> signalwire::swaig::FunctionResult {
    (void)raw;
    return signalwire::swaig::FunctionResult("Checking weather for " + args.value("location", "") + "...");
};

// Register with agent
agent.define_tool(tool);
```

### SWMLService Class

Base class providing SWML document generation and HTTP service capabilities. `AgentBase` extends this class.

#### Key Methods

##### `render_swml() -> std::string`
Render the complete SWML document for the service as a JSON string.

##### `document() -> swml::Document&`
Access the underlying SWML document being built.

### Dynamic Configuration

The dynamic configuration callback receives a mutable agent handle directly, allowing you to configure it based on request data.

**Usage:**
```cpp
auto dynamic_config = [](const std::map<std::string, std::string>& query_params,
                         const json& body_params,
                         const std::map<std::string, std::string>& headers,
                         signalwire::agent::AgentBase& cfg) {
    (void)body_params;

    // Configure based on request
    auto lang_it = query_params.find("lang");
    if (lang_it != query_params.end() && lang_it->second == "es") {
        signalwire::agent::LanguageConfig es{"Spanish", "es-ES", "nova.luna"};
        cfg.add_language(es);
    }

    // Customer-specific configuration
    auto cust_it = headers.find("X-Customer-ID");
    if (cust_it != headers.end()) {
        const std::string& customer_id = cust_it->second;
        cfg.set_global_data({{"customer_id", customer_id}});
        cfg.prompt_add_section("Customer Context", "You are helping customer " + customer_id);
    }

    // Add skills dynamically
    auto search_it = query_params.find("enable_search");
    if (search_it != query_params.end() && search_it->second == "true") {
        cfg.add_skill("web_search", {{"provider", "google"}});
    }
};

agent.set_dynamic_config_callback(dynamic_config);
```

---

## Environment Variables

The SDK supports various environment variables for configuration:

### Authentication
- `SWML_BASIC_AUTH_USER`: Basic auth username
- `SWML_BASIC_AUTH_PASSWORD`: Basic auth password

### SSL/HTTPS
- `SWML_SSL_ENABLED`: Enable SSL (true/false)
- `SWML_SSL_CERT_PATH`: Path to SSL certificate
- `SWML_SSL_KEY_PATH`: Path to SSL private key
- `SWML_DOMAIN`: Domain name for SSL

### Proxy Support
- `SWML_PROXY_URL_BASE`: Base URL for proxy server

### Skills Configuration
- `GOOGLE_SEARCH_API_KEY`: Google Custom Search API key
- `GOOGLE_SEARCH_ENGINE_ID`: Google Custom Search Engine ID
- `SIGNALWIRE_SPACE_NAME`: DataSphere space name (fallback for the `space_name` skill param)
- `SIGNALWIRE_PROJECT_ID`: DataSphere project ID (fallback for the `project_id` skill param)
- `SIGNALWIRE_TOKEN` / `DATASPHERE_TOKEN`: DataSphere access token (fallback for the `token` skill param)
- `SIGNALWIRE_SKILL_PATHS`: Colon-separated list of directories to add to the skill registry's external search path (see [Third-Party Skills](third_party_skills.md)).

### RELAY Transport (advanced / testing)
- `SIGNALWIRE_RELAY_SCHEME`: Override the RELAY WebSocket transport scheme. Production always uses TLS (`wss://`) and needs no setting; set this to a plain form only to point the RELAY client at a non-TLS local/dev or audit-fixture server. Leave unset in production.

### Usage

```cpp
#include <cstdlib>
#include <signalwire/agent/agent_base.hpp>

// Set environment variables (or set them in the shell before launch)
setenv("SWML_BASIC_AUTH_USER", "admin", 1);
setenv("SWML_BASIC_AUTH_PASSWORD", "secret", 1);
setenv("GOOGLE_SEARCH_API_KEY", "your-api-key", 1);

// Agent will automatically use these
signalwire::agent::AgentBase agent("My Agent");
agent.add_skill("web_search", {
    {"search_engine_id", "your-engine-id"}
    // api_key will be read from the environment
});
```

---

## Complete Example

Here's a comprehensive example using multiple SDK components:

```cpp
#include <signalwire/signalwire.hpp>
#include <signalwire/agent/agent_base.hpp>
#include <iostream>

using namespace signalwire;
using json = nlohmann::json;

class ComprehensiveAgent : public signalwire::agent::AgentBase {
 public:
    ComprehensiveAgent() : AgentBase("Comprehensive Agent", "/") {
        // Configure voice and language
        signalwire::agent::LanguageConfig english{"English", "en-US", "rime.spore"};
        english.speech_fillers = {"Let me check...", "One moment..."};
        add_language(english);

        // Add speech recognition hints
        add_hints({"SignalWire", "customer service", "technical support"});

        // Configure AI parameters
        set_params({
            {"ai_model", "gpt-4.1-nano"},
            {"end_of_speech_timeout", 800},
            {"temperature", 0.7}
        });

        // Add skills
        add_skill("datetime");
        add_skill("math");
        add_skill("web_search", {
            {"api_key", "your-google-api-key"},
            {"search_engine_id", "your-engine-id"},
            {"num_results", 3}
        });

        // Set up structured workflow
        setup_contexts();

        // Add custom tools
        register_custom_tools();

        // Set global data
        set_global_data({
            {"company_name", "Acme Corp"},
            {"support_hours", "9 AM - 5 PM EST"},
            {"version", "2.0"}
        });

        // Handle conversation summaries
        on_summary([](const json& summary, const json& raw_data) {
            (void)raw_data;
            std::cout << "Conversation completed: " << summary.dump() << "\n";
            // Could save to database, send notifications, etc.
        });

        // Transfer-to-billing tool
        define_tool(
            "transfer_to_billing", "Transfer call to billing department",
            json{{"type", "object"}, {"properties", json::object()}},
            [](const json& args, const json& raw) -> signalwire::swaig::FunctionResult {
                (void)args;
                (void)raw;
                return signalwire::swaig::FunctionResult("Transferring you to our billing department")
                    .update_global_data({{"last_action", "transfer_to_billing"}})
                    .connect("billing@company.com", /*final=*/false);
            });
    }

 private:
    void setup_contexts() {
        auto& contexts = define_contexts();

        // Greeting context
        auto& greeting = contexts.add_context("greeting");
        greeting.add_step("welcome")
            .set_text("Hello! Welcome to Acme Corp support. How can I help you today?")
            .set_step_criteria("Customer has explained their issue")
            .set_valid_steps({"categorize"});

        greeting.add_step("categorize")
            .add_section("Current Task", "Categorize the customer's request")
            .add_bullets("Categories", {
                "Technical issue - use diagnostic tools",
                "Billing question - transfer to billing",
                "General inquiry - handle directly"
            })
            .set_functions(std::vector<std::string>{"transfer_to_billing", "run_diagnostics"})
            .set_step_criteria("Request categorized and action taken");

        // Technical support context
        auto& tech = contexts.add_context("technical_support");
        tech.add_step("diagnose")
            .set_text("Let me run some diagnostics to identify the issue.")
            .set_functions(std::vector<std::string>{"run_diagnostics", "check_system_status"})
            .set_step_criteria("Diagnostics completed")
            .set_valid_steps({"resolve"});

        tech.add_step("resolve")
            .set_text("Based on the diagnostics, here's how we'll fix this.")
            .set_functions(std::vector<std::string>{"apply_fix", "schedule_technician"})
            .set_step_criteria("Issue resolved or escalated");
    }

    void register_custom_tools() {
        // Customer lookup tool
        auto lookup_tool = signalwire::datamap::DataMap("lookup_customer")
            .description("Look up customer information")
            .parameter("customer_id", "string", "Customer ID", /*required=*/true)
            .webhook("GET", "https://api.company.com/customers/${args.customer_id}",
                     json{{"Authorization", "Bearer YOUR_TOKEN"}})
            .output(signalwire::swaig::FunctionResult("Customer: ${response.name}, Status: ${response.status}"))
            .error_keys({"error"});

        register_swaig_function(lookup_tool.to_swaig_function());

        // System control tool
        auto control_tool = signalwire::datamap::DataMap("system_control")
            .description("Control system functions")
            .parameter("action", "string", "Action to perform", /*required=*/true)
            .parameter("target", "string", "Target system")
            .expression("${args.action}", "restart|reboot",
                        signalwire::swaig::FunctionResult("Restarting ${args.target}")
                        .add_action("restart_system", json{{"target", "${args.target}"}}))
            .expression("${args.action}", "status|check",
                        signalwire::swaig::FunctionResult("Checking ${args.target} status")
                        .add_action("check_status", json{{"target", "${args.target}"}}));

        register_swaig_function(control_tool.to_swaig_function());
    }
};

// Run the agent
int main() {
    ComprehensiveAgent agent;
    agent.run();
    return 0;
}
```

This concludes the complete API reference for the SignalWire AI Agents SDK. The SDK provides a comprehensive framework for building sophisticated AI agents with modular capabilities, structured workflows, persistent state, and deployment across multiple environments.