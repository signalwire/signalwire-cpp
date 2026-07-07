# SignalWire AI Agents SDK: Why the SDK, Not Raw SWML

<!-- snippet-setup -->
```cpp
#include <signalwire/agent/agent_base.hpp>
#include <signalwire/swaig/function_result.hpp>
#include <signalwire/datamap/datamap.hpp>
#include <signalwire/contexts/contexts.hpp>
#include <nlohmann/json.hpp>
#include <iostream>
#include <string>

using json = nlohmann::json;
signalwire::agent::AgentBase agent("my-agent");
```

## The Problem with Raw SWML

SWML (SignalWire Markup Language) is a JSON document format that defines how an agent behaves during a call -- 30+ verbs, an AI verb with dozens of parameters, SWAIG (SignalWire AI Gateway) function definitions with JSON Schema, post-prompt URLs, webhook authentication, language arrays, pronunciation rules, hints, global data, contexts, steps, gather configs. Writing it by hand means constructing deeply nested JSON, manually building authenticated webhook URLs, hand-coding parameter schemas, and deploying separate webhook servers for your tools. Every agent becomes a bespoke JSON engineering project.

The SDK eliminates all of this. You write C++. The SDK generates correct SWML, serves it over HTTP, and handles its own webhook callbacks -- all in one process, deployable to any platform.

---

## The Self-Referencing Pipeline

The SDK's core architectural insight is that the agent is both the **SWML generator** and the **SWAIG webhook handler** in a single stateless microservice.

```
SignalWire requests SWML → Agent generates document
  ↓
SWML contains webhook URLs → URLs point back to the agent itself
  ↓
AI calls a function → SignalWire POSTs to agent's /swaig/ endpoint
  ↓
Agent executes function locally → Returns result to AI
  ↓
Call ends → SignalWire POSTs analytics to agent's /post_prompt/ endpoint
```

The agent auto-detects its own public URL -- including behind ngrok, load balancers, API Gateway, or any reverse proxy (via `X-Forwarded-Host`, `Forwarded` header, or `SWML_PROXY_URL_BASE` env var). It embeds Basic Auth credentials directly into the webhook URLs. It generates per-call security tokens for each function. The developer writes none of this:

```cpp
#include <signalwire/agent/agent_base.hpp>
#include <nlohmann/json.hpp>

using namespace signalwire;
using json = nlohmann::json;

class WeatherAgent : public signalwire::agent::AgentBase {
public:
    WeatherAgent() : AgentBase("weather", "/weather") {
        prompt_add_section("Role", "You help with weather.");

        define_tool(
            "get_weather", "Get weather",
            {{"type", "object"},
             {"properties", {{"city", {{"type", "string"}}}}},
             {"required", {"city"}}},
            [](const json& args, const json& raw_data) {
                std::string city = args.at("city");
                // ... fetch weather ...
                return signalwire::swaig::FunctionResult("72°F and sunny in " + city);
            });
    }
};

int main() {
    WeatherAgent agent;
    agent.run();
}
```

That's a complete agent: HTTP server, SWML generation, authenticated webhook routing, function execution, and response formatting. The generated SWML contains the full AI configuration, function schemas, and webhook URLs pointing back to the running process -- all computed automatically.

---

## Prompt Object Model (POM)

Raw SWML prompts are flat strings. The SDK provides structured prompt building:

```cpp
agent.prompt_add_section("Role", "You are a travel booking assistant.");
agent.prompt_add_section("Rules", "",
    {"Never make up flight information",
     "Always confirm before booking",
     "Use the search tool for real data"});
agent.prompt_add_section("Personality", "Friendly but professional.");
```

POM sections are rendered by the platform into a format the LLM understands with proper hierarchy. You can add subsections, append to existing sections, check if sections exist, and compose prompts programmatically -- including from skills that inject their own sections.

---

## Tools: Three Ways

### 1. Decorated Functions (Local Execution)

<!-- snippet: no-compile illustrative handler body calls the reader's own `db` accessor / order model -->
```cpp
agent.define_tool(
    "lookup_order", "Look up an order",
    {{"type", "object"},
     {"properties", {{"order_id", {{"type", "string"}}}}},
     {"required", {"order_id"}}},
    [](const json& args, const json& raw_data) {
        auto order = db.get(args.at("order_id"));
        return signalwire::swaig::FunctionResult("Order " + order.id + ": " + order.status)
            .update_global_data({{"current_order", order.to_json()}});
    });
```

The SDK converts this into a SWAIG function definition with JSON Schema parameters, creates a secure webhook URL, routes inbound POST requests to the handler, parses arguments, and formats the response -- including the 20+ SWAIG actions (transfer, hold, context_switch, toggle_functions, etc.) that tools can return.

The handler is a `swaig::ToolHandler` -- a `std::function<swaig::FunctionResult(const json& args, const json& raw_data)>`. It receives the parsed arguments and the full inbound `raw_data`, and returns a `swaig::FunctionResult` whose response text and platform actions the SDK renders into the callback response.

### 2. DataMap (Server-Side Execution)

```cpp
auto data_map = signalwire::datamap::DataMap("check_stock")
    .purpose("Check product stock levels")
    .parameter("sku", "string", "Product SKU", /*required=*/true)
    .webhook("GET", "https://api.warehouse.com/stock/${args.sku}")
    .output(signalwire::swaig::FunctionResult("Stock for ${args.sku}: ${response.quantity} units"))
    .fallback_output(signalwire::swaig::FunctionResult("Could not check stock right now"));

agent.register_swaig_function(data_map.to_swaig_function());
```

DataMap tools execute on SignalWire's servers -- no webhook needed. The SDK generates the `data_map` structure in the SWML with variable expansion (`${args.*}`, `${response.*}`, `${global_data.*}`), foreach iteration, expression matching, and error handling. Your agent never receives the callback; SignalWire handles the entire API call.

### 3. Skills (Packaged Integrations)

```cpp
agent.add_skill("web_search", {{"api_key", "..."}, {"engine_id", "..."}});
agent.add_skill("datetime");
agent.add_skill("math");
```

One line. The skill auto-registers its tools, injects prompt sections, adds speech hints, and validates dependencies. No manual wiring.

---

## The Skills System

Skills are self-contained modules that package tools, prompts, hints, and configuration into a single `add_skill()` call. Each skill:

- Inherits from `signalwire::skills::SkillBase` with required `setup()` and `register_tools()` overrides
- Overrides `required_packages()` and `required_env_vars()` for dependency validation
- Calls `define_tool()` and returns the resulting `swaig::ToolDefinition`s from `register_tools()`
- Can inject prompt sections via `get_prompt_sections()`
- Can provide speech hints via `get_hints()`
- Can contribute global data via `get_global_data()`
- Supports multiple instances with different configs (override `supports_multiple_instances()` -- e.g., two `web_search` skills with different engines)

**Built-in skills:** `datetime`, `math`, `web_search`, `wikipedia_search`, `weather_api`, `google_maps`, `datasphere`, `datasphere_serverless`, `native_vector_search`, `spider`, `mcp_gateway`, `swml_transfer`, `play_background_file`, `info_gatherer`, `api_ninjas_trivia`, `joke`, `claude_skills`.

The elegance is composability: skills don't know about each other, but they all register cleanly into the same agent. A single agent can combine web search, datetime, a custom booking tool, and a DataMap stock checker -- all declared in `__init__`, all generating correct SWML with proper function definitions, all routed to the right handler.

---

## Contexts and Steps: Priming the State Machine

The contexts/steps system lets you define structured workflows declaratively. Instead of hoping the LLM follows instructions about conversation flow, you mechanically enforce it:

```cpp
auto& greeting = agent.define_contexts().add_context("default");

greeting.add_step("welcome")
    .set_text("Greet the user and ask how you can help.")
    .set_valid_steps({"collect_info"})
    .set_functions({"check_hours"});  // Only this tool available here

greeting.add_step("collect_info")
    .set_text("Collect the user's name and email.")
    .set_step_criteria("User has provided both name and email")
    .set_gather_info("user_profile")
    .add_gather_question("name", "What is your name?", "string")
    .add_gather_question("email", "What is your email?", "string", /*confirm=*/true)
    .set_valid_steps({"confirm"});

greeting.add_step("confirm")
    .set_text("Confirm the information and say goodbye.")
    .set_functions(std::string("none"));  // No tools -- just confirm and end
```

This generates SWML with a complete contexts/steps structure. The platform enforces navigation rules, restricts which functions are available at each step, collects structured data with typed questions and confirmation, and tracks transitions with trigger attribution in the enriched call_log. The LLM can't skip steps, can't call restricted tools, and can't navigate to disallowed contexts -- not because it was told not to, but because the mechanisms don't exist in its world. This is PGI (Programmatically Governed Inference) in practice.

**Multi-context** agents can define separate conversation modes (e.g., "sales" and "support") with isolated function sets, and use `set_valid_contexts()` to control switching. Context transitions support 4-mode reset (consolidate x full_reset) with conversation history summarization or archival.

---

## Programmatically Governed Inference (PGI)

The contexts/steps system is the SDK's implementation of a broader architectural discipline: **Programmatically Governed Inference**. PGI starts from a single design rule: *do not tell the AI anything it does not need to know.*

Current AI models are extraordinarily good at language -- understanding loosely phrased human input, mapping intent onto structured actions, and rendering system decisions back into natural speech. They are also inconsistent, non-deterministic, and prone to confident error. These are not bugs that will be fixed in the next model generation. They are properties of probabilistic inference itself. The industry's dominant response -- prompt harder and hope ("prompt and pray") -- treats the model as the brain of the system. PGI rejects this entirely. The model is not the brain. It is a controlled participant inside a deterministic system that was always in charge.

### The Four Layers

PGI is enforced through four layers of constraint, each operating independently. Only the first depends on the model's cooperation. The remaining three are mechanical.

**Layer 1: Semantic Constraints** -- The model receives a prompt describing its role and instructions for how to behave. This is the weakest layer; it depends on probabilistic compliance. PGI treats it as guidance, not enforcement. The remaining layers are the law.

**Layer 2: Schema Constraints** -- At each step, the model sees only the tools registered for that step. Tools belonging to other steps do not exist in its function schema. The model cannot call them, reference them, or reason about them. This is the difference between telling someone not to open a door and removing the door from the building.

**Layer 3: Transition Constraints** -- Each step defines which steps it can transition to. The platform validates every transition against this whitelist. The model cannot skip phases, loop back to completed steps, or jump to unreachable states. The conversational flow is governed by the same deterministic logic as any well-designed state machine.

**Layer 4: Execution Authority** -- When the model calls a tool, it is making a request, not issuing a command. The tool handler accesses authoritative state, applies business logic, and returns both a response for the model to speak and a set of actions for the platform to execute. The model does not update state. The model does not decide what happens next. The platform does.

### PGI in Practice: Blackjack

```cpp
auto& ctx = agent.define_contexts().add_context("game");

ctx.add_step("betting")
    .set_functions({"place_bet"})
    .set_valid_steps({"playing"});

ctx.add_step("playing")
    .set_functions(std::vector<std::string>{"hit", "stand", "double_down"})
    .set_valid_steps({"hand_complete"});

ctx.add_step("you_lost")
    .set_functions(std::vector<std::string>{})
    .set_valid_steps({});
```

During the betting step, the model can only call `place_bet`. It cannot deal cards, draw cards, or resolve hands because those functions are not in its schema. When the tool handler transitions to the playing step, `place_bet` disappears and `hit`, `stand`, `double_down` appear. The model's capabilities change not because it was told to behave differently, but because the available operations were mechanically replaced.

The `you_lost` step has zero functions and zero valid transitions. The game is over. A user can beg, negotiate, or attempt social engineering. None of it works, because the mechanism for continuing does not exist. There is nothing for the model to comply with or resist. The interaction is structurally complete.

The tool handler demonstrates execution authority -- the model has no idea a step change is about to happen:

<!-- snippet: no-compile illustrative handler calls the example's own calculate_hand / format_card helpers -->
```cpp
signalwire::swaig::ToolHandler handle_hit = [](const json& args, const json& raw_data) {
    json game = raw_data.at("global_data").at("game_state");
    json card = game["deck"].back();
    game["deck"].erase(game["deck"].end() - 1);
    game["player_hand"].push_back(card);
    int score = calculate_hand(game["player_hand"]);

    auto result = signalwire::swaig::FunctionResult(
        "You drew " + format_card(card) + ". Your total is " + std::to_string(score) + ".");
    result.update_global_data({{"game_state", game}});

    if (score > 21) {
        result.swml_change_step("you_lost");
    }

    return result;
};
```

The model speaks the result. The platform changes the step. The model's world changes without its participation.

### Data Isolation

PGI extends to how data flows through the system. The model operates on a projection of reality, not the full truth. Authoritative state lives in structured data (`global_data`) that the model sees only in curated subsets. In a blackjack game, the model knows the player's chip count and visible cards. It does not know the deck composition, the dealer's hidden card, or the internal scoring calculations. In an ordering system, the model knows which items have been added. It does not know the internal pricing logic, tax calculations, or inventory state.

The model cannot hallucinate a price it has never seen. It cannot promise availability it has no knowledge of. It can only report what the system tells it to report.

### Why PGI, Not Guardrails

PGI produces a property that makes it fundamentally different from guardrails, output filtering, or any other containment strategy: **the model does not know it is being governed.** It does not know that other tools exist elsewhere in the system. It does not know that a state machine is managing the interaction. It sees its current world -- a prompt, a set of functions, a conversation history -- and operates within it. There is nothing to reason around, nothing to game, nothing to circumvent.

The strongest test of any PGI system: replace the model with a rigid scripted menu ("press 1 for tacos, press 2 for drinks") and the system would still produce correct outcomes. The tool handlers would still validate input, enforce business rules, and manage state. The experience would be worse, but every order would be accurate and every transition would follow the rules. The model makes the interaction natural. The software makes it correct. In a PGI system, those are independent properties.

The SDK's contexts/steps/function restrictions are the primitives that make PGI mechanical rather than aspirational. The developer defines steps, scopes tools to steps, declares transitions, and writes tool handlers that return structured results with platform actions. The platform enforces all of it. The developer brings domain expertise. The SDK provides the governance infrastructure.

---

## Deployment: One `run()` Call

```cpp
agent.run();
```

That single call auto-detects the environment and does the right thing:

| Environment | Detection | What Happens |
|-------------|-----------|--------------|
| **Standalone** | Default | Starts uvicorn HTTP server with FastAPI |
| **AWS Lambda** | Lambda context object | Returns Lambda-formatted response |
| **Google Cloud Functions** | GCF environment markers | Returns Flask-compatible response |
| **Azure Functions** | Azure context object | Returns Azure HttpResponse |
| **CGI** | CGI environment variables | Reads stdin, writes stdout |

Each mode handles authentication differently (HTTP Basic Auth, API Gateway authorizers, function-level auth), constructs webhook URLs using the correct public endpoint (Lambda function URL, GCF URL, Azure app URL), and formats request/response bodies per platform. You write one agent, deploy it anywhere.

For standalone mode, the SDK provides:
- Kubernetes health (`/health`) and readiness (`/ready`) probes
- SSL/TLS support via `SWML_SSL_ENABLED`, `SWML_SSL_CERT`, `SWML_SSL_KEY`
- CORS configuration
- Debug endpoint (`/debug`) for inspection

---

## Multi-Agent Hosting

<!-- snippet: no-compile registers the reader's own SalesAgent/SupportAgent/TriageAgent subclasses -->
```cpp
#include <signalwire/server/agent_server.hpp>

signalwire::server::AgentServer server("0.0.0.0", 3000);
server.register_agent(std::make_shared<SalesAgent>(), "/sales");
server.register_agent(std::make_shared<SupportAgent>(), "/support");
server.register_agent(std::make_shared<TriageAgent>(), "/triage");
server.run();
```

One process, multiple agents, route-based dispatch. Each agent gets its own SWML endpoint and SWAIG callback routing. SIP routing can map usernames to specific agents.

---

## Dynamic Configuration and Multi-Tenancy

<!-- snippet: no-compile illustrative callback calls the reader's own load_tenant_config helper -->
```cpp
agent.set_dynamic_config_callback(
    [](const std::map<std::string, std::string>& query,
       const json& body, const std::map<std::string, std::string>& headers,
       signalwire::agent::AgentBase& copy) {
        auto it = headers.find("X-Tenant-ID");
        std::string tenant = (it != headers.end()) ? it->second : "default";
        json config = load_tenant_config(tenant);
        copy.prompt_add_section("Company", config.at("company_info"));
        copy.set_global_data({{"tenant_id", tenant}, {"tier", config.at("tier")}});
        if (config.at("tier") == "premium") {
            copy.add_skill("advanced_search");
        }
    });
```

Each inbound request creates an **ephemeral copy** of the agent. The callback customizes it per-request -- different prompts, skills, global data, languages, tools. The original agent is unchanged. This enables multi-tenancy from a single deployment: one agent instance serves hundreds of tenants with tailored behavior.

---

## Search System

The SDK includes a complete hybrid search engine for local knowledge bases:

**Building indexes:**
```bash
sw-search ./docs --output knowledge.swsearch
sw-search ./docs ./examples --file-types md,txt,py --chunking-strategy sentence
sw-search validate ./knowledge.swsearch
sw-search search ./knowledge.swsearch "how do I configure SSL?"
```

**In agents:**
```cpp
agent.add_skill("native_vector_search", {
    {"index_path", "knowledge.swsearch"},
    {"tool_name", "search_docs"},
    {"description", "Search product documentation"}
});
```

The search system supports:
- **Document processing:** PDF, DOCX, Excel, PowerPoint, HTML, Markdown, plain text
- **Chunking strategies:** sentence, sliding window, paragraph, page, semantic, topic, QA-optimized, markdown-aware, JSON
- **Embedding models:** mini (384d, fast), base (768d), large
- **Hybrid search:** Vector similarity + keyword matching + filename search + metadata search
- **Backends:** SQLite (`.swsearch` files for local/serverless) or PostgreSQL (pgvector for production)
- **Installation tiers:** `search-queryonly` (~400MB, query only), `search` (~500MB, basic), `search-full` (~600MB, document processing), `search-all` (~700MB, everything)

The `.swsearch` format is a self-contained SQLite database with embeddings, chunks, and metadata -- deploy it alongside your agent to Lambda or any serverless platform.

---

## Prefab Agents

Production-ready patterns for common use cases:

```cpp
#include <signalwire/prefabs/prefabs.hpp>

// Collect structured data
signalwire::prefabs::InfoGathererAgent gatherer("info_gatherer", "/");
gatherer.set_questions({
    {{"key", "name"}, {"question", "What is your name?"}},
    {{"key", "issue"}, {"question", "Describe your issue"}, {"confirm", true}}
});

// Route calls to departments
signalwire::prefabs::ReceptionistAgent receptionist("receptionist", "/");
receptionist.set_departments({
    {"sales", {{"number", "+15551234567"}, {"description", "Product inquiries"}}},
    {"support", {{"number", "+15559876543"}, {"description", "Technical help"}}}
});
```

Five prefabs: **InfoGatherer**, **Survey**, **Receptionist**, **FAQ**, **Concierge**. Each generates complete SWML with appropriate prompts, tools, and workflows. You instantiate, customize, deploy.

---

## AI Configuration

Everything the platform supports, the SDK exposes as methods:

```cpp
// LLM tuning
agent.set_prompt_llm_params({{"temperature", 0.3}, {"top_p", 0.9}, {"barge_confidence", 0.7}});

// Multi-language
signalwire::agent::LanguageConfig spanish;
spanish.name = "Spanish";
spanish.code = "es";
spanish.voice = "google.es-ES-Neural2-A";
spanish.speech_fillers = {"Un momento..."};
spanish.function_fillers = {"Buscando..."};
agent.add_language(spanish);

// Speech recognition
agent.add_hints({"SignalWire", "SWML", "SWAIG"});
agent.add_pronunciation("SignalWire", "Signal Wire");

// Vision, thinking, inner dialog
agent.set_params({{"enable_vision", true}, {"vision_model", "gpt-4o"}});
agent.set_params({{"enable_thinking", true}, {"thinking_model", "o4-mini"}});

// Interruption control
agent.set_params({
    {"barge_match_string", "^(stop|cancel|nevermind)$"},
    {"barge_min_words", 2},
    {"barge_confidence", 0.8}
});

// Native functions with custom fillers
agent.set_native_functions({"check_time", "wait_for_user"});
agent.add_internal_filler("check_time", "en", {"Let me check the time..."});

// Call flow verbs
agent.add_pre_answer_verb("play", {{"url", "ringback.wav"}});
agent.add_post_ai_verb("hangup", json::object());
```

Call recording is driven from a tool: return `swaig::FunctionResult().record_call(...)` /
`.stop_record_call(...)` from a handler to start and stop recording mid-call.

Each of these would require understanding and manually constructing the correct SWML JSON structure. The SDK provides named methods with proper defaults.

---

## swaig-test CLI

Test without deploying:

```bash
# List available tools
swaig-test my_agent.py --list-tools

# Execute a specific tool
swaig-test my_agent.py --exec get_weather --city "San Francisco"

# Dump generated SWML for inspection
swaig-test my_agent.py --dump-swml

# Test with serverless environment simulation
swaig-test my_agent.py --simulate-serverless lambda --dump-swml

# Multi-agent: select by route or class
swaig-test multi_agent.py --route /support --list-tools
swaig-test multi_agent.py --agent-class SalesAgent --exec check_inventory
```

---

## Authentication

The SDK handles auth automatically:

- **Auto-generated credentials:** If no env vars set, generates `user_XXXX` / random password and prints to console
- **Environment variables:** `SWML_BASIC_AUTH_USER` / `SWML_BASIC_AUTH_PASSWORD`
- **Embedded in URLs:** Webhook URLs include `user:pass@host` automatically
- **Per-function tokens:** Secure functions get `__token=...` query params with expiration
- **Platform-specific:** Different auth handling for Lambda, CGI, GCF, Azure (each platform has its own auth mechanism)

---

## What You'd Have to Build Without the SDK

| Capability | Without SDK | With SDK |
|-----------|-------------|----------|
| SWML document | Hand-craft JSON | Auto-generated from C++ |
| Webhook server | Build and deploy separately | Built into the agent process |
| URL routing | Manual FastAPI/Flask setup | Automatic route registration |
| Auth tokens | Manual JWT/token system | Auto-generated per call/function |
| Proxy detection | Parse headers yourself | Automatic (ngrok, LB, CDN) |
| Tool schemas | Write JSON Schema by hand | `define_tool()` with a `swaig::ToolHandler` |
| Serverless deploy | Platform-specific handler code | `agent.run()` auto-detects |
| Multi-language | Manually construct language arrays | `add_language()` one-liner |
| State machine | Manually build contexts JSON | Fluent `define_contexts()` API |
| Structured data collection | Build gather configs by hand | `add_gather_question()` chain |
| Search/RAG | Build entire pipeline | `add_skill("native_vector_search")` |
| Multi-agent | Separate deployments + router | `AgentServer` with route registration |
| Dynamic config | Custom middleware | `set_dynamic_config_callback()` |
| Post-call analytics | Parse raw webhook payload | `on_summary()` callback |
| Health checks | Manual endpoints | Built-in `/health` and `/ready` |
| Call recording | Manual SWML verb insertion | `FunctionResult::record_call()` from a tool |
| SSL/TLS | Manual cert configuration | Env var driven |

The SDK turns what would be a multi-file infrastructure project into a single C++ class. The SWML is correct by construction. The webhooks route themselves. The auth is automatic. The deployment is universal. The developer focuses on what the agent should *do*, not how to wire it together.
