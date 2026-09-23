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

(Vocabulary is listed only for tags this file actually uses. Removed 2026-08-12:
`phone_binding` / `phone_binding_implicit` and `python_collapsed` /
`python_collapsed_mixin` — both were defined here but used by ZERO entries, and the
`python_collapsed_mixin` note asserted "remaining entries" that do not exist.)

- `impossible`: a genuine C++ language limit the OO-idiom cousins (Java/TS/
  PHP) also hit — decorator/dunder/framework-router/free-function FORMs with
  no static-C++ analog. See the per-line reasons.
- `approved`: human sign-off. Two uses, both present: (a) a Python-only subsystem not
  ported to any SDK (RAG/vector-search, MCP gateway; §I.1), and (b) a composition
  attribute whose accessor is an open API decision pending sign-off — NOT
  language-impossible, and flagged as a port-gap in the line itself.
- `cpp_builtin_skill_in_tu`: the symbol IS implemented, but in a .cpp translation unit
  with no public header, so the libclang SIGNATURE enumerator (which walks `include/`
  only) sees no member. Dead for SURFACE-DIFF (the regex enumerator does see it), live
  only for the signature gate. The fix is enumerator scope, not the port.
- `cpp_serializer_to_json`: serializer-name idiom — C++ spells the reference's
  `to_dict()` as `to_json()`. Aliased by the SURFACE enumerator, so it is live only for
  the signature gate, which has no such alias.

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
signalwire.core.function_result.FunctionResult.to_dict: cpp_serializer_to_json: the C++ serializer is `json to_json() const` (include/signalwire/swaig/function_result.hpp:428) returning nlohmann::json where the reference records to_dict() -> dict. The SURFACE enumerator aliases to_json->to_dict, so this entry is DEAD for SURFACE-DIFF; it is live only for the signature gate, which has no such alias and reports to_dict as missing-port. Same serialization (byte-compared against Python to_dict() by the EMISSION gate over the shared 81-entry corpus) — serializer-name idiom.
agentbase-family.tool: impossible: Python @tool decorator method relies on the decorator protocol; C++ has no method-decorator feature — tools register via define_tool(...) directly (Java/TS/PHP omit as impossible). Re-keyed to agentbase-family by the A-fold (ALLOWLIST_DISCIPLINE §4c).
agentbase-family.get_app: impossible: returns a FastAPI/Flask ASGI/WSGI app object; C++ has no such framework — the service runs httplib directly, so there is no app object to return (Java/TS/PHP omit the framework-app FORM identically). Re-keyed to agentbase-family by the A-fold (ALLOWLIST_DISCIPLINE §4c).
signalwire.core.swml_builder.SWMLBuilder.__getattr__: impossible: Python runtime __getattr__ is a name-lookup HOOK — a dunder that fires on failed attribute lookup — and C++ has no __getattr__/method_missing analog, so the hook itself cannot be expressed. The DISPATCH the hook provides is reconciled, not the hook: C++ core::SWMLBuilder declares the same 9 explicit members as the Python class (answer/hangup/ai/play/say/add_section/build/render/reset — of which 4, ai/answer/hangup/play, are schema verbs; `say` is a play-with-`say:`-prefix helper, not a schema verb), and every OTHER schema verb is reached through the underlying swml::Service via `service()`, which declares 38 of them explicitly (see the SWMLService.__getattr__ entry). MEASURED 2026-08-12: `yaml.safe_load(mod_infrastructure/specs/swml.yaml)['methods']` is a 52-key dict, but the verb set Python's __getattr__ actually dispatches over is `SchemaUtils().get_all_verb_names()` = 39 (the SDK-bundled signalwire/schema.json); C++ bundles the SAME 39-verb schema (src/swml/schema.json). Only the dunder FORM is omitted (Java/TS/PHP omit the hook identically).
signalwire.core.swml_service.SWMLService.__getattr__: impossible: Python runtime __getattr__ is a name-lookup HOOK; C++ has no __getattr__/method_missing analog, so the catch-all member cannot exist and there is nothing to enumerate. Its dispatch is reconciled by explicit expansion plus a generic escape hatch, NOT by expanding every verb. MEASURED 2026-08-12: Python's dispatch set is `SchemaUtils().get_all_verb_names()` = 39 verbs (from the SDK-bundled signalwire/schema.json, NOT the 52-key `methods` dict in mod_infrastructure/specs/swml.yaml — the SDK bundles a narrower schema); C++ bundles the SAME 39-verb schema (src/swml/schema.json, verified by enumerating it with the reference's own SchemaUtils). swml::Service declares 38 of those 39 as explicit statically-typed methods (include/signalwire/swml/service.hpp:111-148, three reserved-word-renamed via the adapter: goto->goto_section, return->return_section, switch->switch_section). The ONE verb with no explicit method is `ai_sidecar`; it is still reachable — and schema-validated — through the generic `add_verb(verb_name, config)` overload (service.hpp:164), and its config struct is generated (include/signalwire/core/swml_verbs_generated/ai_sidecar_config.hpp). So no capability is lost; only the dunder FORM is omitted (Java/TS/PHP omit the hook identically).
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
signalwire.skills.mcp_gateway.skill.MCPGatewaySkill.register_tools: approved: MCP gateway subsystem is Python-only, not ported to any SDK (user ruling; §I.1). C++ agents consume MCP via agent.add_mcp_server/enable_mcp_server on AgentBase, which are implemented; the standalone MCPGatewaySkill is not ported.
signalwire.skills.mcp_gateway.skill.MCPGatewaySkill.setup: approved: MCP gateway subsystem is Python-only, not ported to any SDK (user ruling; §I.1). C++ agents consume MCP via agent.add_mcp_server/enable_mcp_server on AgentBase, which are implemented; the standalone MCPGatewaySkill is not ported.

## Python instance attributes / properties not exposed in C++

Python's per-instance state attributes (`self.app`, `self.skill_manager`, `self.security`, etc.) are PLAIN instance attributes assigned in `__init__` and read via dot-access. (MEASURED 2026-08-12: they are NOT `@property` descriptors — `grep -c '@property'` is 0 in both `core/agent_base.py` and `web/web_service.py`, and the single `@property` in `core/swml_service.py` is an unrelated symbol. An earlier revision of this file described all of them as "@property", which was false; corrected.) The signature audit treats them as zero-arg getter methods. C++ does not expose these as public methods — they are private member fields owned by the class, or are not held at all. The user-facing surface of the class is unchanged; users interact with the higher-level methods (`run`, `add_skill`, etc.) rather than reaching inside.

agentbase-family.skill_manager: approved: cpp-private-composition — Python AgentBase.skill_manager is a plain instance attribute (`self.skill_manager = SkillManager(self)`, core/agent_base.py:313) holding the SkillManager instance. C++ AgentBase composes SkillManager privately and surfaces the same operations as AgentBase methods (add_skill, remove_skill, list_skills, has_skill — include/signalwire/agent/agent_base.hpp:546-549) rather than exposing the manager object. Port-gap: a skill_manager() accessor is an API decision pending sign-off; NOT language-impossible. Re-keyed to agentbase-family by the A-fold.
signalwire.core.swml_service.SWMLService.security: approved: cpp-folded-onto-methods — Python SWMLService.security is a plain instance attribute (`self.security = SecurityConfig(config_file=…, service_name=name)`, core/swml_service.py:139) holding a SecurityConfig helper object. C++ has the SecurityConfig class but SWMLService folds the auth operations directly onto methods (validate_basic_auth, get_basic_auth_credentials — include/signalwire/swml/service.hpp:96,100) rather than holding/exposing a security member. Port-gap: exposing a security() accessor is an API decision pending sign-off; NOT language-impossible.
signalwire.core.swml_service.SWMLService.verb_registry: impossible: Python SWMLService.verb_registry is a plain instance attribute (`self.verb_registry = VerbHandlerRegistry()`, core/swml_service.py:192) holding the internal verb-name→handler map, readable by dot-access. C++ keeps the verb registry private and exposes only the user-facing verb operations (add_verb, has_function — include/signalwire/swml/service.hpp:154,164,272) — the internal map is not a public member.

# Auto-extras: missing-from-omissions entries surfaced when the surface-audit
# diff step started running after the wire-up bug was fixed.
signalwire.core.security.webhook_middleware.make_webhook_validation_dependency: impossible: framework-bound factory returning a FastAPI dependency callable; the framework WRAPPER has no C++ analog (C++ has no FastAPI). The framework-free decision CORE is reconciled — C++ ships webhook_middleware.validate as the free function signalwire::security::Validate(method,url,headers,body,signing_key)->optional<(status,headers,body)> (matched 1:1 by the DRIFT gate via the free-function rename table), and the cpp-httplib WrapWithSignatureValidation adapter (a PORT_ADDITION) is the native middleware idiom on top of it. Only the FastAPI-dependency FORM stays idiom (Java/TS/PHP ship native middleware likewise).
signalwire.skills.api_ninjas_trivia.skill.ApiNinjasTriviaSkill.get_tools: cpp_builtin_skill_in_tu: the C++ built-in skills are DEFINED IN THE .cpp TRANSLATION UNIT (src/skills/builtin/api_ninjas_trivia.cpp) and self-register with skills::SkillRegistry — there is no public header declaring the class, so the libclang SIGNATURE enumerator (which walks include/ only) records no member for it. The regex SURFACE enumerator DOES see the class, so this entry is dead for SURFACE-DIFF and live only for the signature gate. Not a missing capability: reachable via agent.add_skill("api_ninjas_trivia", params). Fix is enumerator scope (walk src/skills/builtin), not the port.
signalwire.skills.play_background_file.skill.PlayBackgroundFileSkill.get_tools: cpp_builtin_skill_in_tu: the C++ built-in skills are DEFINED IN THE .cpp TRANSLATION UNIT (src/skills/builtin/play_background_file.cpp) and self-register with skills::SkillRegistry — there is no public header declaring the class, so the libclang SIGNATURE enumerator (which walks include/ only) records no member for it. The regex SURFACE enumerator DOES see the class, so this entry is dead for SURFACE-DIFF and live only for the signature gate. Not a missing capability: reachable via agent.add_skill("play_background_file", params). Fix is enumerator scope (walk src/skills/builtin), not the port.
signalwire.skills.weather_api.skill.WeatherApiSkill.get_tools: cpp_builtin_skill_in_tu: the C++ built-in skills are DEFINED IN THE .cpp TRANSLATION UNIT (src/skills/builtin/weather_api.cpp) and self-register with skills::SkillRegistry — there is no public header declaring the class, so the libclang SIGNATURE enumerator (which walks include/ only) records no member for it. The regex SURFACE enumerator DOES see the class, so this entry is dead for SURFACE-DIFF and live only for the signature gate. Not a missing capability: reachable via agent.add_skill("weather_api", params). Fix is enumerator scope (walk src/skills/builtin), not the port.
signalwire.skills.wikipedia_search.skill.WikipediaSearchSkill.search_wiki: cpp_builtin_skill_in_tu: search_wiki IS implemented in C++ (src/skills/builtin/wikipedia_search.cpp:37, `std::string search_wiki(const std::string& query) const`) but the class is defined in the .cpp TRANSLATION UNIT with no public header, so the libclang SIGNATURE enumerator (which walks include/ only) records no member for it. The regex SURFACE enumerator DOES see the class, so this entry is dead for SURFACE-DIFF and live only for the signature gate. Fix is enumerator scope (walk src/skills/builtin), not the port.
signalwire.rest._request_options.resolve: impossible: Python module-level free function resolving effective options (per-request over client-default over built-in); C++ folds this into HttpClient's private request() funnel (an anonymous-namespace resolve() in http_client.cpp) — the module-free-function FORM has no static-C++ analog (Java/TS/PHP fold it into the client likewise)
signalwire.rest._request_options.status_is_retryable: impossible: Python module-level free function deciding idempotency-aware retryability; C++ folds this into HttpClient's private request() funnel (an anonymous-namespace status_is_retryable() in http_client.cpp) — the module-free-function FORM has no static-C++ analog (Java/TS/PHP fold it into the client likewise)

<!-- A-fold / B1-composition re-key: reference composition attributes the B1
     enrichment surfaced. Each is a PLAIN instance attribute in the reference (not a
     @property — measured 2026-08-12, see the section note above); C++ reaches each via
     a getter method or keeps it private. -->
signalwire.agent_server.AgentServer.agents: approved: cpp-getter-method-form — Python AgentServer.agents is a plain instance attribute (`self.agents: dict[str, AgentBase] = {}`, agent_server.py:95) holding a route->AgentBase map. C++ exposes the same collection via the get_agents() METHOD (include/signalwire/server/agent_server.hpp:77, returning a vector of (route, agent) pairs) — a getter-method of a different shape; the dict-attribute form is not separately exposed. Port-gap: a matching agents() accessor is an API decision pending sign-off; NOT language-impossible.
signalwire.core.skill_manager.SkillManager.loaded_skills: approved: cpp-getter-method-form — Python SkillManager.loaded_skills is a plain instance attribute (`self.loaded_skills: dict[str, SkillBase] = {}`, core/skill_manager.py:23) holding a name->SkillBase map. C++ exposes the loaded skills via list_loaded()/list_loaded_skills() (names — include/signalwire/skills/skill_manager.hpp:59,69) + get_skill(name); the dict-attribute form is not separately exposed. Port-gap pending API sign-off; NOT language-impossible.
signalwire.web.web_service.WebService.security: approved: cpp-no-securityconfig-member — Python WebService.security is a plain instance attribute (`self.security = SecurityConfig(config_file=config_file, service_name="web")`, web/web_service.py:107). C++ WebService does NOT hold a SecurityConfig at all: its constructor takes a `config_file` STRING that is accepted for signature compatibility and documented as a no-op (include/signalwire/web/web_service.hpp:41-44; src/web/web_service.cpp:114), and no SecurityConfig member exists among its private fields (web_service.hpp:111-122). The auth that IS wired comes from the separate `basic_auth` ctor parameter. (An earlier revision of this line claimed C++ "accepts SecurityConfig ... but stores it privately" — false; there is no such member. Corrected 2026-08-12.) Port-gap: loading SecurityConfig and exposing a security() accessor is an API decision pending sign-off; NOT language-impossible.
