# PORT_SIGNATURE_OMISSIONS.md — documented signature divergences for the C++ port

This file is checked by `porting-sdk/scripts/diff_port_signatures.py` on
every PR. Every cross-language signature drift between this port and the
Python reference for symbols that exist on both sides MUST appear below
with a rationale, or the signature audit fails. See
`porting-sdk/CHECKLIST.md` for the audit process.

**Line format** — one `<fully.qualified.symbol>: <reason>` per line. Blank
lines and Markdown-style headers/paragraphs are ignored by the parser.

The companion `PORT_OMISSIONS.md` covers symbols missing on the C++ side
entirely; `PORT_ADDITIONS.md` covers C++-only extras. This file covers
shared symbols whose parameter list, parameter types, or return type
diverge from Python's.

## Rationale vocabulary

- `cpp_unified_action`: the Python relay API ships per-verb Action
  subclasses (PlayAction, RecordAction, CollectAction, ...) with verb-
  specific helper methods. C++ collapses these into a single generalized
  `signalwire::relay::Action` class plumbing the verb name through
  `method_prefix` and routing sub-commands generically. Python's typed
  return is structurally a subtype of the C++ unified Action — same
  callable contract, lower static guarantee. Tracked for audit clarity;
  not load-bearing for cross-language code.
- `cpp_constructor_default_only`: C++ ships an explicit default-only or
  config-struct constructor where Python's `__init__` enumerates each
  field as a keyword argument. Construction is reached either by
  calling the no-arg constructor and using setters, or by passing a
  pre-built config struct (`RelayClient::Config`,
  `AgentBase::Builder`). The full set of Python `__init__` keywords is
  reachable through the C++ setters / config-struct fields — same
  callable contract, different idiomatic shape.
- `cpp_typed_overload_subset`: C++ exposes a smaller-arity overload of
  the method where Python merges all variants into one signature with
  default-valued kwargs. The remaining options are reached either via
  named setters on the returned Action / object, or via additional
  overloads that aren't surfaced as separate Python symbols (e.g.
  `Call::play(media)` returns a `PlayAction` whose
  `volume()/loops()/direction()` setters cover Python's positional
  defaults). C++ also lacks Python-style keyword-only parameters.
- `cpp_idiom_kwargs_to_json`: where Python uses `**kwargs` and a typed
  dict at runtime, C++ accepts a `nlohmann::json` object as the kwargs
  payload. The signature audit's projection logic re-types the C++
  param to `dict<string,any>` so the structural diff matches; that
  rule covers the kwargs idiom across every method that carries one.
- `cpp_callable_typedef`: C++ uses `using XxxCallback = std::function<...>`
  typedefs for callback parameters; Python uses bare
  `typing.Callable` annotations. The two describe the same callable
  contract — the C++ typedef just adds parameter-type documentation.
  The signature audit projects the typedef to either `class:Callable`
  or the matching `callable<list<...>,ret>` per Python's annotation,
  which covers most callback signatures; the remaining entries below
  are typedefs whose Python counterpart is a `class:Callable`-typed
  return (handler-on-handler chaining) that doesn't structurally line
  up with C++'s `void` setter idiom.
- `cpp_typed_overload_split`: C++ exposes a typed overload where Python
  passes the variant as a single parameter (e.g.
  `Call::join_conference(name)` vs `Call.join_conference(**kwargs)`).
  Same callable contract; C++ just static-types the call site.
- `cpp_idiom_optional_int_timeout`: C++ uses `int` (with `0` meaning
  "no timeout") where Python uses `Optional[float]`. Same semantics;
  C++ avoids `std::optional<int>` for the canonical no-timeout sentinel.
- `cpp_action_collapsed_return`: paired with `cpp_unified_action`. C++
  methods on Call return the unified `Action` while Python returns
  per-verb subclasses (`PlayAction`, `RecordAction`, ...). The Action
  carries the same wire-level operations (stop, pause, resume, volume,
  ...); per-verb specialization lives in `method_prefix` /
  `event_type_filter` rather than in distinct C++ types.
- `cpp_idiom_dict_return`: C++ returns the underlying JSON dict for
  fire-and-forget verbs (answer/connect/transfer/...) where Python
  wraps the same call in a typed Action. The wire-level behavior is
  identical; C++ skips the Action wrapper for verbs that don't carry
  per-call sub-commands.
- `cpp_pom_runtime_shape`: Python's PromptObjectModel exposes a typed
  `pom.PromptObjectModel` instance; C++ keeps the runtime shape as a
  plain `std::vector<json>` so the same data is serializable without a
  separate type. Same on-wire contract.
- `cpp_overload_for_optional_kw`: Python merges
  `get_basic_auth_credentials(include_source: bool=False)` into a
  single signature returning a Union of two/three-tuple. C++ ships two
  overloads — the bare `get_basic_auth_credentials()` returning
  `(user, pass)` and `get_basic_auth_credentials_with_source()`
  returning `(user, pass, source)`. The 3-tuple variant is documented
  as a port addition in PORT_ADDITIONS.md.
- `cpp_typed_skill_pipeline`: Python's `SkillBase` exposes
  `register_tools()` returning `None` (callbacks register tools as a
  side-effect via `self.define_tool(...)`) and `get_prompt_sections()`
  returning `list[dict]`. C++ ships these as pure functions returning
  the typed payload directly (`vector<ToolDefinition>`,
  `vector<SkillPromptSection>`) so skill authors don't need a back-
  reference to the agent. Same registration outcome.
- `cpp_explicit_load_status`: Python's `SkillManager.load_skill` returns
  `(success: bool, error: str)`; C++ returns just `bool` because the
  error path goes through structured logging instead of an inline
  string. Same load-vs-fail contract.
- `cpp_wait_returns_bool`: Python's `Message.wait` /
  `Call.wait_for_ended` return the terminal `RelayEvent`; C++ returns
  `bool` indicating whether the timeout was reached without producing
  a terminal event. Callers who need the event reach for it via the
  Call/Message accessors after `wait` returns.
- `cpp_handler_register_void`: Python's `RelayClient.on_call` /
  `on_message` return the registered handler so the registration site
  can chain decoration. C++ returns `void` and exposes the registered
  handler via the corresponding accessor when needed. Wire-level the
  same — purely a return-type difference for fluent style.
- `cpp_connect_returns_bool`: Python's `RelayClient.connect` returns
  `None`; C++ returns `bool` so callers can branch on connection
  success without a try/catch. Same connection-establishment contract.
- `cpp_pattern_string`: Python's `DataMap.expression(pattern)` accepts
  `Union[Pattern, str]`; C++ accepts `string` only and compiles
  internally. Same regex contract.
- `cpp_postal_code_string`: Python's `FunctionResult.pay(postal_code:
  Union[bool, str])` accepts a sentinel boolean to mean "ask for
  postal code at runtime"; C++ accepts only `string` (the empty
  string acts as the sentinel). Same wire payload.
- `cpp_debug_level_bool`: Python's
  `AIConfigMixin.enable_debug_events(level: int)` accepts a verbosity
  level; C++ accepts a bool (on/off). The level-of-detail modes are
  not yet ported; the boolean variant covers the common
  enable-or-disable case.
- `cpp_load_skill_signature`: Python's
  `SkillManager.load_skill(skill_name, ..., params)` accepts a
  `params: dict` runtime configuration; C++ accepts the parent
  agent reference instead, with skill-specific options reached via
  `SkillBase` setter methods. Same load-time configuration contract.
- `cpp_questions_string`: Python's
  `InfoGathererAgent.__init__(questions: list[dict])` accepts a list
  of typed question dicts; C++ accepts a `string` JSON spec for the
  same data. Construction-time only.
- `cpp_dial_int_timeout`: paired with `cpp_idiom_optional_int_timeout`
  — `RelayClient.dial(dial_timeout: int)` uses `0` for "no
  timeout"; Python uses `Optional[float]`.
- `cpp_register_skill_factory`: Python's
  `SkillRegistry.register_skill(skill_class)` accepts a class object
  (Python's metaclass machinery introspects it for name/factory);
  C++ takes the explicit (name, factory) pair since C++ has no
  classmethod-style introspection. Same registration outcome.
- `cpp_list_skills_names`: Python's `SkillRegistry.list_skills()`
  returns `list[dict[str, str]]` with rich metadata per skill; C++
  returns `list[string]` of just the skill names. Skill metadata is
  reached via `SkillRegistry::get_skill(name)`. Same skill-discovery
  contract.
- `cpp_typed_tool_payload`: Python's `ToolRegistry.get_function` /
  `get_all_functions` return either the typed `SWAIGFunction` or a
  raw `dict` (Union); C++ returns the typed `ToolDefinition`
  directly. Same lookup outcome; C++ avoids the dynamic-typed
  fallback because all C++ tools are registered through the typed
  `define_tool` path.
- `cpp_typed_step_positional`: Python's `Context.add_step(name, *,
  task=..., bullets=..., criteria=..., functions=...,
  valid_steps=...)` uses keyword-only params; C++ collapses them to
  a `Step`-builder pattern with positional fields. Same step-
  building contract.
- `cpp_gather_question_kwargs`: Python's
  `GatherInfo.add_question(name, prompt, **kwargs)` accepts arbitrary
  metadata via kwargs; C++ ships a typed `GatherQuestion` argument
  list. Same question-registration contract.
- `cpp_get_prompt_string_only`: Python's `PromptMixin.get_prompt()`
  / `PromptManager.get_prompt()` returns `Optional[Union[list[dict],
  str]]` — either the full sectioned-prompt structure or the rendered
  string. C++ returns just the rendered string; the structured
  variant is reached via `get_raw_prompt`.
- `cpp_define_contexts_typed_return`: Python's
  `PromptMixin.define_contexts()` returns a `Union[AgentBase,
  ContextBuilder]`; C++ returns `ContextBuilder` directly (the
  builder pattern is the canonical entry point). Same contexts-
  configuration contract.
- `cpp_skill_define_tool_signature`: Python's
  `SkillBase.define_tool(**kwargs) -> None` is a thin wrapper that
  forwards to the agent; C++ ships the same as a typed builder
  returning a `ToolDefinition` so skill authors can chain `.define_
  tool(...)` into their `register_tools()` return list. Same tool-
  registration contract.
- `cpp_post_no_params`: Python's `HttpClient.post` accepts an
  optional `params` query-string dict; C++ omits this — POST URLs
  with query params aren't used by the SignalWire REST API surface
  C++ targets.
- `cpp_rest_error_field_layout`: C++ `SignalWireRestError.__init__`
  takes `(status, message, body)`; Python takes
  `(status_code, body, url, method)`. The two carry the same
  diagnostic content under different field names.
- `cpp_typed_setter_no_extra_dict`: Python's
  `PhoneNumbersResource.set_*` helpers accept an `extra: dict`
  catch-all for fields the typed setters don't enumerate; C++
  emits the typed setters only and reaches the dynamic fields via
  `update(resource_id, params)` for the rare full-control case.

## Documented signature divergences

### __init__ default-only / config-struct construction

signalwire.agent_server.AgentServer.__init__: cpp_constructor_default_only
signalwire.core.agent_base.AgentBase.__init__: cpp_constructor_default_only
signalwire.core.contexts.Context.__init__: cpp_constructor_default_only
signalwire.core.contexts.ContextBuilder.__init__: cpp_constructor_default_only
signalwire.core.contexts.Step.__init__: cpp_constructor_default_only
signalwire.core.security.session_manager.SessionManager.__init__: cpp_constructor_default_only
signalwire.core.skill_base.SkillBase.__init__: cpp_constructor_default_only
signalwire.core.skill_manager.SkillManager.__init__: cpp_constructor_default_only
signalwire.core.swml_service.SWMLService.__init__: cpp_constructor_default_only
signalwire.prefabs.concierge.ConciergeAgent.__init__: cpp_constructor_default_only
signalwire.prefabs.faq_bot.FAQBotAgent.__init__: cpp_constructor_default_only
signalwire.prefabs.receptionist.ReceptionistAgent.__init__: cpp_constructor_default_only
signalwire.prefabs.survey.SurveyAgent.__init__: cpp_constructor_default_only
signalwire.relay.call.Call.__init__: cpp_constructor_default_only
signalwire.relay.client.RelayClient.__init__: cpp_constructor_default_only
signalwire.relay.message.Message.__init__: cpp_constructor_default_only
signalwire.rest._base.SignalWireRestError.__init__: cpp_rest_error_field_layout
signalwire.prefabs.info_gatherer.InfoGathererAgent.__init__: cpp_questions_string

### Unified Action — Call methods

signalwire.relay.call.Call.ai: cpp_unified_action
signalwire.relay.call.Call.answer: cpp_unified_action
signalwire.relay.call.Call.collect: cpp_unified_action
signalwire.relay.call.Call.connect: cpp_unified_action
signalwire.relay.call.Call.detect: cpp_unified_action
signalwire.relay.call.Call.disconnect: cpp_unified_action
signalwire.relay.call.Call.hangup: cpp_unified_action
signalwire.relay.call.Call.hold: cpp_unified_action
signalwire.relay.call.Call.join_conference: cpp_unified_action
signalwire.relay.call.Call.join_room: cpp_unified_action
signalwire.relay.call.Call.live_transcribe: cpp_unified_action
signalwire.relay.call.Call.live_translate: cpp_unified_action
signalwire.relay.call.Call.pay: cpp_unified_action
signalwire.relay.call.Call.play: cpp_unified_action
signalwire.relay.call.Call.play_and_collect: cpp_unified_action
signalwire.relay.call.Call.receive_fax: cpp_unified_action
signalwire.relay.call.Call.record: cpp_unified_action
signalwire.relay.call.Call.send_digits: cpp_unified_action
signalwire.relay.call.Call.send_fax: cpp_unified_action
signalwire.relay.call.Call.stream: cpp_unified_action
signalwire.relay.call.Call.tap: cpp_unified_action
signalwire.relay.call.Call.transcribe: cpp_unified_action
signalwire.relay.call.Call.transfer: cpp_unified_action
signalwire.relay.call.Call.unhold: cpp_unified_action

### Wait / connect / handler-register return-type idiom

signalwire.relay.call.Call.wait_for_ended: cpp_wait_returns_bool
signalwire.relay.message.Message.wait: cpp_wait_returns_bool
signalwire.relay.client.RelayClient.connect: cpp_connect_returns_bool
signalwire.relay.client.RelayClient.on_call: cpp_handler_register_void
signalwire.relay.client.RelayClient.on_message: cpp_handler_register_void
signalwire.relay.client.RelayClient.dial: cpp_dial_int_timeout

### Typed-overload subset (C++ ships fewer kwargs)

signalwire.agent_server.AgentServer.run: cpp_typed_overload_subset
signalwire.core.agent.prompt.manager.PromptManager.define_contexts: cpp_define_contexts_typed_return
signalwire.core.agent.prompt.manager.PromptManager.prompt_add_section: cpp_typed_overload_subset
signalwire.core.agent.prompt.manager.PromptManager.prompt_add_to_section: cpp_typed_overload_subset
signalwire.core.agent.tools.registry.ToolRegistry.define_tool: cpp_typed_overload_subset
signalwire.core.agent_base.AgentBase.add_answer_verb: cpp_typed_overload_split
signalwire.core.agent_base.AgentBase.enable_sip_routing: cpp_typed_overload_subset
signalwire.core.agent_base.AgentBase.on_summary: cpp_typed_overload_subset
signalwire.core.function_result.FunctionResult.join_conference: cpp_typed_overload_subset
signalwire.core.mixins.ai_config_mixin.AIConfigMixin.add_function_include: cpp_typed_overload_subset
signalwire.core.mixins.ai_config_mixin.AIConfigMixin.add_internal_filler: cpp_typed_overload_subset
signalwire.core.mixins.ai_config_mixin.AIConfigMixin.add_language: cpp_typed_overload_subset
signalwire.core.mixins.ai_config_mixin.AIConfigMixin.add_pattern_hint: cpp_typed_overload_subset
signalwire.core.mixins.auth_mixin.AuthMixin.get_basic_auth_credentials: cpp_overload_for_optional_kw
signalwire.core.mixins.prompt_mixin.PromptMixin.define_contexts: cpp_define_contexts_typed_return
signalwire.core.mixins.prompt_mixin.PromptMixin.prompt_add_section: cpp_typed_overload_subset
signalwire.core.mixins.prompt_mixin.PromptMixin.prompt_add_to_section: cpp_typed_overload_subset
signalwire.core.mixins.tool_mixin.ToolMixin.define_tool: cpp_typed_overload_subset
signalwire.core.mixins.web_mixin.WebMixin.on_swml_request: cpp_typed_overload_subset
signalwire.core.mixins.web_mixin.WebMixin.run: cpp_typed_overload_subset
signalwire.core.mixins.web_mixin.WebMixin.serve: cpp_typed_overload_subset
signalwire.core.skill_base.SkillBase.define_tool: cpp_skill_define_tool_signature
signalwire.core.skill_base.SkillBase.setup: cpp_typed_overload_subset
signalwire.core.swml_service.SWMLService.add_verb: cpp_typed_overload_split
signalwire.core.swml_service.SWMLService.get_basic_auth_credentials: cpp_overload_for_optional_kw
signalwire.core.swml_service.SWMLService.serve: cpp_typed_overload_subset
signalwire.relay.client.RelayClient.send_message: cpp_typed_overload_subset
signalwire.rest._base.HttpClient.post: cpp_post_no_params
signalwire.rest.namespaces.phone_numbers.PhoneNumbersResource.set_ai_agent: cpp_typed_setter_no_extra_dict
signalwire.rest.namespaces.phone_numbers.PhoneNumbersResource.set_call_flow: cpp_typed_setter_no_extra_dict
signalwire.rest.namespaces.phone_numbers.PhoneNumbersResource.set_cxml_application: cpp_typed_setter_no_extra_dict
signalwire.rest.namespaces.phone_numbers.PhoneNumbersResource.set_cxml_webhook: cpp_typed_setter_no_extra_dict
signalwire.rest.namespaces.phone_numbers.PhoneNumbersResource.set_relay_application: cpp_typed_setter_no_extra_dict
signalwire.rest.namespaces.phone_numbers.PhoneNumbersResource.set_relay_topic: cpp_typed_setter_no_extra_dict
signalwire.rest.namespaces.phone_numbers.PhoneNumbersResource.set_swml_webhook: cpp_typed_setter_no_extra_dict
signalwire.skills.registry.SkillRegistry.register_skill: cpp_register_skill_factory

### Type / kind divergences

signalwire.core.agent.prompt.manager.PromptManager.get_prompt: cpp_get_prompt_string_only
signalwire.core.agent.tools.registry.ToolRegistry.get_all_functions: cpp_typed_tool_payload
signalwire.core.agent.tools.registry.ToolRegistry.get_function: cpp_typed_tool_payload
signalwire.core.agent_base.AgentBase.on_debug_event: cpp_callable_typedef
signalwire.core.agent_base.AgentBase.pom: cpp_pom_runtime_shape
signalwire.core.contexts.Context.add_step: cpp_typed_step_positional
signalwire.core.contexts.GatherInfo.add_question: cpp_gather_question_kwargs
signalwire.core.data_map.DataMap.expression: cpp_pattern_string
signalwire.core.function_result.FunctionResult.pay: cpp_postal_code_string
signalwire.core.mixins.ai_config_mixin.AIConfigMixin.enable_debug_events: cpp_debug_level_bool
signalwire.core.mixins.ai_config_mixin.AIConfigMixin.set_languages: cpp_typed_overload_subset
signalwire.core.mixins.ai_config_mixin.AIConfigMixin.set_pronunciations: cpp_typed_overload_subset
signalwire.core.mixins.prompt_mixin.PromptMixin.get_prompt: cpp_get_prompt_string_only
signalwire.core.skill_base.SkillBase.get_prompt_sections: cpp_typed_skill_pipeline
signalwire.core.skill_base.SkillBase.register_tools: cpp_typed_skill_pipeline
signalwire.core.skill_manager.SkillManager.load_skill: cpp_explicit_load_status
signalwire.core.skill_manager.SkillManager.unload_skill: cpp_explicit_load_status
signalwire.skills.registry.SkillRegistry.list_skills: cpp_list_skills_names
