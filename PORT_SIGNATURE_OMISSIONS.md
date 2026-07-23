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
  `Call.wait_for_ended` / `Call.wait_for_{answered,ringing,ending}`
  return the terminal/state `RelayEvent`; C++ returns `bool` indicating
  whether the wait reached the target before the timeout. Callers who
  need the event reach for it via the Call/Message accessors (e.g.
  `Call::state()`) after `wait` returns. The state-wait helpers share
  `wait_for_ended`'s `int timeout_ms` idiom (`<=0` waits indefinitely)
  and the same created<ringing<answered<ending<ended short-circuit as
  Python's `_wait_for_state`.
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
- `cpp_typed_builder_infer`: Python's `type_inference.infer_schema`
  reflects a callable's type hints (`inspect.signature` + `typing`
  introspection) to build the SWAIG parameter schema; C++ has no runtime
  parameter-name/type-hint reflection over an arbitrary lambda, so — like
  the .NET / Ruby ports (which infer from a delegate's parameter list) —
  the C++ port infers the schema from the port's typed `ParameterSchema`
  params-builder. Same output tuple `(parameters, required, description,
  is_typed, has_raw_data)`; only the input is idiomatic (a built
  `ParameterSchema` instead of a raw callable). `create_typed_handler_wrapper`
  likewise wraps a C++ `ToolHandler` (`std::function<FunctionResult(json,
  json)>`) to the standard `(args, raw_data)` calling convention rather than
  a bare Python callable.

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
signalwire.relay.call.Call.detect_answering_machine: cpp_unified_action
signalwire.relay.call.Call.detect_digit: cpp_unified_action
signalwire.relay.call.Call.detect_fax: cpp_unified_action
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
signalwire.relay.call.Call.play_audio: cpp_unified_action
signalwire.relay.call.Call.play_ringtone: cpp_unified_action
signalwire.relay.call.Call.play_silence: cpp_unified_action
signalwire.relay.call.Call.play_tts: cpp_unified_action
signalwire.relay.call.Call.prompt_audio: cpp_unified_action
signalwire.relay.call.Call.prompt_tts: cpp_unified_action
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

signalwire.relay.call.Call.wait_for_answered: cpp_wait_returns_bool
signalwire.relay.call.Call.wait_for_ended: cpp_wait_returns_bool
signalwire.relay.call.Call.wait_for_ending: cpp_wait_returns_bool
signalwire.relay.call.Call.wait_for_ringing: cpp_wait_returns_bool
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
signalwire.core.agent.tools.type_inference.infer_schema: cpp_typed_builder_infer: Python reflects a callable's type hints; C++ has no lambda reflection so infers the schema from the typed ParameterSchema params-builder — same output tuple (parameters, required, description, is_typed, has_raw_data), idiomatic input.
signalwire.core.agent.tools.type_inference.create_typed_handler_wrapper: cpp_typed_builder_infer: Python wraps a bare callable to the (args, raw_data) convention; C++ wraps a ToolHandler (std::function<FunctionResult(json,json)>) to the same convention — same wrap-and-forward behavior, idiomatic handler type.
signalwire.core.agent_base.AgentBase.add_answer_verb: cpp_typed_overload_split
signalwire.core.agent_base.AgentBase.enable_sip_routing: cpp_typed_overload_subset
signalwire.core.agent_base.AgentBase.on_summary: cpp_typed_overload_subset
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
signalwire.core.contexts.Context.add_step: cpp_typed_step_positional
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

## POM (signalwire.pom.pom) — C++ idiom

signalwire.pom.pom.PromptObjectModel.__init__: cpp-overload-set — C++ exposes overloaded ctors (default, copy-from-list, copy-from-PromptObjectModel) where Python has a single __init__ with default arg
signalwire.pom.pom.PromptObjectModel.add_section: cpp-overload-set — C++ exposes 4 overloads (title-only / title+body / title+bullets / full) where Python uses single positional+kwargs
signalwire.pom.pom.PromptObjectModel.add_pom_as_subsection: cpp-typed-overload — C++ takes typed Section& or std::string title parameter where Python uses Union[str, Section]
signalwire.pom.pom.PromptObjectModel.from_json: cpp-typed-overload — C++ takes const std::string& where Python's from_json takes Union[str, dict]
signalwire.pom.pom.PromptObjectModel.from_yaml: cpp-typed-overload — C++ takes const std::string& where Python's from_yaml takes Union[str, dict]
signalwire.pom.pom.Section.__init__: cpp-overload-set — C++ exposes overloaded ctors (default, builder, copy) where Python has a single __init__ with positional+kwargs
signalwire.pom.pom.Section.add_subsection: cpp-overload-set — C++ exposes 4 overloads (title-only / title+body / title+bullets / full) where Python uses single positional+kwargs

## Webhook signature validation (signalwire.core.security.*) — C++ idiom

signalwire.core.security.webhook_validator.validate_request: cpp-typed-overload — C++ ParamsOrBody is std::variant<string, vector<pair<string,vector<string>>>> covering raw-body and pre-parsed form-params; Python's Union additionally lists Mapping[str,Any] and None which collapse to the same Scheme B path. Same wire contract — different idiomatic typing.

# ---- item H/I signature idioms (relay events/actions, bedrock, core infra, mixins, prefabs) ----
signalwire.agent_server.AgentServer.agents: cpp_kwargs_json: AgentServer method takes typed C++ params / a routing callback where Python spreads keyword args; same multi-agent routing (mirrors Java AgentServer).
signalwire.agent_server.AgentServer.register_global_routing_callback: cpp_kwargs_json: AgentServer method takes typed C++ params / a routing callback where Python spreads keyword args; same multi-agent routing (mirrors Java AgentServer).
signalwire.agents.bedrock.BedrockAgent.__init__: cpp_kwargs_json: BedrockAgent setters take typed C++ params / a nlohmann::json options object where Python spreads **kwargs; the port emits the same calling.amazon_bedrock config keys (kwargs idiom, mirrors the Java BedrockAgent).
signalwire.agents.bedrock.BedrockAgent.set_inference_params: cpp_kwargs_json: BedrockAgent setters take typed C++ params / a nlohmann::json options object where Python spreads **kwargs; the port emits the same calling.amazon_bedrock config keys (kwargs idiom, mirrors the Java BedrockAgent).
signalwire.agents.bedrock.BedrockAgent.set_llm_model: cpp_kwargs_json: BedrockAgent setters take typed C++ params / a nlohmann::json options object where Python spreads **kwargs; the port emits the same calling.amazon_bedrock config keys (kwargs idiom, mirrors the Java BedrockAgent).
signalwire.agents.bedrock.BedrockAgent.set_llm_temperature: cpp_kwargs_json: BedrockAgent setters take typed C++ params / a nlohmann::json options object where Python spreads **kwargs; the port emits the same calling.amazon_bedrock config keys (kwargs idiom, mirrors the Java BedrockAgent).
signalwire.agents.bedrock.BedrockAgent.set_post_prompt_llm_params: cpp_kwargs_json: BedrockAgent setters take typed C++ params / a nlohmann::json options object where Python spreads **kwargs; the port emits the same calling.amazon_bedrock config keys (kwargs idiom, mirrors the Java BedrockAgent).
signalwire.agents.bedrock.BedrockAgent.set_prompt_llm_params: cpp_kwargs_json: BedrockAgent setters take typed C++ params / a nlohmann::json options object where Python spreads **kwargs; the port emits the same calling.amazon_bedrock config keys (kwargs idiom, mirrors the Java BedrockAgent).
signalwire.agents.bedrock.BedrockAgent.set_voice: cpp_kwargs_json: BedrockAgent setters take typed C++ params / a nlohmann::json options object where Python spreads **kwargs; the port emits the same calling.amazon_bedrock config keys (kwargs idiom, mirrors the Java BedrockAgent).
signalwire.core.agent_base.AgentBase.add_swaig_query_params: cpp_kwargs_json: AgentBase method takes typed C++ params / a nlohmann::json where Python spreads **kwargs; same behavior (the port's kwargs idiom).
signalwire.core.auth_handler.AuthHandler.verify_basic_auth: cpp_idiom_carrier: AuthHandler verify_* take typed C++ credential carriers (BasicCredentials/BearerCredentials) where Python takes framework HTTPBasicCredentials/HTTPAuthorizationCredentials; same auth check (mirrors Java's records).
signalwire.core.auth_handler.AuthHandler.verify_bearer_token: cpp_idiom_carrier: AuthHandler verify_* take typed C++ credential carriers (BasicCredentials/BearerCredentials) where Python takes framework HTTPBasicCredentials/HTTPAuthorizationCredentials; same auth check (mirrors Java's records).
signalwire.core.contexts.Context.set_enter_fillers: cpp_kwargs_json: the C++ method takes a nlohmann::json / typed params object where Python spreads **kwargs or returns a native dict; same wire shape (the port's kwargs/return idiom).
signalwire.core.contexts.Context.set_exit_fillers: cpp_kwargs_json: the C++ method takes a nlohmann::json / typed params object where Python spreads **kwargs or returns a native dict; same wire shape (the port's kwargs/return idiom).
signalwire.core.contexts.Context.to_dict: cpp_kwargs_json: the C++ method takes a nlohmann::json / typed params object where Python spreads **kwargs or returns a native dict; same wire shape (the port's kwargs/return idiom).
signalwire.core.contexts.ContextBuilder.to_dict: cpp_kwargs_json: the C++ method takes a nlohmann::json / typed params object where Python spreads **kwargs or returns a native dict; same wire shape (the port's kwargs/return idiom).
signalwire.core.contexts.GatherInfo.to_dict: cpp_kwargs_json: the C++ method takes a nlohmann::json / typed params object where Python spreads **kwargs or returns a native dict; same wire shape (the port's kwargs/return idiom).
signalwire.core.contexts.GatherQuestion.to_dict: cpp_kwargs_json: the C++ method takes a nlohmann::json / typed params object where Python spreads **kwargs or returns a native dict; same wire shape (the port's kwargs/return idiom).
signalwire.core.contexts.Step.to_dict: cpp_kwargs_json: the C++ method takes a nlohmann::json / typed params object where Python spreads **kwargs or returns a native dict; same wire shape (the port's kwargs/return idiom).
signalwire.core.data_map.DataMap.webhook: cpp_kwargs_json: the C++ method takes a nlohmann::json / typed params object where Python spreads **kwargs or returns a native dict; same wire shape (the port's kwargs/return idiom).
signalwire.core.function_result.FunctionResult.add_dynamic_hints: cpp_kwargs_json: the C++ method takes a nlohmann::json / typed params object where Python spreads **kwargs or returns a native dict; same wire shape (the port's kwargs/return idiom).
signalwire.core.function_result.FunctionResult.execute_rpc: cpp_kwargs_json: the C++ method takes a nlohmann::json / typed params object where Python spreads **kwargs or returns a native dict; same wire shape (the port's kwargs/return idiom).
signalwire.core.function_result.FunctionResult.toggle_functions: cpp_kwargs_json: the C++ method takes a nlohmann::json / typed params object where Python spreads **kwargs or returns a native dict; same wire shape (the port's kwargs/return idiom).
signalwire.core.logging_config.strip_control_chars: cpp_processor_idiom: Python strip_control_chars is a structlog processor(logger, method_name, event_dict)->dict; C++ implements the value-sanitizing core as strip_control_chars(string)->string (no structlog processor chain in C++) — same control-char stripping.
signalwire.core.mixins.ai_config_mixin.AIConfigMixin.set_internal_fillers: cpp_kwargs_json: the mixin method (projected onto the flattened AgentBase) takes typed C++ params / a nlohmann::json where Python spreads **kwargs; same behavior (the port's kwargs idiom).
signalwire.core.mixins.skill_mixin.SkillMixin.add_skill: cpp_kwargs_json: the mixin method (projected onto the flattened AgentBase) takes typed C++ params / a nlohmann::json where Python spreads **kwargs; same behavior (the port's kwargs idiom).
signalwire.core.mixins.tool_mixin.ToolMixin.define_tools: cpp_kwargs_json: the mixin method (projected onto the flattened AgentBase) takes typed C++ params / a nlohmann::json where Python spreads **kwargs; same behavior (the port's kwargs idiom).
signalwire.core.mixins.tool_mixin.ToolMixin.on_function_call: cpp_kwargs_json: the mixin method (projected onto the flattened AgentBase) takes typed C++ params / a nlohmann::json where Python spreads **kwargs; same behavior (the port's kwargs idiom).
signalwire.core.mixins.web_mixin.WebMixin.register_routing_callback: cpp_kwargs_json: the mixin method (projected onto the flattened AgentBase) takes typed C++ params / a nlohmann::json where Python spreads **kwargs; same behavior (the port's kwargs idiom).
signalwire.core.pom_builder.PomBuilder.from_sections: cpp_kwargs_json: PomBuilder.add_section/add_subsection/add_to_section take typed C++ params (optional<vector>/json) where Python spreads keyword args (bullets/numbered/subsections); same POM structure (mirrors Java PomBuilder).
signalwire.core.security.security_utils.filter_sensitive_headers: cpp_kwargs_json: security_utils free function takes/returns the C++ typed form (map<string,string>/string) where Python uses dicts; same hygiene behavior.
signalwire.core.security_config.SecurityConfig.validate_ssl_config: cpp_kwargs_positional: SecurityConfig ctor takes typed C++ params where Python takes config_file/service_name keyword args; same env-driven config (mirrors Java SecurityConfig).
signalwire.core.skill_manager.SkillManager.loaded_skills: cpp_kwargs_json: skill-registry/manager method takes typed C++ params / json where Python spreads **kwargs or returns a native dict; same skill behavior (the port's idiom).
signalwire.core.swaig_function.SWAIGFunction.__init__: cpp_kwargs_json: SWAIGFunction ctor/__call__ take typed C++ params + a trailing json for extra_swaig_fields where Python spreads **kwargs; operator()/call invoke the handler (same SWAIG descriptor).
signalwire.core.swaig_function.SWAIGFunction.validate_args: cpp_kwargs_json: SWAIGFunction ctor/__call__ take typed C++ params + a trailing json for extra_swaig_fields where Python spreads **kwargs; operator()/call invoke the handler (same SWAIG descriptor).
signalwire.core.swml_builder.SWMLBuilder.add_section: cpp_fluent_self: SWMLBuilder verb methods return the concrete SWMLBuilder& (fluent chaining) where Python's type hint is Self; and kwargs land as a trailing nlohmann::json — same document, C++ builder idiom.
signalwire.core.swml_builder.SWMLBuilder.ai: cpp_fluent_self: SWMLBuilder verb methods return the concrete SWMLBuilder& (fluent chaining) where Python's type hint is Self; and kwargs land as a trailing nlohmann::json — same document, C++ builder idiom.
signalwire.core.swml_builder.SWMLBuilder.answer: cpp_fluent_self: SWMLBuilder verb methods return the concrete SWMLBuilder& (fluent chaining) where Python's type hint is Self; and kwargs land as a trailing nlohmann::json — same document, C++ builder idiom.
signalwire.core.swml_builder.SWMLBuilder.hangup: cpp_fluent_self: SWMLBuilder verb methods return the concrete SWMLBuilder& (fluent chaining) where Python's type hint is Self; and kwargs land as a trailing nlohmann::json — same document, C++ builder idiom.
signalwire.core.swml_builder.SWMLBuilder.play: cpp_fluent_self: SWMLBuilder verb methods return the concrete SWMLBuilder& (fluent chaining) where Python's type hint is Self; and kwargs land as a trailing nlohmann::json — same document, C++ builder idiom.
signalwire.core.swml_builder.SWMLBuilder.reset: cpp_fluent_self: SWMLBuilder verb methods return the concrete SWMLBuilder& (fluent chaining) where Python's type hint is Self; and kwargs land as a trailing nlohmann::json — same document, C++ builder idiom.
signalwire.core.swml_builder.SWMLBuilder.say: cpp_fluent_self: SWMLBuilder verb methods return the concrete SWMLBuilder& (fluent chaining) where Python's type hint is Self; and kwargs land as a trailing nlohmann::json — same document, C++ builder idiom.
signalwire.core.swml_handler.AIVerbHandler.build_config: cpp_kwargs_json: verb-handler build_config takes a single nlohmann::json kwargs object (plus a typed overload) where Python spreads **kwargs; validate_config is a C++ overload the reference records once — same verb config shape (kwargs idiom).
signalwire.core.swml_handler.AIVerbHandler.validate_config: cpp_kwargs_json: verb-handler build_config takes a single nlohmann::json kwargs object (plus a typed overload) where Python spreads **kwargs; validate_config is a C++ overload the reference records once — same verb config shape (kwargs idiom).
signalwire.core.swml_handler.SWMLVerbHandler.validate_config: cpp_kwargs_json: verb-handler build_config takes a single nlohmann::json kwargs object (plus a typed overload) where Python spreads **kwargs; validate_config is a C++ overload the reference records once — same verb config shape (kwargs idiom).
signalwire.core.swml_renderer.SwmlRenderer.render_swml: cpp_options_struct: SwmlRenderer.render_swml collapses Python's ~14 keyword render options into a RenderOptions struct arg (the port's options-object idiom, mirrors Java's RenderOptions) — same rendered SWML.
signalwire.core.swml_service.SWMLService.add_verb_to_section: cpp_kwargs_json: SWMLService method takes typed C++ params / a nlohmann::json where Python spreads keyword args; add_verb_to_section/register_* carry the C++ shape (kwargs idiom).
signalwire.core.swml_service.SWMLService.register_routing_callback: cpp_kwargs_json: SWMLService method takes typed C++ params / a nlohmann::json where Python spreads keyword args; add_verb_to_section/register_* carry the C++ shape (kwargs idiom).
signalwire.prefabs.concierge.ConciergeAgent.on_summary: cpp_overload: the C++ prefab method is a real handler/callback whose signature the reference records once (on_summary/on_swml_request take the C++ request+headers form); same behavior, port overload idiom (mirrors Java prefabs).
signalwire.prefabs.faq_bot.FAQBotAgent.on_summary: cpp_overload: the C++ prefab method is a real handler/callback whose signature the reference records once (on_summary/on_swml_request take the C++ request+headers form); same behavior, port overload idiom (mirrors Java prefabs).
signalwire.prefabs.info_gatherer.InfoGathererAgent.on_swml_request: cpp_overload: the C++ prefab method is a real handler/callback whose signature the reference records once (on_summary/on_swml_request take the C++ request+headers form); same behavior, port overload idiom (mirrors Java prefabs).
signalwire.prefabs.info_gatherer.InfoGathererAgent.set_question_callback: cpp_overload: the C++ prefab method is a real handler/callback whose signature the reference records once (on_summary/on_swml_request take the C++ request+headers form); same behavior, port overload idiom (mirrors Java prefabs).
signalwire.prefabs.receptionist.ReceptionistAgent.on_summary: cpp_overload: the C++ prefab method is a real handler/callback whose signature the reference records once (on_summary/on_swml_request take the C++ request+headers form); same behavior, port overload idiom (mirrors Java prefabs).
signalwire.prefabs.survey.SurveyAgent.on_summary: cpp_overload: the C++ prefab method is a real handler/callback whose signature the reference records once (on_summary/on_swml_request take the C++ request+headers form); same behavior, port overload idiom (mirrors Java prefabs).
signalwire.register_skill: cpp_kwargs_json: top-level/module free function takes typed C++ params (factory / json) where Python spreads **kwargs or a type object; same registration/behavior (the port's free-function idiom).
signalwire.relay.call.AIAction.__init__: cpp_unified_action: C++ flattens every RELAY call-action onto a single relay::Action (concrete PlayAction/RecordAction/... inherit its ctor); the projected __init__/start_input_timers carry the unified Action's signature, not Python's per-subclass one (documented cpp_unified_action idiom).
signalwire.relay.call.Action.__init__: cpp_unified_action: C++ flattens every RELAY call-action onto a single relay::Action (concrete PlayAction/RecordAction/... inherit its ctor); the projected __init__/start_input_timers carry the unified Action's signature, not Python's per-subclass one (documented cpp_unified_action idiom).
signalwire.relay.call.Action.result: cpp_unified_action: C++ flattens every RELAY call-action onto a single relay::Action (concrete PlayAction/RecordAction/... inherit its ctor); the projected __init__/start_input_timers carry the unified Action's signature, not Python's per-subclass one (documented cpp_unified_action idiom).
signalwire.relay.call.Action.wait: cpp_unified_action: C++ flattens every RELAY call-action onto a single relay::Action (concrete PlayAction/RecordAction/... inherit its ctor); the projected __init__/start_input_timers carry the unified Action's signature, not Python's per-subclass one (documented cpp_unified_action idiom).
signalwire.relay.call.Call.ai_hold: cpp_kwargs_json: Python Call verb method takes **kwargs / typed keyword params; the C++ Call method takes a single nlohmann::json params object (the port's kwargs idiom) and returns a relay::Action rather than Python's dict — same wire frame (verified against relay_apis.c).
signalwire.relay.call.Call.ai_message: cpp_kwargs_json: Python Call verb method takes **kwargs / typed keyword params; the C++ Call method takes a single nlohmann::json params object (the port's kwargs idiom) and returns a relay::Action rather than Python's dict — same wire frame (verified against relay_apis.c).
signalwire.relay.call.Call.ai_unhold: cpp_kwargs_json: Python Call verb method takes **kwargs / typed keyword params; the C++ Call method takes a single nlohmann::json params object (the port's kwargs idiom) and returns a relay::Action rather than Python's dict — same wire frame (verified against relay_apis.c).
signalwire.relay.call.Call.amazon_bedrock: cpp_kwargs_json: Python Call verb method takes **kwargs / typed keyword params; the C++ Call method takes a single nlohmann::json params object (the port's kwargs idiom) and returns a relay::Action rather than Python's dict — same wire frame (verified against relay_apis.c).
signalwire.relay.call.Call.bind_digit: cpp_kwargs_json: Python Call verb method takes **kwargs / typed keyword params; the C++ Call method takes a single nlohmann::json params object (the port's kwargs idiom) and returns a relay::Action rather than Python's dict — same wire frame (verified against relay_apis.c).
signalwire.relay.call.Call.clear_digit_bindings: cpp_kwargs_json: Python Call verb method takes **kwargs / typed keyword params; the C++ Call method takes a single nlohmann::json params object (the port's kwargs idiom) and returns a relay::Action rather than Python's dict — same wire frame (verified against relay_apis.c).
signalwire.relay.call.Call.denoise: cpp_kwargs_json: Python Call verb method takes **kwargs / typed keyword params; the C++ Call method takes a single nlohmann::json params object (the port's kwargs idiom) and returns a relay::Action rather than Python's dict — same wire frame (verified against relay_apis.c).
signalwire.relay.call.Call.denoise_stop: cpp_kwargs_json: Python Call verb method takes **kwargs / typed keyword params; the C++ Call method takes a single nlohmann::json params object (the port's kwargs idiom) and returns a relay::Action rather than Python's dict — same wire frame (verified against relay_apis.c).
signalwire.relay.call.Call.echo: cpp_kwargs_json: Python Call verb method takes **kwargs / typed keyword params; the C++ Call method takes a single nlohmann::json params object (the port's kwargs idiom) and returns a relay::Action rather than Python's dict — same wire frame (verified against relay_apis.c).
signalwire.relay.call.Call.leave_conference: cpp_kwargs_json: Python Call verb method takes **kwargs / typed keyword params; the C++ Call method takes a single nlohmann::json params object (the port's kwargs idiom) and returns a relay::Action rather than Python's dict — same wire frame (verified against relay_apis.c).
signalwire.relay.call.Call.leave_room: cpp_kwargs_json: Python Call verb method takes **kwargs / typed keyword params; the C++ Call method takes a single nlohmann::json params object (the port's kwargs idiom) and returns a relay::Action rather than Python's dict — same wire frame (verified against relay_apis.c).
signalwire.relay.call.Call.on: cpp_kwargs_json: Python Call verb method takes **kwargs / typed keyword params; the C++ Call method takes a single nlohmann::json params object (the port's kwargs idiom) and returns a relay::Action rather than Python's dict — same wire frame (verified against relay_apis.c).
signalwire.relay.call.Call.pass_: cpp_kwargs_json: Python Call verb method takes **kwargs / typed keyword params; the C++ Call method takes a single nlohmann::json params object (the port's kwargs idiom) and returns a relay::Action rather than Python's dict — same wire frame (verified against relay_apis.c).
signalwire.relay.call.Call.queue_enter: cpp_kwargs_json: Python Call verb method takes **kwargs / typed keyword params; the C++ Call method takes a single nlohmann::json params object (the port's kwargs idiom) and returns a relay::Action rather than Python's dict — same wire frame (verified against relay_apis.c).
signalwire.relay.call.Call.queue_leave: cpp_kwargs_json: Python Call verb method takes **kwargs / typed keyword params; the C++ Call method takes a single nlohmann::json params object (the port's kwargs idiom) and returns a relay::Action rather than Python's dict — same wire frame (verified against relay_apis.c).
signalwire.relay.call.Call.refer: cpp_kwargs_json: Python Call verb method takes **kwargs / typed keyword params; the C++ Call method takes a single nlohmann::json params object (the port's kwargs idiom) and returns a relay::Action rather than Python's dict — same wire frame (verified against relay_apis.c).
signalwire.relay.call.Call.user_event: cpp_kwargs_json: Python Call verb method takes **kwargs / typed keyword params; the C++ Call method takes a single nlohmann::json params object (the port's kwargs idiom) and returns a relay::Action rather than Python's dict — same wire frame (verified against relay_apis.c).
signalwire.relay.call.Call.wait_for: cpp_kwargs_json: Python Call verb method takes **kwargs / typed keyword params; the C++ Call method takes a single nlohmann::json params object (the port's kwargs idiom) and returns a relay::Action rather than Python's dict — same wire frame (verified against relay_apis.c).
signalwire.relay.call.CollectAction.__init__: cpp_unified_action: C++ flattens every RELAY call-action onto a single relay::Action (concrete PlayAction/RecordAction/... inherit its ctor); the projected __init__/start_input_timers carry the unified Action's signature, not Python's per-subclass one (documented cpp_unified_action idiom).
signalwire.relay.call.CollectAction.start_input_timers: cpp_unified_action: C++ flattens every RELAY call-action onto a single relay::Action (concrete PlayAction/RecordAction/... inherit its ctor); the projected __init__/start_input_timers carry the unified Action's signature, not Python's per-subclass one (documented cpp_unified_action idiom).
signalwire.relay.call.DetectAction.__init__: cpp_unified_action: C++ flattens every RELAY call-action onto a single relay::Action (concrete PlayAction/RecordAction/... inherit its ctor); the projected __init__/start_input_timers carry the unified Action's signature, not Python's per-subclass one (documented cpp_unified_action idiom).
signalwire.relay.call.FaxAction.__init__: cpp_unified_action: C++ flattens every RELAY call-action onto a single relay::Action (concrete PlayAction/RecordAction/... inherit its ctor); the projected __init__/start_input_timers carry the unified Action's signature, not Python's per-subclass one (documented cpp_unified_action idiom).
signalwire.relay.call.PayAction.__init__: cpp_unified_action: C++ flattens every RELAY call-action onto a single relay::Action (concrete PlayAction/RecordAction/... inherit its ctor); the projected __init__/start_input_timers carry the unified Action's signature, not Python's per-subclass one (documented cpp_unified_action idiom).
signalwire.relay.call.PlayAction.__init__: cpp_unified_action: C++ flattens every RELAY call-action onto a single relay::Action (concrete PlayAction/RecordAction/... inherit its ctor); the projected __init__/start_input_timers carry the unified Action's signature, not Python's per-subclass one (documented cpp_unified_action idiom).
signalwire.relay.call.RecordAction.__init__: cpp_unified_action: C++ flattens every RELAY call-action onto a single relay::Action (concrete PlayAction/RecordAction/... inherit its ctor); the projected __init__/start_input_timers carry the unified Action's signature, not Python's per-subclass one (documented cpp_unified_action idiom).
signalwire.relay.call.StandaloneCollectAction.__init__: cpp_unified_action: C++ flattens every RELAY call-action onto a single relay::Action (concrete PlayAction/RecordAction/... inherit its ctor); the projected __init__/start_input_timers carry the unified Action's signature, not Python's per-subclass one (documented cpp_unified_action idiom).
signalwire.relay.call.StandaloneCollectAction.start_input_timers: cpp_unified_action: C++ flattens every RELAY call-action onto a single relay::Action (concrete PlayAction/RecordAction/... inherit its ctor); the projected __init__/start_input_timers carry the unified Action's signature, not Python's per-subclass one (documented cpp_unified_action idiom).
signalwire.relay.call.StreamAction.__init__: cpp_unified_action: C++ flattens every RELAY call-action onto a single relay::Action (concrete PlayAction/RecordAction/... inherit its ctor); the projected __init__/start_input_timers carry the unified Action's signature, not Python's per-subclass one (documented cpp_unified_action idiom).
signalwire.relay.call.TapAction.__init__: cpp_unified_action: C++ flattens every RELAY call-action onto a single relay::Action (concrete PlayAction/RecordAction/... inherit its ctor); the projected __init__/start_input_timers carry the unified Action's signature, not Python's per-subclass one (documented cpp_unified_action idiom).
signalwire.relay.call.TranscribeAction.__init__: cpp_unified_action: C++ flattens every RELAY call-action onto a single relay::Action (concrete PlayAction/RecordAction/... inherit its ctor); the projected __init__/start_input_timers carry the unified Action's signature, not Python's per-subclass one (documented cpp_unified_action idiom).
signalwire.relay.call.AIAction.stop: cpp_unified_action: the concrete action's control method is inherited from the unified relay::Action; it fires the `<prefix>.<op>` frame (control_id) and returns void, where Python's coroutine awaits and returns the result dict — same wire frame, C++ fire-and-forget return idiom.
signalwire.relay.call.CollectAction.pause: cpp_unified_action: the concrete action's control method is inherited from the unified relay::Action; it fires the `<prefix>.<op>` frame (control_id) and returns void, where Python's coroutine awaits and returns the result dict — same wire frame, C++ fire-and-forget return idiom.
signalwire.relay.call.CollectAction.resume: cpp_unified_action: the concrete action's control method is inherited from the unified relay::Action; it fires the `<prefix>.<op>` frame (control_id) and returns void, where Python's coroutine awaits and returns the result dict — same wire frame, C++ fire-and-forget return idiom.
signalwire.relay.call.CollectAction.stop: cpp_unified_action: the concrete action's control method is inherited from the unified relay::Action; it fires the `<prefix>.<op>` frame (control_id) and returns void, where Python's coroutine awaits and returns the result dict — same wire frame, C++ fire-and-forget return idiom.
signalwire.relay.call.CollectAction.volume: cpp_unified_action: the concrete action's control method is inherited from the unified relay::Action; it fires the `<prefix>.<op>` frame (control_id) and returns void, where Python's coroutine awaits and returns the result dict — same wire frame, C++ fire-and-forget return idiom.
signalwire.relay.call.DetectAction.stop: cpp_unified_action: the concrete action's control method is inherited from the unified relay::Action; it fires the `<prefix>.<op>` frame (control_id) and returns void, where Python's coroutine awaits and returns the result dict — same wire frame, C++ fire-and-forget return idiom.
signalwire.relay.call.FaxAction.stop: cpp_unified_action: the concrete action's control method is inherited from the unified relay::Action; it fires the `<prefix>.<op>` frame (control_id) and returns void, where Python's coroutine awaits and returns the result dict — same wire frame, C++ fire-and-forget return idiom.
signalwire.relay.call.PayAction.stop: cpp_unified_action: the concrete action's control method is inherited from the unified relay::Action; it fires the `<prefix>.<op>` frame (control_id) and returns void, where Python's coroutine awaits and returns the result dict — same wire frame, C++ fire-and-forget return idiom.
signalwire.relay.call.PlayAction.pause: cpp_unified_action: the concrete action's control method is inherited from the unified relay::Action; it fires the `<prefix>.<op>` frame (control_id) and returns void, where Python's coroutine awaits and returns the result dict — same wire frame, C++ fire-and-forget return idiom.
signalwire.relay.call.PlayAction.resume: cpp_unified_action: the concrete action's control method is inherited from the unified relay::Action; it fires the `<prefix>.<op>` frame (control_id) and returns void, where Python's coroutine awaits and returns the result dict — same wire frame, C++ fire-and-forget return idiom.
signalwire.relay.call.PlayAction.stop: cpp_unified_action: the concrete action's control method is inherited from the unified relay::Action; it fires the `<prefix>.<op>` frame (control_id) and returns void, where Python's coroutine awaits and returns the result dict — same wire frame, C++ fire-and-forget return idiom.
signalwire.relay.call.PlayAction.volume: cpp_unified_action: the concrete action's control method is inherited from the unified relay::Action; it fires the `<prefix>.<op>` frame (control_id) and returns void, where Python's coroutine awaits and returns the result dict — same wire frame, C++ fire-and-forget return idiom.
signalwire.relay.call.RecordAction.pause: cpp_unified_action: the concrete action's control method is inherited from the unified relay::Action; it fires the `<prefix>.<op>` frame (control_id) and returns void, where Python's coroutine awaits and returns the result dict — same wire frame, C++ fire-and-forget return idiom.
signalwire.relay.call.RecordAction.resume: cpp_unified_action: the concrete action's control method is inherited from the unified relay::Action; it fires the `<prefix>.<op>` frame (control_id) and returns void, where Python's coroutine awaits and returns the result dict — same wire frame, C++ fire-and-forget return idiom.
signalwire.relay.call.RecordAction.stop: cpp_unified_action: the concrete action's control method is inherited from the unified relay::Action; it fires the `<prefix>.<op>` frame (control_id) and returns void, where Python's coroutine awaits and returns the result dict — same wire frame, C++ fire-and-forget return idiom.
signalwire.relay.call.StandaloneCollectAction.stop: cpp_unified_action: the concrete action's control method is inherited from the unified relay::Action; it fires the `<prefix>.<op>` frame (control_id) and returns void, where Python's coroutine awaits and returns the result dict — same wire frame, C++ fire-and-forget return idiom.
signalwire.relay.call.StreamAction.stop: cpp_unified_action: the concrete action's control method is inherited from the unified relay::Action; it fires the `<prefix>.<op>` frame (control_id) and returns void, where Python's coroutine awaits and returns the result dict — same wire frame, C++ fire-and-forget return idiom.
signalwire.relay.call.TapAction.stop: cpp_unified_action: the concrete action's control method is inherited from the unified relay::Action; it fires the `<prefix>.<op>` frame (control_id) and returns void, where Python's coroutine awaits and returns the result dict — same wire frame, C++ fire-and-forget return idiom.
signalwire.relay.call.TranscribeAction.stop: cpp_unified_action: the concrete action's control method is inherited from the unified relay::Action; it fires the `<prefix>.<op>` frame (control_id) and returns void, where Python's coroutine awaits and returns the result dict — same wire frame, C++ fire-and-forget return idiom.
signalwire.relay.event.CallReceiveEvent.__init__: cpp_typed_event_ctor: C++ typed RELAY events construct from a single nlohmann::json payload (from_payload(json)/ctor), where Python spreads the decoded event fields as typed __init__ params; same wire event, C++ passes the raw JSON object (the port's typed-event idiom).
signalwire.relay.event.CallReceiveEvent.from_payload: cpp_typed_event_ctor: C++ typed RELAY events construct from a single nlohmann::json payload (from_payload(json)/ctor), where Python spreads the decoded event fields as typed __init__ params; same wire event, C++ passes the raw JSON object (the port's typed-event idiom).
signalwire.relay.event.CallStateEvent.__init__: cpp_typed_event_ctor: C++ typed RELAY events construct from a single nlohmann::json payload (from_payload(json)/ctor), where Python spreads the decoded event fields as typed __init__ params; same wire event, C++ passes the raw JSON object (the port's typed-event idiom).
signalwire.relay.event.CallStateEvent.from_payload: cpp_typed_event_ctor: C++ typed RELAY events construct from a single nlohmann::json payload (from_payload(json)/ctor), where Python spreads the decoded event fields as typed __init__ params; same wire event, C++ passes the raw JSON object (the port's typed-event idiom).
signalwire.relay.event.CallingErrorEvent.__init__: cpp_typed_event_ctor: C++ typed RELAY events construct from a single nlohmann::json payload (from_payload(json)/ctor), where Python spreads the decoded event fields as typed __init__ params; same wire event, C++ passes the raw JSON object (the port's typed-event idiom).
signalwire.relay.event.CallingErrorEvent.from_payload: cpp_typed_event_ctor: C++ typed RELAY events construct from a single nlohmann::json payload (from_payload(json)/ctor), where Python spreads the decoded event fields as typed __init__ params; same wire event, C++ passes the raw JSON object (the port's typed-event idiom).
signalwire.relay.event.CollectEvent.__init__: cpp_typed_event_ctor: C++ typed RELAY events construct from a single nlohmann::json payload (from_payload(json)/ctor), where Python spreads the decoded event fields as typed __init__ params; same wire event, C++ passes the raw JSON object (the port's typed-event idiom).
signalwire.relay.event.CollectEvent.from_payload: cpp_typed_event_ctor: C++ typed RELAY events construct from a single nlohmann::json payload (from_payload(json)/ctor), where Python spreads the decoded event fields as typed __init__ params; same wire event, C++ passes the raw JSON object (the port's typed-event idiom).
signalwire.relay.event.ConferenceEvent.__init__: cpp_typed_event_ctor: C++ typed RELAY events construct from a single nlohmann::json payload (from_payload(json)/ctor), where Python spreads the decoded event fields as typed __init__ params; same wire event, C++ passes the raw JSON object (the port's typed-event idiom).
signalwire.relay.event.ConferenceEvent.from_payload: cpp_typed_event_ctor: C++ typed RELAY events construct from a single nlohmann::json payload (from_payload(json)/ctor), where Python spreads the decoded event fields as typed __init__ params; same wire event, C++ passes the raw JSON object (the port's typed-event idiom).
signalwire.relay.event.ConnectEvent.__init__: cpp_typed_event_ctor: C++ typed RELAY events construct from a single nlohmann::json payload (from_payload(json)/ctor), where Python spreads the decoded event fields as typed __init__ params; same wire event, C++ passes the raw JSON object (the port's typed-event idiom).
signalwire.relay.event.ConnectEvent.from_payload: cpp_typed_event_ctor: C++ typed RELAY events construct from a single nlohmann::json payload (from_payload(json)/ctor), where Python spreads the decoded event fields as typed __init__ params; same wire event, C++ passes the raw JSON object (the port's typed-event idiom).
signalwire.relay.event.DenoiseEvent.__init__: cpp_typed_event_ctor: C++ typed RELAY events construct from a single nlohmann::json payload (from_payload(json)/ctor), where Python spreads the decoded event fields as typed __init__ params; same wire event, C++ passes the raw JSON object (the port's typed-event idiom).
signalwire.relay.event.DenoiseEvent.from_payload: cpp_typed_event_ctor: C++ typed RELAY events construct from a single nlohmann::json payload (from_payload(json)/ctor), where Python spreads the decoded event fields as typed __init__ params; same wire event, C++ passes the raw JSON object (the port's typed-event idiom).
signalwire.relay.event.DetectEvent.__init__: cpp_typed_event_ctor: C++ typed RELAY events construct from a single nlohmann::json payload (from_payload(json)/ctor), where Python spreads the decoded event fields as typed __init__ params; same wire event, C++ passes the raw JSON object (the port's typed-event idiom).
signalwire.relay.event.DetectEvent.from_payload: cpp_typed_event_ctor: C++ typed RELAY events construct from a single nlohmann::json payload (from_payload(json)/ctor), where Python spreads the decoded event fields as typed __init__ params; same wire event, C++ passes the raw JSON object (the port's typed-event idiom).
signalwire.relay.event.DialEvent.__init__: cpp_typed_event_ctor: C++ typed RELAY events construct from a single nlohmann::json payload (from_payload(json)/ctor), where Python spreads the decoded event fields as typed __init__ params; same wire event, C++ passes the raw JSON object (the port's typed-event idiom).
signalwire.relay.event.DialEvent.from_payload: cpp_typed_event_ctor: C++ typed RELAY events construct from a single nlohmann::json payload (from_payload(json)/ctor), where Python spreads the decoded event fields as typed __init__ params; same wire event, C++ passes the raw JSON object (the port's typed-event idiom).
signalwire.relay.event.EchoEvent.__init__: cpp_typed_event_ctor: C++ typed RELAY events construct from a single nlohmann::json payload (from_payload(json)/ctor), where Python spreads the decoded event fields as typed __init__ params; same wire event, C++ passes the raw JSON object (the port's typed-event idiom).
signalwire.relay.event.EchoEvent.from_payload: cpp_typed_event_ctor: C++ typed RELAY events construct from a single nlohmann::json payload (from_payload(json)/ctor), where Python spreads the decoded event fields as typed __init__ params; same wire event, C++ passes the raw JSON object (the port's typed-event idiom).
signalwire.relay.event.FaxEvent.__init__: cpp_typed_event_ctor: C++ typed RELAY events construct from a single nlohmann::json payload (from_payload(json)/ctor), where Python spreads the decoded event fields as typed __init__ params; same wire event, C++ passes the raw JSON object (the port's typed-event idiom).
signalwire.relay.event.FaxEvent.from_payload: cpp_typed_event_ctor: C++ typed RELAY events construct from a single nlohmann::json payload (from_payload(json)/ctor), where Python spreads the decoded event fields as typed __init__ params; same wire event, C++ passes the raw JSON object (the port's typed-event idiom).
signalwire.relay.event.HoldEvent.__init__: cpp_typed_event_ctor: C++ typed RELAY events construct from a single nlohmann::json payload (from_payload(json)/ctor), where Python spreads the decoded event fields as typed __init__ params; same wire event, C++ passes the raw JSON object (the port's typed-event idiom).
signalwire.relay.event.HoldEvent.from_payload: cpp_typed_event_ctor: C++ typed RELAY events construct from a single nlohmann::json payload (from_payload(json)/ctor), where Python spreads the decoded event fields as typed __init__ params; same wire event, C++ passes the raw JSON object (the port's typed-event idiom).
signalwire.relay.event.MessageReceiveEvent.__init__: cpp_typed_event_ctor: C++ typed RELAY events construct from a single nlohmann::json payload (from_payload(json)/ctor), where Python spreads the decoded event fields as typed __init__ params; same wire event, C++ passes the raw JSON object (the port's typed-event idiom).
signalwire.relay.event.MessageReceiveEvent.from_payload: cpp_typed_event_ctor: C++ typed RELAY events construct from a single nlohmann::json payload (from_payload(json)/ctor), where Python spreads the decoded event fields as typed __init__ params; same wire event, C++ passes the raw JSON object (the port's typed-event idiom).
signalwire.relay.event.MessageStateEvent.__init__: cpp_typed_event_ctor: C++ typed RELAY events construct from a single nlohmann::json payload (from_payload(json)/ctor), where Python spreads the decoded event fields as typed __init__ params; same wire event, C++ passes the raw JSON object (the port's typed-event idiom).
signalwire.relay.event.MessageStateEvent.from_payload: cpp_typed_event_ctor: C++ typed RELAY events construct from a single nlohmann::json payload (from_payload(json)/ctor), where Python spreads the decoded event fields as typed __init__ params; same wire event, C++ passes the raw JSON object (the port's typed-event idiom).
signalwire.relay.event.PayEvent.__init__: cpp_typed_event_ctor: C++ typed RELAY events construct from a single nlohmann::json payload (from_payload(json)/ctor), where Python spreads the decoded event fields as typed __init__ params; same wire event, C++ passes the raw JSON object (the port's typed-event idiom).
signalwire.relay.event.PayEvent.from_payload: cpp_typed_event_ctor: C++ typed RELAY events construct from a single nlohmann::json payload (from_payload(json)/ctor), where Python spreads the decoded event fields as typed __init__ params; same wire event, C++ passes the raw JSON object (the port's typed-event idiom).
signalwire.relay.event.PlayEvent.__init__: cpp_typed_event_ctor: C++ typed RELAY events construct from a single nlohmann::json payload (from_payload(json)/ctor), where Python spreads the decoded event fields as typed __init__ params; same wire event, C++ passes the raw JSON object (the port's typed-event idiom).
signalwire.relay.event.PlayEvent.from_payload: cpp_typed_event_ctor: C++ typed RELAY events construct from a single nlohmann::json payload (from_payload(json)/ctor), where Python spreads the decoded event fields as typed __init__ params; same wire event, C++ passes the raw JSON object (the port's typed-event idiom).
signalwire.relay.event.QueueEvent.__init__: cpp_typed_event_ctor: C++ typed RELAY events construct from a single nlohmann::json payload (from_payload(json)/ctor), where Python spreads the decoded event fields as typed __init__ params; same wire event, C++ passes the raw JSON object (the port's typed-event idiom).
signalwire.relay.event.QueueEvent.from_payload: cpp_typed_event_ctor: C++ typed RELAY events construct from a single nlohmann::json payload (from_payload(json)/ctor), where Python spreads the decoded event fields as typed __init__ params; same wire event, C++ passes the raw JSON object (the port's typed-event idiom).
signalwire.relay.event.RecordEvent.__init__: cpp_typed_event_ctor: C++ typed RELAY events construct from a single nlohmann::json payload (from_payload(json)/ctor), where Python spreads the decoded event fields as typed __init__ params; same wire event, C++ passes the raw JSON object (the port's typed-event idiom).
signalwire.relay.event.RecordEvent.from_payload: cpp_typed_event_ctor: C++ typed RELAY events construct from a single nlohmann::json payload (from_payload(json)/ctor), where Python spreads the decoded event fields as typed __init__ params; same wire event, C++ passes the raw JSON object (the port's typed-event idiom).
signalwire.relay.event.ReferEvent.__init__: cpp_typed_event_ctor: C++ typed RELAY events construct from a single nlohmann::json payload (from_payload(json)/ctor), where Python spreads the decoded event fields as typed __init__ params; same wire event, C++ passes the raw JSON object (the port's typed-event idiom).
signalwire.relay.event.ReferEvent.from_payload: cpp_typed_event_ctor: C++ typed RELAY events construct from a single nlohmann::json payload (from_payload(json)/ctor), where Python spreads the decoded event fields as typed __init__ params; same wire event, C++ passes the raw JSON object (the port's typed-event idiom).
signalwire.relay.event.RelayEvent.__init__: cpp_typed_event_ctor: C++ typed RELAY events construct from a single nlohmann::json payload (from_payload(json)/ctor), where Python spreads the decoded event fields as typed __init__ params; same wire event, C++ passes the raw JSON object (the port's typed-event idiom).
signalwire.relay.event.RelayEvent.from_payload: cpp_typed_event_ctor: C++ typed RELAY events construct from a single nlohmann::json payload (from_payload(json)/ctor), where Python spreads the decoded event fields as typed __init__ params; same wire event, C++ passes the raw JSON object (the port's typed-event idiom).
signalwire.relay.event.SendDigitsEvent.__init__: cpp_typed_event_ctor: C++ typed RELAY events construct from a single nlohmann::json payload (from_payload(json)/ctor), where Python spreads the decoded event fields as typed __init__ params; same wire event, C++ passes the raw JSON object (the port's typed-event idiom).
signalwire.relay.event.SendDigitsEvent.from_payload: cpp_typed_event_ctor: C++ typed RELAY events construct from a single nlohmann::json payload (from_payload(json)/ctor), where Python spreads the decoded event fields as typed __init__ params; same wire event, C++ passes the raw JSON object (the port's typed-event idiom).
signalwire.relay.event.StreamEvent.__init__: cpp_typed_event_ctor: C++ typed RELAY events construct from a single nlohmann::json payload (from_payload(json)/ctor), where Python spreads the decoded event fields as typed __init__ params; same wire event, C++ passes the raw JSON object (the port's typed-event idiom).
signalwire.relay.event.StreamEvent.from_payload: cpp_typed_event_ctor: C++ typed RELAY events construct from a single nlohmann::json payload (from_payload(json)/ctor), where Python spreads the decoded event fields as typed __init__ params; same wire event, C++ passes the raw JSON object (the port's typed-event idiom).
signalwire.relay.event.TapEvent.__init__: cpp_typed_event_ctor: C++ typed RELAY events construct from a single nlohmann::json payload (from_payload(json)/ctor), where Python spreads the decoded event fields as typed __init__ params; same wire event, C++ passes the raw JSON object (the port's typed-event idiom).
signalwire.relay.event.TapEvent.from_payload: cpp_typed_event_ctor: C++ typed RELAY events construct from a single nlohmann::json payload (from_payload(json)/ctor), where Python spreads the decoded event fields as typed __init__ params; same wire event, C++ passes the raw JSON object (the port's typed-event idiom).
signalwire.relay.event.TranscribeEvent.__init__: cpp_typed_event_ctor: C++ typed RELAY events construct from a single nlohmann::json payload (from_payload(json)/ctor), where Python spreads the decoded event fields as typed __init__ params; same wire event, C++ passes the raw JSON object (the port's typed-event idiom).
signalwire.relay.event.TranscribeEvent.from_payload: cpp_typed_event_ctor: C++ typed RELAY events construct from a single nlohmann::json payload (from_payload(json)/ctor), where Python spreads the decoded event fields as typed __init__ params; same wire event, C++ passes the raw JSON object (the port's typed-event idiom).
signalwire.relay.event.parse_event: cpp_typed_event_ctor: C++ typed RELAY events construct from a single nlohmann::json payload (from_payload(json)/ctor), where Python spreads the decoded event fields as typed __init__ params; same wire event, C++ passes the raw JSON object (the port's typed-event idiom).
signalwire.relay.message.Message.on: cpp_kwargs_json: relay Message method takes a nlohmann::json / typed params where Python spreads **kwargs; same message frame (kwargs idiom).
signalwire.rest._base.CrudWithAddresses.__init__: cpp_crud_idiom: generated CrudWithAddresses base method takes the C++ typed params object where Python spreads **kwargs; same REST wire shape (documented CRUD idiom).
signalwire.skills.registry.SkillRegistry.get_skill_class: cpp_return_idiom: C++ get_skill_class returns bool (whether the skill factory is known) where Python returns the skill type object; C++ has no first-class type value — use create() to instantiate (same discovery-by-name contract).
signalwire.web.web_service.WebService.__init__: cpp_kwargs_positional: WebService ctor/start collapse Python's many keyword options (directories/basic_auth/allowed_extensions/ssl_cert/...) into positional C++ params / accessors; same static-file service behavior (mirrors Java WebService).
signalwire.web.web_service.WebService.start: cpp_kwargs_positional: WebService ctor/start collapse Python's many keyword options (directories/basic_auth/allowed_extensions/ssl_cert/...) into positional C++ params / accessors; same static-file service behavior (mirrors Java WebService).

## KNOWN PRE-EXISTING RESIDUAL (NOT item H/I) — gen-payload SWML AI-payload structs

The DRIFT gate still reports ~378 `missing-port` drifts under the `gen-payload`
fold (reference module `signalwire.core.swml_verbs_generated`: AIParams.*,
AIObject.*, UserSWAIGFunction.*, Webhook.*, LanguagesWith*.* etc.). These are
NOT part of item H/I. The Python oracle records each SWML-payload field as a
property-getter *method* (e.g. `AIParams.acknowledge_interruptions(self) ->
union<bool,SWMLVar>`), whereas the C++ port implements them as `std::optional<>`
*fields* on POD `struct`s (include/signalwire/core/swml_verbs_generated/*.hpp),
which the libclang signature enumerator does not emit as methods. This gap
predates item H/I (present and larger at HEAD: 869 total drifts, of which these
378 were a subset) and is owned by the SWML-payload work (item D). It is left
UNTAGGED on purpose — an honest gate failure, not silenced with a blanket
allowlist. Fix requires the signature enumerator to project POD-struct fields
under `swml_verbs_generated` as property-getters (or the port to expose them as
accessors).
signalwire.rest._request_options.RequestOptions.__init__: cpp_constructor_default_only: RequestOptions is an aggregate struct with public data fields (timeout/retries/retry_on_status/retry_backoff/abort_signal); Python's dataclass __init__ enumerates each field as a keyword. The full set is reachable via the C++ public fields (ro.retries = 1, ro.abort_signal = &flag) — same callable contract, aggregate-init idiom instead of a keyword ctor (go/ts/ruby/java value-struct match).
signalwire.rest._request_options.RequestOptions.abort_signal: cpp_field_not_property: Python exposes abort_signal as a @property getter *method*; C++ implements it as a public data member (std::atomic<bool>* abort_signal) the libclang enumerator does not emit as a method. Reachable directly as ro.abort_signal — same callable contract, public-field idiom (the RequestOptions data fields are deliberately not surface symbols, exactly as the Python dataclass fields aren't).
