# PORT_OMISSIONS.md -- Python-reference symbols intentionally NOT implemented in signalwire-cpp

<!-- ══════════════════════════════════════════════════════════════════════════
BEFORE YOU ADD AN ENTRY TO THIS FILE — READ THIS.

Every entry here is a place the parity checker STOPS comparing. That is a real cost:
a divergence you list is a divergence no gate will ever catch again. So entries must
be RARE, and each one must earn its place. Default to skepticism: assume the entry is
NOT needed and make the case that it is.

The order of preference, always:
  1. FIX THE PORT so it matches the reference (add the missing member; make the
     signature match).
  2. FIX THE EMISSION so idiom folds onto the reference shape — the enumerator/emitter
     canonicalizes your language's spelling onto the oracle's (builder → __init__,
     getters → attributes, Result<T,E> → the plain return, CamelCase → the reference
     name, options-object/kwargs → the expanded param list, RAII/dispose → close).
     MOST divergences are idiom and belong here, not in this file.
  3. FIX THE REFERENCE if the oracle itself is wrong or stale (a Python-only symbol
     that leaked into the contract, a param the reference added and the oracle never
     re-enumerated). Fix Python / the oracle, then re-drift — do not paper over a
     broken reference with a per-port entry.
  4. Only when 1–3 genuinely cannot apply does an entry here become justified.

An entry is JUSTIFIED ONLY IF it is irreducible after correct emission — i.e. the
divergence survives because the two languages genuinely cannot express the same thing,
not because the emitter hasn't folded the idiom yet. If emission COULD fold it, the
entry is a bug in this file; go fix the emitter.

Each entry MUST state WHY, concretely, in one of these forms:
  • ADDITION — this symbol exists in the port but not the reference. Answer: is it
    genuine port-only surface with NO reference twin (say what it is and why the
    reference has no equivalent), or is it IDIOM the emitter should have folded (then
    it does not belong here — fold it)? A convenience/alias/back-compat wrapper is NOT
    a justification.
  • OMISSION — this reference symbol has no port member. Answer: WHY can it not exist
    here — what specific language feature is absent (e.g. no async-context-manager
    protocol, no __init__ method protocol)? "impossible:" means the construct cannot
    be expressed at all; if it merely LOOKS different, that's idiom → fold it, don't
    omit it. Cite a precedent when one exists (e.g. RelayClient omits the same dunder).
  • SIGNATURE — the symbol matches by name but its parameters differ. Answer: is the
    difference a foldable idiom collapse (options-object, leading context/self,
    builder) — then EXPAND it in the signature emitter so names+count match, don't list
    it — or a genuine reference-only parameter with no cross-language analogue?

If you cannot write a crisp, specific WHY that survives the "could emission fold this?"
test, the entry is not ready. Prove it's needed before you add it.
═══════════════════════════════════════════════════════════════════════════════ -->


This file is checked by `scripts/enumerate_surface.py` +
`porting-sdk/scripts/diff_port_surface.py` on every PR. Every
Python-reference symbol missing from the C++ port MUST appear below with
a rationale, or the surface audit fails. See
`porting-sdk/CHECKLIST_TEMPLATE.md` section 13 "Symbol-level surface parity".

**Line format** -- one `<fully.qualified.symbol>: <reason>` per line. Blank
lines and Markdown-style headers/paragraphs are ignored by the parser.

## Rationale vocabulary

- `phone_binding` / `phone_binding_implicit`: covered by the phone-binding
  good path (`phone_numbers.set_*` helpers) per
  [porting-sdk/phone-binding.md](../porting-sdk/phone-binding.md). C++
  deliberately omits the legacy `assign_phone_route` path and the
  auto-materialized `create` endpoints on `SwmlWebhooksResource`,
  `CxmlWebhooksResource`, `AutoMaterializedWebhook`, and
  `CxmlApplicationsResource`.
- `python_collapsed` / `python_collapsed_mixin`: Python's multiple
  inheritance fans methods out across mixin classes; C++ collapses them
  onto the concrete class. `scripts/enumerate_surface.py` projects
  implemented methods back to their mixin module so the diff matches.
  Remaining `python_collapsed_mixin` entries are mixin methods reconciled
  via the collapsed class + enumerator projection.
- `impossible`: a genuine C++ language limit the OO-idiom cousins (Java/TS/
  PHP) also hit — decorator/dunder/framework-router/free-function FORMs with
  no static-C++ analog. See the per-line reasons.
- `approved`: human-signed-off Python-only subsystem not ported to any SDK
  (RAG/vector-search, MCP gateway; §I.1).

## Already-decided exemptions (phone-binding pivot)

These five lines carry forward the phone-binding precedent verbatim:


## Full symbol-level omissions

Every symbol below is missing in the C++ port relative to
`porting-sdk/python_surface.json`. Each line states the rationale.
Symbols above are not repeated.

signalwire.core.agent.tools.decorator.ToolDecorator: impossible: Python @tool class/instance decorator relies on the decorator protocol; C++ has no method-decorator feature — tools register via define_tool(...) / define_tools(...) directly (Java/TS/PHP omit as impossible)
signalwire.core.agent.tools.decorator.ToolDecorator.create_class_decorator: impossible: Python @tool class-decorator relies on the decorator protocol; C++ has no method-decorator feature — tools register via define_tool(...) directly (Java/TS/PHP omit as impossible)
signalwire.core.agent.tools.decorator.ToolDecorator.create_instance_decorator: impossible: Python @tool instance-decorator relies on the decorator protocol; C++ has no method-decorator feature — tools register via define_tool(...) directly (Java/TS/PHP omit as impossible)
signalwire.core.agent.tools.registry.ToolRegistry.register_class_decorated_tools: impossible: discovers @tool-decorated class methods via the Python decorator protocol; C++ has no method-decorator feature to discover, so there is nothing to register (Java/TS/PHP omit as impossible)
signalwire.core.contexts.create_simple_context: impossible: Python module-level convenience factory returning a ContextBuilder from **kwargs; C++ uses contexts::ContextBuilder directly — the free-function FORM has no static-C++ analog (Java/TS/PHP construct the builder directly likewise)
signalwire.core.data_map.create_expression_tool: impossible: Python module-level factory composing an expression tool from **kwargs + a callable pattern-map; C++'s datamap::DataMap builds the same wire shape fluently — the free-function FORM has no static-C++ analog (Java/TS/PHP compose fluently likewise)
signalwire.core.data_map.create_simple_api_tool: impossible: Python module-level factory composing an API tool from **kwargs; C++ builds the same wire shape fluently via datamap::DataMap(...).webhook(...).output(...) — the free-function FORM has no static-C++ analog (Java/TS/PHP compose fluently likewise)
signalwire.core.function_result.FunctionResult.to_dict: python_collapsed: Python to_dict() returns a Python dict; C++ to_json() returns the equivalent nlohmann::json. to_dict name is intentionally replaced by to_json.
agentbase-family.tool: impossible: Python @tool decorator method relies on the decorator protocol; C++ has no method-decorator feature — tools register via define_tool(...) directly (Java/TS/PHP omit as impossible). Re-keyed to agentbase-family by the A-fold (ALLOWLIST_DISCIPLINE §4c).
agentbase-family.get_app: impossible: returns a FastAPI/Flask ASGI/WSGI app object; C++ has no such framework — the service runs httplib directly, so there is no app object to return (Java/TS/PHP omit the framework-app FORM identically). Re-keyed to agentbase-family by the A-fold (ALLOWLIST_DISCIPLINE §4c).
signalwire.core.swml_builder.SWMLBuilder.__getattr__: impossible: Python runtime __getattr__ dynamic verb dispatch; C++ has no __getattr__/method_missing analog — SWMLBuilder expands each named verb (answer/hangup/ai/play/say) as an explicit method (Java/TS/PHP expand identically)
signalwire.core.swml_service.SWMLService.__getattr__: impossible: Python runtime __getattr__ verb dispatch; C++ swml::Service expands every schema verb as an explicit statically-typed method, so there is no single catch-all member to enumerate (Java/TS/PHP expand identically)
# signalwire.pom.pom.PromptObjectModel + Section are implemented in C++ —
# see include/signalwire/pom/pom.hpp. Their previous "not_yet_implemented"
# omissions were dropped when the standalone POM module shipped.
signalwire.relay.client.RelayClient.__aenter__: impossible: Python async-context-manager protocol dunder; C++ RelayClient uses explicit connect()/disconnect() — no __aenter__ equivalent (Java/TS/PHP omit identically)
signalwire.relay.client.RelayClient.__aexit__: impossible: Python async-context-manager protocol dunder; C++ RelayClient uses explicit connect()/disconnect() (Java/TS/PHP omit identically)
signalwire.relay.client.RelayClient.__del__: impossible: Python finalizer dunder; C++ uses a deterministic destructor (~RelayClient) rather than the Python __del__ finalizer protocol — the dunder NAME has no cross-language surface (Java/TS/PHP omit identically)
signalwire.rest._pagination.PaginatedIterator.__iter__: impossible: Python iterator-protocol dunder (__iter__); C++ has no object-protocol equivalent and the OO cousins do not implement it either.
signalwire.rest._pagination.PaginatedIterator.__next__: impossible: Python iterator-protocol dunder (__next__); C++ has no object-protocol equivalent and the OO cousins do not implement it either.
signalwire.skills.mcp_gateway.skill.MCPGatewaySkill: approved: MCP gateway subsystem is Python-only, not ported to any SDK (user ruling; §I.1). C++ agents consume MCP via agent.add_mcp_server/enable_mcp_server on AgentBase, which are implemented; the standalone MCPGatewaySkill is not ported.
signalwire.skills.mcp_gateway.skill.MCPGatewaySkill.get_global_data: approved: MCP gateway subsystem is Python-only, not ported to any SDK (user ruling; §I.1). C++ agents consume MCP via agent.add_mcp_server/enable_mcp_server on AgentBase, which are implemented; the standalone MCPGatewaySkill is not ported.
signalwire.skills.mcp_gateway.skill.MCPGatewaySkill.get_hints: approved: MCP gateway subsystem is Python-only, not ported to any SDK (user ruling; §I.1). C++ agents consume MCP via agent.add_mcp_server/enable_mcp_server on AgentBase, which are implemented; the standalone MCPGatewaySkill is not ported.
signalwire.skills.mcp_gateway.skill.MCPGatewaySkill.get_parameter_schema: approved: MCP gateway subsystem is Python-only, not ported to any SDK (user ruling; §I.1). C++ agents consume MCP via agent.add_mcp_server/enable_mcp_server on AgentBase, which are implemented; the standalone MCPGatewaySkill is not ported.
signalwire.skills.mcp_gateway.skill.MCPGatewaySkill.get_prompt_sections: approved: MCP gateway subsystem is Python-only, not ported to any SDK (user ruling; §I.1). C++ agents consume MCP via agent.add_mcp_server/enable_mcp_server on AgentBase, which are implemented; the standalone MCPGatewaySkill is not ported.
signalwire.skills.mcp_gateway.skill.MCPGatewaySkill.register_tools: approved: MCP gateway subsystem is Python-only, not ported to any SDK (user ruling; §I.1). C++ agents consume MCP via agent.add_mcp_server/enable_mcp_server on AgentBase, which are implemented; the standalone MCPGatewaySkill is not ported.
signalwire.skills.mcp_gateway.skill.MCPGatewaySkill.setup: approved: MCP gateway subsystem is Python-only, not ported to any SDK (user ruling; §I.1). C++ agents consume MCP via agent.add_mcp_server/enable_mcp_server on AgentBase, which are implemented; the standalone MCPGatewaySkill is not ported.

## Python instance attributes / properties not exposed in C++

Python's per-instance state attributes (`self.app`, `self.logger`, `self.skill_manager`, etc.) are exposed as Python @property descriptors and read via dot-access. The signature audit treats them as zero-arg getter methods. C++ does not expose these as public methods — they are private member fields owned by the class. The user-facing surface of the class is unchanged; users interact with the higher-level methods (`run`, `add_skill`, etc.) rather than reaching inside.

signalwire.agent_server.AgentServer.logger: impossible: cpp-module-level-logger — Python exposes a per-instance logger @property (class:logging_config.get_logger). C++ SDK uses a MODULE-LEVEL signalwire::logging::Logger (get_logger("agent_server")) by design — there is no per-instance logger FIELD in the C++ design — the logger is module-level state, structurally not a per-instance member (Java/TS omit the per-instance logger form identically).
agentbase-family.skill_manager: approved: cpp-private-composition — Python AgentBase.skill_manager is a @property exposing the SkillManager instance. C++ AgentBase composes SkillManager privately and surfaces the same operations as AgentBase methods (add_skill, remove_skill, list_skills, has_skill) rather than exposing the manager object. Port-gap: a skill_manager() accessor is an API decision pending sign-off; NOT language-impossible. Re-keyed to agentbase-family by the A-fold.
signalwire.core.skill_base.SkillBase.logger: impossible: cpp-module-level-logger — per-instance logger @property in Python; C++ uses the module-level logger (see AgentServer.logger). Port-gap pending API sign-off; NOT language-impossible.
signalwire.core.skill_manager.SkillManager.logger: impossible: cpp-module-level-logger — per-instance logger @property in Python; C++ uses the module-level logger (see AgentServer.logger). Port-gap pending API sign-off; NOT language-impossible.
signalwire.core.swml_service.SWMLService.security: approved: cpp-folded-onto-methods — Python SWMLService.security is a @property exposing a SecurityConfig helper object. C++ has the SecurityConfig class but SWMLService folds the auth operations directly onto methods (validate_basic_auth, get_basic_auth_credentials) rather than holding/exposing a security member. Port-gap: exposing a security() accessor is an API decision pending sign-off; NOT language-impossible.
signalwire.core.swml_service.SWMLService.verb_registry: impossible: Python SWMLService.verb_registry is a @property exposing the internal verb-name→handler map for inspection; C++ keeps the verb registry private and exposes only the user-facing verb operations (add_verb, has_function) — the internal map is not a public member.
signalwire.skills.registry.SkillRegistry.logger: impossible: cpp-module-level-logger — per-instance logger @property in Python; C++ uses the module-level logger (see AgentServer.logger). Port-gap pending API sign-off; NOT language-impossible.

## POM internal collections


# Auto-extras: missing-from-omissions entries surfaced when the surface-audit
# diff step started running after the wire-up bug was fixed.
signalwire.core.security.webhook_middleware.make_webhook_validation_dependency: impossible: framework-bound factory returning a FastAPI dependency callable; the framework WRAPPER has no C++ analog (C++ has no FastAPI). The framework-free decision CORE is reconciled — C++ ships webhook_middleware.validate as the free function signalwire::security::Validate(method,url,headers,body,signing_key)->optional<(status,headers,body)> (matched 1:1 by the DRIFT gate via the free-function rename table), and the cpp-httplib WrapWithSignatureValidation adapter (a PORT_ADDITION) is the native middleware idiom on top of it. Only the FastAPI-dependency FORM stays idiom (Java/TS/PHP ship native middleware likewise).
signalwire.skills.api_ninjas_trivia.skill.ApiNinjasTriviaSkill.get_tools: python_mixin_collapsed: C++ skills are implemented as C++ classes registered with skills::SkillRegistry; per-skill Python method names (setup/register_tools/get_hints/get_parameter_schema) are implementation internals and not exposed by name in C++. User-visible behavior reachable via agent.add_skill("<name>", params).
signalwire.skills.play_background_file.skill.PlayBackgroundFileSkill.get_tools: python_mixin_collapsed: C++ skills are implemented as C++ classes registered with skills::SkillRegistry; per-skill Python method names (setup/register_tools/get_hints/get_parameter_schema) are implementation internals and not exposed by name in C++. User-visible behavior reachable via agent.add_skill("<name>", params).
signalwire.skills.spider.skill.SpiderSkill.__init__: python_mixin_collapsed: C++ skills are implemented as C++ classes registered with skills::SkillRegistry; per-skill Python method names (setup/register_tools/get_hints/get_parameter_schema) are implementation internals and not exposed by name in C++. User-visible behavior reachable via agent.add_skill("<name>", params).
signalwire.skills.weather_api.skill.WeatherApiSkill.get_tools: python_mixin_collapsed: C++ skills are implemented as C++ classes registered with skills::SkillRegistry; per-skill Python method names (setup/register_tools/get_hints/get_parameter_schema) are implementation internals and not exposed by name in C++. User-visible behavior reachable via agent.add_skill("<name>", params).
signalwire.skills.wikipedia_search.skill.WikipediaSearchSkill.search_wiki: python_mixin_collapsed: C++ skills are implemented as C++ classes registered with skills::SkillRegistry; per-skill Python method names (setup/register_tools/get_hints/get_parameter_schema) are implementation internals and not exposed by name in C++. User-visible behavior reachable via agent.add_skill("<name>", params).
signalwire.web.web_service.WebService: python_collapsed: Python WebService hosts async HTTP endpoints for multi-agent serving; C++ uses AgentServer + httplib for the same purpose. A separate WebService class is intentionally folded into AgentServer.
signalwire.rest._request_options.resolve: impossible: Python module-level free function resolving effective options (per-request over client-default over built-in); C++ folds this into HttpClient's private request() funnel (an anonymous-namespace resolve() in http_client.cpp) — the module-free-function FORM has no static-C++ analog (Java/TS/PHP fold it into the client likewise)
signalwire.rest._request_options.status_is_retryable: impossible: Python module-level free function deciding idempotency-aware retryability; C++ folds this into HttpClient's private request() funnel (an anonymous-namespace status_is_retryable() in http_client.cpp) — the module-free-function FORM has no static-C++ analog (Java/TS/PHP fold it into the client likewise)

<!-- A-fold / B1-property re-key: reference @property composition attributes the
     B1 enrichment surfaced. C++ has no property idiom; each is reached via a getter
     method or kept private — impossible: as the OO cousins (Java/TS) omit likewise. -->
signalwire.agent_server.AgentServer.agents: approved: cpp-getter-method-form — Python AgentServer.agents is a @property returning a route->AgentBase map. C++ exposes the same collection via the get_agents() METHOD (returns a vector of (route, agent) pairs) — a getter-method of a different shape; the map-@property form is not separately exposed. Port-gap: a matching agents() accessor is an API decision pending sign-off; NOT language-impossible.
signalwire.core.skill_manager.SkillManager.loaded_skills: approved: cpp-getter-method-form — Python SkillManager.loaded_skills is a @property returning a name->SkillBase map. C++ exposes the loaded skills via list_loaded()/list_loaded_skills() (names) + get_skill(name); the map-@property form is not separately exposed. Port-gap pending API sign-off; NOT language-impossible.
signalwire.web.web_service.WebService.security: approved: cpp-private-config — Python WebService.security is a @property exposing a SecurityConfig. C++ WebService accepts SecurityConfig for signature compatibility but stores it privately (config-file loading is a documented no-op stub) and exposes no accessor. Port-gap: a security() accessor is an API decision pending sign-off; NOT language-impossible.
