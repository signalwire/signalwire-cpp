#!/usr/bin/env python3
"""enumerate_surface.py -- emit port_surface.json for the C++ SignalWire SDK.

This walks every ``include/signalwire/**/*.hpp`` and ``**/*.h`` header, parses
out the namespace/class/public-method structure with regex, and emits JSON
matching the shape of ``porting-sdk/python_surface.json``.

Output shape (identical to ``python_surface.json``)::

    {
      "version": "1",
      "generated_from": "signalwire-cpp @ <git sha>",
      "modules": {
        "signalwire.core.agent_base": {
          "classes": {
            "AgentBase": ["__init__", "set_prompt_text", ...]
          },
          "functions": [...]
        },
        ...
      }
    }

Symbol naming contract (from porting-sdk CHECKLIST_TEMPLATE.md
Phase 13 symbol-level surface parity):

  * Class names are kept as-is (``AgentBase``, ``FunctionResult``, ...).
  * Method names in this SDK are ALREADY snake_case (this is a C++ port
    that followed the Python naming) -- no translation needed.
  * Constructors (methods with the same name as their class, inside
    a public block) are emitted as ``__init__``.
  * C++ namespaces are translated to Python's canonical module path via
    ``CLASS_MODULE_MAP`` below. Port-only classes without a Python analog
    fall back to a native-namespace translation
    (``signalwire::rest::PhoneCallHandler`` -> ``signalwire.rest.phone_call_handler``).
  * Only ``public:`` members are emitted. Everything in ``private:`` or
    ``protected:`` is skipped.
  * Operator overloads are skipped (no Python analog in v1).
  * Destructors (``~Foo``) are skipped.
  * Friend declarations and forward declarations are skipped.

Regex-based parsing is pragmatic for this SDK size (~15 headers). libclang is
heavyweight overkill; the header surface is regular enough that a line-based
visibility scanner handles it.

Usage:
    python3 scripts/enumerate_surface.py                    # write port_surface.json
    python3 scripts/enumerate_surface.py --output FILE
    python3 scripts/enumerate_surface.py --check            # exit 1 on drift
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

# ---------------------------------------------------------------------------
# Class -> Python module mapping
# ---------------------------------------------------------------------------
# Every class in the C++ SDK has to be reported under a Python-reference dotted
# module name so the diff against ``python_surface.json`` lines up. The table
# below is the single source of truth for that mapping. When the Python
# reference renames a module, this table changes, not the parser.
#
# Classes NOT in this map fall back to the native-namespace translation
# (e.g. ``signalwire::rest::PhoneCallHandler`` -> ``signalwire.rest.phone_call_handler``).
CLASS_MODULE_MAP: dict[str, str] = {
    # -- agent ------------------------------------------------------------
    "AgentBase": "signalwire.core.agent_base",

    # -- pom --------------------------------------------------------------
    # PromptObjectModel has no name conflict; Section does (swml::Section).
    # PromptObjectModel can use the simple class-name map; Section is
    # disambiguated via CLASS_RENAME_MAP keyed on (signalwire::pom, Section).
    "PromptObjectModel": "signalwire.pom.pom",

    # -- contexts ---------------------------------------------------------
    "Context": "signalwire.core.contexts",
    "ContextBuilder": "signalwire.core.contexts",
    "GatherInfo": "signalwire.core.contexts",
    "GatherQuestion": "signalwire.core.contexts",
    "Step": "signalwire.core.contexts",

    # -- datamap ----------------------------------------------------------
    "DataMap": "signalwire.core.data_map",

    # -- swaig ------------------------------------------------------------
    "FunctionResult": "signalwire.core.function_result",
    "ToolDefinition": "signalwire.core.swaig_function",
    "SWAIGFunction": "signalwire.core.swaig_function",

    # -- swml verb-handler registry (core/swml_handler.hpp) ---------------
    "SWMLVerbHandler": "signalwire.core.swml_handler",
    "AIVerbHandler": "signalwire.core.swml_handler",
    "VerbHandlerRegistry": "signalwire.core.swml_handler",
    # -- swml builder / renderer -----------------------------------------
    "SWMLBuilder": "signalwire.core.swml_builder",
    "SwmlRenderer": "signalwire.core.swml_renderer",

    # -- core infra classes (auth/config/security/pom) --------------------
    "AuthHandler": "signalwire.core.auth_handler",
    "ConfigLoader": "signalwire.core.config_loader",
    "SecurityConfig": "signalwire.core.security_config",
    "PomBuilder": "signalwire.core.pom_builder",

    # -- skills -----------------------------------------------------------
    "SkillBase": "signalwire.core.skill_base",
    "SkillManager": "signalwire.core.skill_manager",
    "SkillRegistry": "signalwire.skills.registry",

    # -- prefab agents ----------------------------------------------------
    "BedrockAgent": "signalwire.agents.bedrock",

    # -- server -----------------------------------------------------------
    "AgentServer": "signalwire.agent_server",

    # -- security ---------------------------------------------------------
    "SessionManager": "signalwire.core.security.session_manager",

    # -- swml -------------------------------------------------------------
    # Document/Schema have no exact Python analog, so treat as port-only
    # via the native translation.
    # ``Service`` in C++ == Python's ``SWMLService``; rename at emit time.
    # Handled via CLASS_RENAME_MAP below, not via module mapping.

    # -- utils ------------------------------------------------------------
    # SchemaUtils + SchemaValidationError both live under
    # signalwire.utils.schema_utils per the canonical Python module layout.
    "SchemaUtils": "signalwire.utils.schema_utils",
    "SchemaValidationError": "signalwire.utils.schema_utils",

    # -- rest -------------------------------------------------------------
    "HttpClient": "signalwire.rest._base",
    "CrudResource": "signalwire.rest._base",
    "SignalWireRestError": "signalwire.rest._base",
    "RestClient": "signalwire.rest.client",
    # rest namespaces — C++ has 21 nested ::Namespace structs inside
    # RestClient. Map each to the Python canonical submodule path.
    "AddressesNamespace": "signalwire.rest.namespaces.addresses",
    "CallingNamespace": "signalwire.rest.namespaces.calling",
    "ChatNamespace": "signalwire.rest.namespaces.chat",
    "DatasphereNamespace": "signalwire.rest.namespaces.datasphere",
    "FabricNamespace": "signalwire.rest.namespaces.fabric",
    "ImportedNumbersNamespace": "signalwire.rest.namespaces.imported_numbers",
    "LogsNamespace": "signalwire.rest.namespaces.logs",
    "LookupNamespace": "signalwire.rest.namespaces.lookup",
    "MFANamespace": "signalwire.rest.namespaces.mfa",
    "NumberGroupsNamespace": "signalwire.rest.namespaces.number_groups",
    "PhoneNumbersNamespace": "signalwire.rest.namespaces.phone_numbers",
    "ProjectNamespace": "signalwire.rest.namespaces.project",
    "PubSubNamespace": "signalwire.rest.namespaces.pubsub",
    "QueuesNamespace": "signalwire.rest.namespaces.queues",
    "RecordingsNamespace": "signalwire.rest.namespaces.recordings",
    "RegistryNamespace": "signalwire.rest.namespaces.registry",
    "ShortCodesNamespace": "signalwire.rest.namespaces.short_codes",
    "SipProfileNamespace": "signalwire.rest.namespaces.sip_profile",
    "VerifiedCallersNamespace": "signalwire.rest.namespaces.verified_callers",
    "VideoNamespace": "signalwire.rest.namespaces.video",

    # -- rest sub-resources (Python parity) -------------------------------
    # Fabric sub-resources.
    "FabricAddresses": "signalwire.rest.namespaces.fabric",
    "FabricCallFlows": "signalwire.rest.namespaces.fabric",
    "FabricConferenceRooms": "signalwire.rest.namespaces.fabric",
    "FabricCxmlApplications": "signalwire.rest.namespaces.fabric",
    "FabricGenericResources": "signalwire.rest.namespaces.fabric",
    "FabricResource": "signalwire.rest.namespaces.fabric",
    "FabricResourcePUT": "signalwire.rest.namespaces.fabric",
    "FabricSubscribers": "signalwire.rest.namespaces.fabric",
    "FabricTokens": "signalwire.rest.namespaces.fabric",

    # Logs sub-resources.
    "LogsConferences": "signalwire.rest.namespaces.logs",
    "LogsFax": "signalwire.rest.namespaces.logs",
    "LogsMessages": "signalwire.rest.namespaces.logs",
    "LogsVoice": "signalwire.rest.namespaces.logs",

    # Registry sub-resources.
    "RegistryBrands": "signalwire.rest.namespaces.registry",
    "RegistryCampaigns": "signalwire.rest.namespaces.registry",
    "RegistryNumbers": "signalwire.rest.namespaces.registry",
    "RegistryOrders": "signalwire.rest.namespaces.registry",

    # Video sub-resources.
    "VideoConferences": "signalwire.rest.namespaces.video",
    "VideoConferenceTokens": "signalwire.rest.namespaces.video",
    "VideoRoomRecordings": "signalwire.rest.namespaces.video",
    "VideoRoomSessions": "signalwire.rest.namespaces.video",
    "VideoRoomTokens": "signalwire.rest.namespaces.video",
    "VideoRooms": "signalwire.rest.namespaces.video",
    "VideoStreams": "signalwire.rest.namespaces.video",

    # Pagination helper -- Python: signalwire.rest._pagination.PaginatedIterator.
    "PaginatedIterator": "signalwire.rest._pagination",

    # -- relay ------------------------------------------------------------
    "RelayClient": "signalwire.relay.client",
    "RelayError": "signalwire.relay.client",
    "Call": "signalwire.relay.call",
    "Message": "signalwire.relay.message",
    # Action / RelayEvent / CallEvent / MessageEvent / DialEvent /
    # ComponentEvent have no 1:1 Python analog -- port-only, use native
    # translation.

    # -- prefabs ----------------------------------------------------------
    "ConciergeAgent": "signalwire.prefabs.concierge",
    "FAQBotAgent": "signalwire.prefabs.faq_bot",
    "InfoGathererAgent": "signalwire.prefabs.info_gatherer",
    "ReceptionistAgent": "signalwire.prefabs.receptionist",
    "SurveyAgent": "signalwire.prefabs.survey",

    # -- logging ----------------------------------------------------------
    # ``Logger`` in signalwire::logging -> Python core.logging_config
    # has no matching class (Python uses module-level functions),
    # so let it fall through to native translation.
}


# -- Class rename map --------------------------------------------------------
# C++ names that should be emitted under a different class name to match the
# Python reference. These are cases where the C++ name is purely cosmetic (a
# legitimate simplification that still refers to the same concept).
CLASS_RENAME_MAP: dict[tuple[str, str], tuple[str, str]] = {
    # (source_ns, source_class) -> (target_module, target_class)
    ("signalwire::swml", "Service"): (
        "signalwire.core.swml_service", "SWMLService",
    ),
    # ``signalwire::pom::Section`` projects to ``signalwire.pom.pom.Section``;
    # disambiguates from ``signalwire::swml::Section`` (which falls through
    # to the native namespace translation as ``signalwire.swml.section``).
    ("signalwire::pom", "Section"): (
        "signalwire.pom.pom", "Section",
    ),
    # C++ uses ``XxxNamespace`` for all REST namespaces; Python uses
    # ``XxxResource`` for single-resource namespaces and ``XxxNamespace``
    # for multi-resource ones. Map the single-resource cases.
    ("signalwire::rest", "AddressesNamespace"): (
        "signalwire.rest.namespaces.addresses", "AddressesResource",
    ),
    ("signalwire::rest", "ChatNamespace"): (
        "signalwire.rest.namespaces.chat", "ChatResource",
    ),
    ("signalwire::rest", "ImportedNumbersNamespace"): (
        "signalwire.rest.namespaces.imported_numbers", "ImportedNumbersResource",
    ),
    ("signalwire::rest", "LookupNamespace"): (
        "signalwire.rest.namespaces.lookup", "LookupResource",
    ),
    ("signalwire::rest", "MFANamespace"): (
        "signalwire.rest.namespaces.mfa", "MfaResource",
    ),
    ("signalwire::rest", "NumberGroupsNamespace"): (
        "signalwire.rest.namespaces.number_groups", "NumberGroupsResource",
    ),
    ("signalwire::rest", "PhoneNumbersNamespace"): (
        "signalwire.rest.namespaces.phone_numbers", "PhoneNumbersResource",
    ),
    ("signalwire::rest", "PubSubNamespace"): (
        "signalwire.rest.namespaces.pubsub", "PubSubResource",
    ),
    ("signalwire::rest", "QueuesNamespace"): (
        "signalwire.rest.namespaces.queues", "QueuesResource",
    ),
    ("signalwire::rest", "RecordingsNamespace"): (
        "signalwire.rest.namespaces.recordings", "RecordingsResource",
    ),
    ("signalwire::rest", "ShortCodesNamespace"): (
        "signalwire.rest.namespaces.short_codes", "ShortCodesResource",
    ),
    ("signalwire::rest", "SipProfileNamespace"): (
        "signalwire.rest.namespaces.sip_profile", "SipProfileResource",
    ),
    ("signalwire::rest", "VerifiedCallersNamespace"): (
        "signalwire.rest.namespaces.verified_callers", "VerifiedCallersResource",
    ),
    # ProjectTokens is exposed as a nested class on the project namespace.
    ("signalwire::rest", "ProjectTokens"): (
        "signalwire.rest.namespaces.project", "ProjectTokens",
    ),
    # DatasphereDocuments is the typed wrapper around the documents
    # CrudResource; Python exposes it as DatasphereDocuments inside
    # namespaces/datasphere.py.
    ("signalwire::rest", "DatasphereDocuments"): (
        "signalwire.rest.namespaces.datasphere", "DatasphereDocuments",
    ),
    # Fabric: C++ uses ``FabricXxx`` names for sub-resources; Python uses
    # ``XxxResource`` (or shorter names). Map at emit time so the audit
    # treats them as the same class.
    ("signalwire::rest", "FabricCallFlows"): (
        "signalwire.rest.namespaces.fabric", "CallFlowsResource",
    ),
    ("signalwire::rest", "FabricConferenceRooms"): (
        "signalwire.rest.namespaces.fabric", "ConferenceRoomsResource",
    ),
    ("signalwire::rest", "FabricCxmlApplications"): (
        "signalwire.rest.namespaces.fabric", "CxmlApplicationsResource",
    ),
    ("signalwire::rest", "FabricGenericResources"): (
        "signalwire.rest.namespaces.fabric", "GenericResources",
    ),
    ("signalwire::rest", "FabricSubscribers"): (
        "signalwire.rest.namespaces.fabric", "SubscribersResource",
    ),
    # Logs: Python names are ``MessageLogs`` / ``VoiceLogs`` etc; C++ uses
    # ``LogsMessages`` / ``LogsVoice`` for namespace-prefix consistency.
    ("signalwire::rest", "LogsMessages"): (
        "signalwire.rest.namespaces.logs", "MessageLogs",
    ),
    ("signalwire::rest", "LogsVoice"): (
        "signalwire.rest.namespaces.logs", "VoiceLogs",
    ),
    ("signalwire::rest", "LogsFax"): (
        "signalwire.rest.namespaces.logs", "FaxLogs",
    ),
    ("signalwire::rest", "LogsConferences"): (
        "signalwire.rest.namespaces.logs", "ConferenceLogs",
    ),

    # -- Callback typedef projection -------------------------------------
    # C++ uses ``using XxxHandler = std::function<...>`` aliases for
    # callbacks. libclang emits the typedef name (``InboundCallHandler``
    # etc.) as the parameter type. Python exposes the same callbacks
    # under canonical class-style aliases that live in their owning
    # module — ``CallHandler`` in ``signalwire.relay.client`` etc.
    # Map at emit time so the diff treats handler signatures as the
    # same callable contract regardless of the C++ typedef name.
    ("signalwire::relay", "InboundCallHandler"): (
        "signalwire.relay.client", "CallHandler",
    ),
    ("signalwire::relay", "InboundMessageHandler"): (
        "signalwire.relay.client", "MessageHandler",
    ),
}


# -- Generated REST surface-map projection (item A/B adoption) ----------------
# The REST resource + container surface is now GENERATED
# (include/signalwire/rest/namespaces/generated/*.hpp), all under the C++
# namespace ``signalwire::rest::generated``. The generator emits
# ``generated_surface_map.json`` (ruby-identical) mapping each generated class
# name to its canonical Python module:
#   - resource classes  -> signalwire.rest.namespaces.<ns>_resources_generated
#   - namespace containers -> signalwire.rest.namespaces._client_tree_generated
# The generated class NAME is kept verbatim (L2: AiAgents/SipEndpoints/... equal
# the Python class names). We register each as a CLASS_RENAME_MAP entry keyed on
# (``signalwire::rest::generated``, ClassName) so it takes precedence over the
# name-only CLASS_MODULE_MAP (which still carries the now-deleted hand REST
# classes — harmless, never hit for the generated namespace). Abort-loud on a
# missing map so a new generated resource can't silently fall through to the
# native-namespace translation (signalwire.rest.generated.<snake>) and drift.
def _load_generated_surface_map() -> dict[str, str]:
    here = Path(__file__).resolve().parent
    smap = (here.parent / "include" / "signalwire" / "rest" / "namespaces"
            / "generated" / "generated_surface_map.json")
    if not smap.is_file():
        return {}
    import json as _json
    return _json.loads(smap.read_text())


_GENERATED_NS = "signalwire::rest::generated"
for _cls, _mod in _load_generated_surface_map().items():
    CLASS_RENAME_MAP[(_GENERATED_NS, _cls)] = (_mod, _cls)

# The generated resource layer's hand base hierarchy (base_resource.hpp) lives
# in the same ``signalwire::rest::generated`` namespace but maps to Python's
# ``signalwire.rest._base`` (BaseResource / ReadResource / CrudResource /
# CrudWithAddresses). C++ folds CrudWithAddresses into FabricResource (Python's
# FabricResource == CrudResource + CrudWithAddresses.list_addresses), so route
# the C++ ``FabricResource`` to the oracle's ``CrudWithAddresses`` (its
# list_addresses home). ResourceTree is a port-only composition holder (no
# Python analog — the reference client uses dynamic __getattr__), documented in
# PORT_ADDITIONS.
for _bc_cpp, _bc_py in (
    ("BaseResource", "BaseResource"),
    ("ReadResource", "ReadResource"),
    ("CrudResource", "CrudResource"),
    ("FabricResource", "CrudWithAddresses"),
):
    CLASS_RENAME_MAP[(_GENERATED_NS, _bc_cpp)] = ("signalwire.rest._base", _bc_py)


# -- Generated wire-TYPE / read-side-payload surface (item D/H) ----------------
# The <ns>_types_generated wire types (generate_rest.py), SWML-verbs
# (generate_swml_verbs.py), relay-protocol (generate_relay_protocol.py), and
# SWAIG payload/action (generate_swaig_payloads.py) modules are method-less C++
# data structs, each in its OWN namespace so it routes by PATH (not by class
# name — these names recur across modules and collide with SDK class names, so a
# name-keyed lookup would misroute them). The reference records each as a bare
# method-less class on the SURFACE; the diff tool folds the cross-module
# duplicates by leaf name (``gen-type``). A struct with zero methods is normally
# never registered by the regex walker (it only emits classes that have a public
# method), so ``parse_header`` also registers a class the moment its opener is
# seen inside one of these namespaces (see GENERATED_TYPE_NS_PREFIXES use there).
#
# REST types nest under signalwire::rest::generated::types::<Ns>; the per-Ns leaf
# maps to signalwire.rest.namespaces.<ns_key>_types_generated. The other four
# groups map their whole namespace prefix to the flat reference module.
_TYPES_NS_PREFIX = "signalwire::rest::generated::types::"
_TYPES_NS_KEY = {
    "RelayRest": "relay_rest", "Fabric": "fabric", "Calling": "calling",
    "Video": "video", "Datasphere": "datasphere", "Logs": "logs",
    "Message": "message", "Messages": "messages", "Voice": "voice", "Fax": "fax", "Project": "project",
    "Projects": "projects", "Chat": "chat", "PubSub": "pubsub",
    "SwmlWebhooks": "swml_webhooks",
}
GENERATED_PAYLOAD_NS = {
    "signalwire::core::swml_verbs_generated": "signalwire.core.swml_verbs_generated",
    "signalwire::relay::protocol_types_generated": "signalwire.relay.protocol_types_generated",
    "signalwire::core::post_prompt_generated": "signalwire.core.post_prompt_generated",
    "signalwire::core::swaig_request_generated": "signalwire.core.swaig_request_generated",
    "signalwire::core::swaig_actions_generated": "signalwire.core.swaig_actions_generated",
}
# Namespace-path prefixes whose classes are generated method-less types (used by
# parse_header to force-register a zero-method struct so it surfaces).
GENERATED_TYPE_NS_PREFIXES = (_TYPES_NS_PREFIX.rstrip(":"),) + tuple(GENERATED_PAYLOAD_NS)


def generated_type_module(ns_path: str) -> str | None:
    """If ``ns_path`` is one of the generated wire-type / payload namespaces,
    return the canonical Python reference module (routed by PATH). Else None."""
    if ns_path in GENERATED_PAYLOAD_NS:
        return GENERATED_PAYLOAD_NS[ns_path]
    if ns_path.startswith(_TYPES_NS_PREFIX):
        seg = ns_path[len(_TYPES_NS_PREFIX):].split("::", 1)[0]
        key = _TYPES_NS_KEY.get(seg)
        if key is None:
            raise SystemExit(
                f"enumerate_surface.py: generated types namespace {ns_path!r} has "
                f"unknown segment {seg!r} (add to _TYPES_NS_KEY)")
        return f"signalwire.rest.namespaces.{key}_types_generated"
    return None


def _is_generated_type_ns(ns_path: str) -> bool:
    return ns_path in GENERATED_PAYLOAD_NS or ns_path.startswith(_TYPES_NS_PREFIX)


# C++ typedef aliases for std::function callables that have no class
# counterpart on the Python side — Python uses the bare ``typing.Callable``
# for these. Translate the C++ typedef name to the canonical
# ``class:Callable`` form so the cross-language diff treats them as the
# same callable contract.
#
# Listed by C++ typedef NAME (libclang emits the typedef rather than
# expanding to the full ``std::function<...>`` shape, so name-based
# matching is sufficient and keeps the rule decoupled from the typedef's
# specific signature).
CALLBACK_TYPEDEFS_AS_CALLABLE: set[str] = {
    "DebugEventCallback",
    "DynamicConfigCallback",
    "SummaryCallback",
}


# -- Method rename map -------------------------------------------------------
# C++ uses ``delete_`` and similar trailing-underscore method names because
# ``delete`` is a reserved keyword. Python uses the unsuffixed names. Map
# back so the diff lines up.
_METHOD_RENAMES: dict[str, str] = {
    "delete_": "delete",
    # HttpClient spells the DELETE verb ``del`` — ``delete`` is a C++ keyword;
    # the Python reference records ``delete``.
    "del": "delete",
    # BedrockAgent's C++ ``repr()`` is Python's ``__repr__`` (C++ has no dunder
    # convention; the method is the same string-representation contract).
    "repr": "__repr__",
    # AgentServer spells the Python ``register`` verb ``register_`` (``register``
    # is not reserved in C++, but the trailing underscore disambiguates it from
    # the many ``register_*`` methods and matches the port's escape convention).
    "register_": "register",
}


# -- Free-function rename map ------------------------------------------------
# Maps a C++ (namespace, function-name) to the Python (module-path,
# function-name) the reference inventory exposes at module level. Use this
# when ``native_ns_to_module + camel/Pascal`` doesn't land on the right
# Python module — for example C++ ``signalwire::security`` collapses
# both webhook_validator and session_manager into one namespace, but
# Python keeps them in separate modules under
# ``signalwire.core.security.<file>``.
FREE_FUNCTION_RENAMES: dict[tuple[str, str], tuple[str, str]] = {
    # Webhook signature validation (porting-sdk/webhooks.md). C++ uses
    # PascalCase per its naming convention; Python uses snake_case.
    ("signalwire::security", "ValidateWebhookSignature"): (
        "signalwire.core.security.webhook_validator", "validate_webhook_signature",
    ),
    ("signalwire::security", "ValidateRequest"): (
        "signalwire.core.security.webhook_validator", "validate_request",
    ),
    # The framework-free webhook-validation decision core (porting-sdk
    # webhooks.md + HIDDEN_SURFACE_AUDIT Pass 1). Python exposes it as a
    # module-level ``validate`` under ``webhook_middleware``; C++ ships the
    # same decomposed ``(method,url,headers,body,signing_key) ->
    # optional<(status,headers,body)>`` core as a free function. The
    # cpp-httplib ``WrapWithSignatureValidation`` wrapper stays a
    # PORT_ADDITION idiom on top of this.
    ("signalwire::security", "Validate"): (
        "signalwire.core.security.webhook_middleware", "validate",
    ),
    # Standalone security-hygiene utils (security_utils.py). C++ groups them in
    # a nested ``signalwire::security::security_utils`` namespace with PascalCase
    # names; Python keeps them as module-level snake_case functions under
    # ``signalwire.core.security.security_utils``.
    ("signalwire::security::security_utils", "FilterSensitiveHeaders"): (
        "signalwire.core.security.security_utils", "filter_sensitive_headers",
    ),
    ("signalwire::security::security_utils", "RedactUrl"): (
        "signalwire.core.security.security_utils", "redact_url",
    ),
    ("signalwire::security::security_utils", "IsValidHostname"): (
        "signalwire.core.security.security_utils", "is_valid_hostname",
    ),
    # SWAIG schema inference (type_inference.py). C++ groups these in a nested
    # ``signalwire::swaig::type_inference`` namespace (snake_case names);
    # Python keeps them module-level under
    # ``signalwire.core.agent.tools.type_inference``.
    ("signalwire::swaig::type_inference", "infer_schema"): (
        "signalwire.core.agent.tools.type_inference", "infer_schema",
    ),
    ("signalwire::swaig::type_inference", "create_typed_handler_wrapper"): (
        "signalwire.core.agent.tools.type_inference", "create_typed_handler_wrapper",
    ),
}


# Reserved identifiers that must never be emitted as methods.
SKIP_METHOD_NAMES: set[str] = {
    # C++ constructs that can superficially look like methods
    "operator",
    "using",
    "typedef",
    "friend",
    "template",
    "return",
    "if", "else", "for", "while", "do", "switch", "case",
}


# Classes whose surface we deliberately don't enumerate because they're not
# part of the SDK's public contract (internal helper types, event-parsing
# POD structs with only static from_X methods that are port-specific, etc.).
#
# We DO enumerate these but they will show up as port additions since the
# Python side doesn't have them. The diff catches them in PORT_ADDITIONS.md
# which is the correct treatment -- they're deliberate C++ additions.
# So this set is intentionally empty; nothing is silently dropped.
CLASSES_TO_SKIP: set[str] = set()


# -- Mixin projection -------------------------------------------------------
# Python's AgentBase collapses multiple inheritance across 9 mixin classes.
# The C++ port flattens all those methods onto AgentBase directly. To make
# the diff line up, we *project* each mixin class into port_surface.json by
# picking the matching methods off AgentBase.
#
# This table mirrors ``signalwire.core.mixins.*`` in ``python_surface.json``.
# If Python adds/removes/renames a mixin method, update this table and the
# nightly diff will tell you.
MIXIN_PROJECTIONS: dict[tuple[str, str], list[str]] = {
    # (module_path, class_name) -> list of method names to copy from
    # the C++ AgentBase class if present there.
    ("signalwire.core.mixins.ai_config_mixin", "AIConfigMixin"): [
        "add_function_include", "add_hint", "add_hints", "add_internal_filler",
        "add_language", "add_mcp_server", "add_pattern_hint", "add_pronunciation",
        "enable_debug_events", "enable_mcp_server",
        "get_language_params",
        "set_function_includes", "set_global_data", "set_internal_fillers",
        "set_language_params",
        "set_languages", "set_multilingual", "set_native_functions", "set_param", "set_params",
        "set_post_prompt_llm_params", "set_prompt_llm_params",
        "set_pronunciations", "update_global_data",
    ],
    ("signalwire.core.mixins.auth_mixin", "AuthMixin"): [
        # These two AuthMixin methods are implementation-detail protected
        # helpers in C++ (validate_auth) that aren't part of the public C++
        # surface. Tracked as a PORT_OMISSIONS exemption, not a projection.
    ],
    ("signalwire.core.mixins.mcp_server_mixin", "MCPServerMixin"): [
        # Empty in Python -- class exists as a marker only.
    ],
    ("signalwire.core.mixins.prompt_mixin", "PromptMixin"): [
        "contexts", "define_contexts", "get_post_prompt", "get_prompt",
        "prompt_add_section", "prompt_add_subsection", "prompt_add_to_section",
        "prompt_has_section", "reset_contexts", "set_post_prompt",
        "set_prompt_pom", "set_prompt_text",
    ],
    # Python additionally extracted a ``PromptManager`` class that
    # PromptMixin delegates to. The user-facing surface is identical
    # (``agent.prompt_manager.X`` ≡ ``agent.X``). Project the same set of
    # AgentBase methods to PromptManager so the cross-language audit
    # treats both paths as covered.
    ("signalwire.core.agent.prompt.manager", "PromptManager"): [
        "__init__", "define_contexts", "get_contexts", "get_post_prompt", "get_prompt",
        "get_raw_prompt",
        "prompt_add_section", "prompt_add_subsection", "prompt_add_to_section",
        "prompt_has_section", "set_post_prompt", "set_prompt_pom",
        "set_prompt_text",
    ],
    ("signalwire.core.mixins.serverless_mixin", "ServerlessMixin"): [
        # AgentBase::handle_serverless_request delegates to the per-platform
        # dispatchers in signalwire::utils. Projected here so the ServerlessMixin
        # method matches (surface + signature).
        "handle_serverless_request",
    ],
    ("signalwire.core.mixins.skill_mixin", "SkillMixin"): [
        "add_skill", "has_skill", "list_skills", "remove_skill",
    ],
    ("signalwire.core.mixins.state_mixin", "StateMixin"): [
        "validate_tool_token",
    ],
    ("signalwire.core.mixins.tool_mixin", "ToolMixin"): [
        "define_tool", "define_tools", "on_function_call", "register_swaig_function",
    ],
    ("signalwire.core.agent.tools.registry", "ToolRegistry"): [
        "__init__", "define_tool", "register_swaig_function",
        "has_function", "get_function", "get_all_functions",
        "remove_function",
    ],
    ("signalwire.core.mixins.auth_mixin", "AuthMixin"): [
        "validate_basic_auth", "get_basic_auth_credentials",
    ],
    ("signalwire.core.mixins.web_mixin", "WebMixin"): [
        "as_router", "enable_debug_routes", "manual_set_proxy_url", "run",
        "serve", "set_dynamic_config_callback", "on_request", "on_swml_request",
        "register_routing_callback", "setup_graceful_shutdown",
    ],
}


# -- Built-in skill projection ----------------------------------------------
# Python ships one module per built-in skill (``signalwire.skills.<name>.skill``)
# each exporting a ``<Name>Skill`` class. The C++ port implements each skill as a
# class in ``src/skills/builtin/<name>.cpp`` (an implementation file, NOT a header),
# registered at static-init via ``REGISTER_SKILL``. The header-only surface walker
# never sees them, so — exactly like the AgentBase mixin projection — we project
# each C++ skill class into its Python-canonical module.
#
# The method set is the Python-recorded surface for that skill, intersected at
# emit time with the methods the C++ class actually has: a method the C++ class
# defines itself, OR one it inherits from ``SkillBase`` (the shared virtual base
# — ``setup``/``register_tools``/``get_hints``/``get_global_data``/
# ``get_prompt_sections``/``get_parameter_schema``/``get_instance_key``/
# ``cleanup`` — every skill genuinely has these), OR ``__init__`` (the ctor
# always exists). We never project a method the C++ class lacks (that would be
# inventing surface); those are recorded in PORT_OMISSIONS.
#
# ``(cpp_class) -> (python_module, python_class)``. C++ class names that differ
# from Python only by casing/reserved-word idiom (``DatasphereSkill`` ->
# ``DataSphereSkill``, ``SwmlTransferSkill`` -> ``SWMLTransferSkill``) are the
# adapter rename, not an omission.
# -- RELAY call-action control projection -----------------------------------
# The Python oracle no longer ships the abstract Stoppable/Pausable/Volume
# mixin bases as cross-port symbols; it PROJECTS the control methods directly
# onto each CONCRETE action (PlayAction: stop/pause/resume/volume; RecordAction:
# stop/pause/resume; CollectAction: +volume+start_input_timers; the rest: stop).
# The C++ port flattens every control onto a single ``signalwire::relay::Action``
# class, and each concrete subclass inherits them (macro
# ``SIGNALWIRE_RELAY_ACTION_SUBCLASS``). So we project the oracle's per-concrete-
# action control set from the methods the C++ ``Action`` genuinely defines — the
# real user-facing control surface, matching the reference (NOT an addition, NOT
# an omission). ``python_concrete_class -> control methods it exposes``.
RELAY_ACTION_CONTROL_METHODS: dict[str, list[str]] = {
    "PlayAction": ["stop", "pause", "resume", "volume"],
    "RecordAction": ["stop", "pause", "resume"],
    "CollectAction": ["stop", "pause", "resume", "volume", "start_input_timers"],
    "StandaloneCollectAction": ["stop", "start_input_timers"],
    "DetectAction": ["stop"],
    "FaxAction": ["stop"],
    "PayAction": ["stop"],
    "StreamAction": ["stop"],
    "TapAction": ["stop"],
    "TranscribeAction": ["stop"],
    "AIAction": ["stop"],
}

SKILL_SOURCE_DIR = "src/skills/builtin"
# Methods that live on the shared C++ ``SkillBase`` (so every concrete skill
# inherits them and they are legitimately part of that skill's callable surface).
_SKILL_BASE_METHODS = {
    "setup", "register_tools", "get_hints", "get_global_data",
    "get_prompt_sections", "get_parameter_schema", "get_instance_key",
    "cleanup", "get_datamap_functions", "skill_name", "skill_description",
}
SKILL_PROJECTIONS: dict[str, tuple[str, str, list[str]]] = {
    # cpp_class: (python_module, python_class, python_recorded_methods)
    "ApiNinjasTriviaSkill": ("signalwire.skills.api_ninjas_trivia.skill", "ApiNinjasTriviaSkill",
        ["__init__", "get_instance_key", "get_parameter_schema", "get_tools", "register_tools", "setup"]),
    "ClaudeSkillsSkill": ("signalwire.skills.claude_skills.skill", "ClaudeSkillsSkill",
        ["get_hints", "get_instance_key", "get_parameter_schema", "register_tools", "setup"]),
    "DatasphereSkill": ("signalwire.skills.datasphere.skill", "DataSphereSkill",
        ["cleanup", "get_global_data", "get_hints", "get_instance_key", "get_parameter_schema",
         "get_prompt_sections", "register_tools", "setup"]),
    "DatasphereServerlessSkill": ("signalwire.skills.datasphere_serverless.skill", "DataSphereServerlessSkill",
        ["get_global_data", "get_hints", "get_instance_key", "get_parameter_schema",
         "get_prompt_sections", "register_tools", "setup"]),
    "DateTimeSkill": ("signalwire.skills.datetime.skill", "DateTimeSkill",
        ["get_hints", "get_parameter_schema", "get_prompt_sections", "register_tools", "setup"]),
    "GoogleMapsSkill": ("signalwire.skills.google_maps.skill", "GoogleMapsSkill",
        ["get_hints", "get_parameter_schema", "get_prompt_sections", "register_tools", "setup"]),
    "InfoGathererSkill": ("signalwire.skills.info_gatherer.skill", "InfoGathererSkill",
        ["get_global_data", "get_instance_key", "get_parameter_schema", "register_tools", "setup"]),
    "JokeSkill": ("signalwire.skills.joke.skill", "JokeSkill",
        ["get_global_data", "get_hints", "get_parameter_schema", "get_prompt_sections", "register_tools", "setup"]),
    "MathSkill": ("signalwire.skills.math.skill", "MathSkill",
        ["get_hints", "get_parameter_schema", "get_prompt_sections", "register_tools", "setup"]),
    "NativeVectorSearchSkill": ("signalwire.skills.native_vector_search.skill", "NativeVectorSearchSkill",
        ["cleanup", "get_global_data", "get_hints", "get_instance_key", "get_parameter_schema",
         "get_prompt_sections", "register_tools", "setup"]),
    "PlayBackgroundFileSkill": ("signalwire.skills.play_background_file.skill", "PlayBackgroundFileSkill",
        ["__init__", "get_instance_key", "get_parameter_schema", "get_tools", "register_tools", "setup"]),
    "SpiderSkill": ("signalwire.skills.spider.skill", "SpiderSkill",
        ["__init__", "cleanup", "get_hints", "get_instance_key", "get_parameter_schema", "register_tools", "setup"]),
    "SwmlTransferSkill": ("signalwire.skills.swml_transfer.skill", "SWMLTransferSkill",
        ["get_hints", "get_instance_key", "get_parameter_schema", "get_prompt_sections", "register_tools", "setup"]),
    "WeatherApiSkill": ("signalwire.skills.weather_api.skill", "WeatherApiSkill",
        ["__init__", "get_parameter_schema", "get_tools", "register_tools", "setup"]),
    "WebSearchSkill": ("signalwire.skills.web_search.skill", "WebSearchSkill",
        ["get_global_data", "get_hints", "get_instance_key", "get_parameter_schema",
         "get_prompt_sections", "register_tools", "setup"]),
    "WikipediaSearchSkill": ("signalwire.skills.wikipedia_search.skill", "WikipediaSearchSkill",
        ["get_hints", "get_parameter_schema", "get_prompt_sections", "register_tools", "search_wiki", "setup"]),
}


# -- Serialization-method alias projection ----------------------------------
# C++ names its object->JSON serializer ``to_json`` (the nlohmann/json + C++
# convention); the Python reference records the same method as ``to_dict`` on
# these classes (a Python object returns a dict, which the SDK then serializes).
# Same method, port-idiom spelling. Where the C++ class genuinely has ``to_json``
# we also surface ``to_dict`` so the membership matches — never inventing it (only
# projected when ``to_json`` is actually present on that class). This is an alias
# projection, not a global rename: classes like PomBuilder/PromptObjectModel carry
# BOTH names in both languages and must not collapse.
_TO_DICT_ALIAS_CLASSES: set[tuple[str, str]] = {
    ("signalwire.core.contexts", "Context"),
    ("signalwire.core.contexts", "ContextBuilder"),
    ("signalwire.core.contexts", "GatherInfo"),
    ("signalwire.core.contexts", "GatherQuestion"),
    ("signalwire.core.contexts", "Step"),
    ("signalwire.core.function_result", "FunctionResult"),
}


def _project_to_dict_aliases(modules: dict) -> None:
    for (mod, cls) in _TO_DICT_ALIAS_CLASSES:
        methods = modules.get(mod, {}).get("classes", {}).get(cls)
        if methods is not None and "to_json" in methods and "to_dict" not in methods:
            methods.append("to_dict")
            modules[mod]["classes"][cls] = sorted(set(methods))


# -- Module-level free-function projection ----------------------------------
# The regex header walker only collects methods declared INSIDE a class; it does
# not emit namespace-level free functions. Python exposes a few module-level
# functions the C++ port implements as free functions in a namespace; project
# them by (python_module -> [function names]) when the C++ header genuinely
# defines them (verified by a source grep, never invented).
# ``python_module: [(cpp_header_relpath, cpp_func_name, python_func_name)]``
MODULE_FUNCTION_PROJECTIONS: dict[str, list[tuple[str, str, str]]] = {
    # parse_event: the RELAY typed-event dispatcher (typed_events.hpp).
    "signalwire.relay.event": [
        ("include/signalwire/relay/typed_events.hpp", "parse_event", "parse_event"),
    ],
    # Serverless-mode detection free function.
    "signalwire.utils": [
        ("include/signalwire/utils/serverless.hpp", "is_serverless_mode", "is_serverless_mode"),
    ],
    # URL validation free function.
    "signalwire.utils.url_validator": [
        ("include/signalwire/utils/url_validator.hpp", "validate_url", "validate_url"),
    ],
    # Security-hygiene utilities (security_utils.py). C++ groups them in a
    # nested ``signalwire::security::security_utils`` namespace with PascalCase
    # names; Python keeps them module-level under
    # ``signalwire.core.security.security_utils``. Grep the PascalCase C++ name;
    # emit the Python snake_case name.
    "signalwire.core.security.security_utils": [
        ("include/signalwire/security/security_utils.hpp",
         "FilterSensitiveHeaders", "filter_sensitive_headers"),
        ("include/signalwire/security/security_utils.hpp", "RedactUrl", "redact_url"),
        ("include/signalwire/security/security_utils.hpp",
         "IsValidHostname", "is_valid_hostname"),
    ],
    # Inbound-webhook signature validation (webhooks.md). C++ exposes these as
    # PascalCase free functions in ``signalwire::security``; Python keeps them
    # module-level under ``signalwire.core.security.webhook_validator``.
    "signalwire.core.security.webhook_validator": [
        ("include/signalwire/security/webhook_validator.hpp",
         "ValidateWebhookSignature", "validate_webhook_signature"),
        ("include/signalwire/security/webhook_validator.hpp",
         "ValidateRequest", "validate_request"),
    ],
    # Framework-free webhook-validation decision core (webhooks.md +
    # HIDDEN_SURFACE_AUDIT Pass 1). C++ ships it as the ``Validate`` free
    # function in ``signalwire::security`` (webhook_validator.hpp); Python
    # exposes it module-level as ``webhook_middleware.validate``. The
    # cpp-httplib ``WrapWithSignatureValidation`` wrapper stays a
    # PORT_ADDITION idiom on top of this. The bare ``Validate(`` grep does
    # NOT match ``ValidateWebhookSignature(`` / ``ValidateRequest(`` (those
    # have no word-boundary before ``(``), so this surfaces only the core.
    "signalwire.core.security.webhook_middleware": [
        ("include/signalwire/security/webhook_validator.hpp",
         "Validate", "validate"),
    ],
    # Top-level ``signalwire/__init__.py`` package helpers. C++ implements them
    # as free functions in ``namespace signalwire`` (src/signalwire.cpp,
    # declared in include/signalwire/signalwire.hpp). ``RestClient`` keeps its
    # PascalCase spelling on both sides (it is a factory named for the class).
    "signalwire": [
        ("include/signalwire/signalwire.hpp", "RestClient", "RestClient"),
        ("include/signalwire/signalwire.hpp", "register_skill", "register_skill"),
        ("include/signalwire/signalwire.hpp", "add_skill_directory", "add_skill_directory"),
        ("include/signalwire/signalwire.hpp", "list_skills_with_params", "list_skills_with_params"),
        ("include/signalwire/signalwire.hpp", "list_skills", "list_skills"),
    ],
    # SWAIG schema-inference module-level helpers (type_inference.py). C++
    # implements them as free functions in
    # ``signalwire::swaig::type_inference`` (same snake_case names as Python).
    # Python reflects a callable's type hints; C++ has no lambda reflection, so
    # infer_schema derives the schema from the typed ``ParameterSchema``
    # params-builder — same output tuple, idiomatic input.
    "signalwire.core.agent.tools.type_inference": [
        ("include/signalwire/swaig/type_inference.hpp", "infer_schema", "infer_schema"),
        ("include/signalwire/swaig/type_inference.hpp",
         "create_typed_handler_wrapper", "create_typed_handler_wrapper"),
    ],
    # Logging-config module-level helpers. C++ implements them as free functions
    # in ``signalwire::core::logging_config`` (same snake_case names as Python).
    "signalwire.core.logging_config": [
        ("include/signalwire/core/logging_config.hpp",
         "configure_logging", "configure_logging"),
        ("include/signalwire/core/logging_config.hpp", "get_logger", "get_logger"),
        ("include/signalwire/core/logging_config.hpp",
         "reset_logging_configuration", "reset_logging_configuration"),
        ("include/signalwire/core/logging_config.hpp",
         "strip_control_chars", "strip_control_chars"),
        ("include/signalwire/core/logging_config.hpp",
         "get_execution_mode", "get_execution_mode"),
    ],
}


def _project_module_functions(modules: dict, repo: Path) -> None:
    for mod, funcs in MODULE_FUNCTION_PROJECTIONS.items():
        for relpath, cpp_name, py_name in funcs:
            hdr = repo / relpath
            if not hdr.is_file():
                continue
            text = hdr.read_text(encoding="utf-8")
            # Require the C++ header to actually define the function (return-type
            # + name + '(') so we never surface a function the port lacks.
            if not re.search(rf"\b{re.escape(cpp_name)}\s*\(", text):
                continue
            entry = modules.setdefault(mod, {"classes": {}, "functions": []})
            if py_name not in entry["functions"]:
                entry["functions"].append(py_name)
                entry["functions"].sort()


def _scan_skill_methods(repo: Path) -> dict[str, set[str]]:
    """Return {cpp_skill_class: {method names it DEFINES}} by scanning the
    built-in skill implementation files. A skill class is only projected if its
    ``.cpp`` is actually present (fail-honest: a deleted skill drops out)."""
    src = repo / SKILL_SOURCE_DIR
    defined: dict[str, set[str]] = {}
    if not src.is_dir():
        return defined
    class_re = re.compile(r"\bclass\s+([A-Za-z_]\w*Skill)\b")
    # method def: `<name>(...) override` or `<name>(...) const override` or `<name>(...) {`
    method_re = re.compile(r"\b([a-z_][a-z0-9_]*)\s*\([^;{]*\)\s*(?:const\s*)?(?:override|noexcept|\{)")
    for cpp in sorted(src.glob("*.cpp")):
        text = strip_block_comments(cpp.read_text(encoding="utf-8"))
        # Drop ``//`` line comments too, so a method name mentioned in a doc
        # comment doesn't perturb the regex scan.
        text = "\n".join(strip_line_comments(ln) for ln in text.splitlines())
        classes = class_re.findall(text)
        methods = set(method_re.findall(text))
        for cls in classes:
            defined.setdefault(cls, set()).update(methods)
    return defined


def _project_builtin_skills(modules: dict, repo: Path) -> None:
    """Project each built-in skill class into its Python-canonical module with the
    intersection of Python's recorded methods and the methods the C++ class
    genuinely has (own-defined | SkillBase-inherited | ctor)."""
    defined = _scan_skill_methods(repo)
    for cpp_cls, (mod, py_cls, py_methods) in SKILL_PROJECTIONS.items():
        if cpp_cls not in defined:
            continue  # skill not implemented in this tree — don't invent it
        own = defined[cpp_cls]
        present = []
        for m in py_methods:
            if m == "__init__" or m in own or m in _SKILL_BASE_METHODS:
                present.append(m)
        mod_entry = modules.setdefault(mod, {"classes": {}, "functions": []})
        mod_entry["classes"][py_cls] = sorted(set(present))


# ---------------------------------------------------------------------------
# Parser
# ---------------------------------------------------------------------------

# Match "namespace foo {", "namespace foo::bar {", or "namespace foo { namespace bar {"
NAMESPACE_RE = re.compile(r"^\s*namespace\s+([A-Za-z_][\w:]*)\s*\{")
# Unnamed namespace
ANON_NAMESPACE_RE = re.compile(r"^\s*namespace\s*\{")
# class Foo { / class Foo : public Bar {  -- exclude forward declarations
CLASS_RE = re.compile(
    r"^\s*(?:class|struct)\s+([A-Z][A-Za-z0-9_]*)\b(?:\s*(?:final\s*)?:\s*[^{;]+)?\s*\{"
)
# Forward declarations like "class Foo;" or "class foo::Bar;" inside scopes
FORWARD_DECL_RE = re.compile(r"^\s*(?:class|struct)\s+[^{;]*;\s*$")
# Visibility specifiers: "public:", "private:", "protected:"
VISIBILITY_RE = re.compile(r"^\s*(public|private|protected)\s*:\s*$")
# A plausible method line. This is intentionally loose -- we post-filter.
# Captures: return/qualifiers, then name, then open-paren.
# Matches things like:
#   AgentBase& set_name(const std::string& n) { ... }
#   static const std::set<std::string>& supported_internal_filler_names();
#   explicit AgentBase(const std::string& name = "agent", ...);
#   ~AgentBase();
#   Action answer();
#   json to_json() const { ... }
#
# The key signal: an identifier followed by `(` before `;` or `{` on the
# logical line, and the line isn't obviously a local variable or type alias.
METHOD_RE = re.compile(
    # Leading whitespace, optional modifiers
    r"^\s*"
    # Optional specifiers / attributes. Non-greedy blob up to the name.
    # We allow any text here because the types get arbitrarily complex
    # (templates, nested types, qualifiers). We require the name+paren
    # at the end to anchor.
    r"(?P<prefix>(?:[A-Za-z_][\w:<>,\s*&\[\]]*?\s+)?)"
    # The method / ctor / dtor name
    r"(?P<tilde>~?)(?P<name>[A-Za-z_][A-Za-z0-9_]*)"
    # Opening paren
    r"\s*\("
)


def strip_line_comments(line: str) -> str:
    """Remove // comments; block comments handled at buffer level."""
    # Find // outside of strings. Simplified: assume source code is
    # well-behaved (no // inside strings in the header API sections).
    idx = line.find("//")
    if idx != -1:
        return line[:idx]
    return line


def strip_block_comments(text: str) -> str:
    """Remove /* ... */ comments (possibly multi-line)."""
    out = []
    i = 0
    n = len(text)
    while i < n:
        if text[i:i + 2] == "/*":
            end = text.find("*/", i + 2)
            if end == -1:
                break
            # Preserve newlines inside the comment to keep line numbers sane.
            block = text[i:end + 2]
            out.append("\n" * block.count("\n"))
            i = end + 2
        else:
            out.append(text[i])
            i += 1
    return "".join(out)


def strip_strings(line: str) -> str:
    """Mask string/char literals with spaces so their braces don't confuse us."""
    # Replace content inside double-quoted strings (respecting simple escapes).
    result = []
    i = 0
    n = len(line)
    while i < n:
        c = line[i]
        if c == '"':
            result.append('"')
            i += 1
            while i < n:
                c = line[i]
                if c == "\\" and i + 1 < n:
                    result.append("  ")
                    i += 2
                    continue
                if c == '"':
                    result.append('"')
                    i += 1
                    break
                result.append(" ")
                i += 1
        elif c == "'":
            result.append("'")
            i += 1
            while i < n:
                c = line[i]
                if c == "\\" and i + 1 < n:
                    result.append("  ")
                    i += 2
                    continue
                if c == "'":
                    result.append("'")
                    i += 1
                    break
                result.append(" ")
                i += 1
        else:
            result.append(c)
            i += 1
    return "".join(result)


ATTRIBUTE_RE = re.compile(r"\[\[[^\]]*\]\]")


def strip_attributes(line: str) -> str:
    """Remove C++ standard attributes (``[[nodiscard]]``, ``[[maybe_unused]]``,
    ``[[deprecated]]`` …) so they don't break the method/class regexes.

    These attributes can legitimately prefix a declaration's return type, e.g.
    ``[[nodiscard]] json get(...) const;``. The method/class matchers anchor on
    a leading ``[A-Za-z_]`` type/name token, so a leading ``[[…]]`` would make
    the declaration invisible (it is dropped from the surface). Only the
    DOUBLE-bracket attribute form is matched — single-bracket array subscripts
    (``std::array<const char*, 9>``) are left untouched. Replaced with a single
    space so the surrounding tokens stay separated.
    """
    if "[[" not in line:
        return line
    return ATTRIBUTE_RE.sub(" ", line)


class Scope:
    """A nested scope (namespace or class) stacked during parsing."""

    def __init__(self, kind: str, name: str, brace_depth: int,
                 visibility: str | None = None):
        self.kind = kind              # "namespace" | "class" | "struct" | "anon"
        self.name = name
        self.brace_depth = brace_depth
        # Visibility applies to class/struct scopes. struct defaults to public.
        self.visibility = visibility


def parse_header(path: Path) -> list[tuple[str, str, list[str]]]:
    """Return list of (namespace_path, class_name, public_methods).

    namespace_path is a "::"-joined string like "signalwire::agent".
    Methods are already sorted.
    """
    raw = path.read_text(encoding="utf-8", errors="replace")
    text = strip_block_comments(raw)

    findings: list[tuple[str, str, list[str]]] = []

    scopes: list[Scope] = []
    brace_depth = 0

    # Per (namespace, class) collect methods
    # Key: (ns_path, class_name) -> ordered list of methods (dedup at end)
    collected: dict[tuple[str, str], list[str]] = {}

    lines = text.split("\n")
    for raw_line in lines:
        line = strip_line_comments(raw_line)
        code_line = strip_attributes(strip_strings(line))

        # --- Namespace opener
        m = NAMESPACE_RE.match(code_line)
        if m:
            ns = m.group(1)
            # Support "namespace a::b {"
            parts = ns.split("::")
            # Count opening braces on this line
            # We expect exactly one for "namespace X {"
            opens = code_line.count("{")
            closes = code_line.count("}")
            # Push one Scope per part, all sharing the same brace_depth
            for i, p in enumerate(parts):
                # For nested "a::b", only the last part actually opens a brace.
                # C++ allows "namespace a::b { ... }" with a single pair.
                # So the first n-1 parts are logical; only the last increments
                # brace_depth. But standard parsers treat a::b as one scope,
                # which is fine: push a single multi-part scope.
                pass
            # Push as one combined scope using "::" name.
            # Only the last "{" actually opens; count opens-closes adjusts.
            scopes.append(Scope("namespace", ns, brace_depth))
            brace_depth += opens - closes
            continue

        m = ANON_NAMESPACE_RE.match(code_line)
        if m:
            opens = code_line.count("{")
            closes = code_line.count("}")
            scopes.append(Scope("anon", "", brace_depth))
            brace_depth += opens - closes
            continue

        # --- Class / struct opener (NOT a forward decl)
        if not FORWARD_DECL_RE.match(code_line):
            m = CLASS_RE.match(code_line)
            if m:
                class_name = m.group(1)
                is_struct = code_line.lstrip().startswith("struct")
                opens = code_line.count("{")
                closes = code_line.count("}")
                # The enclosing namespace path at the point this class opens.
                _ns_here = "::".join(s.name for s in scopes if s.kind == "namespace")
                scopes.append(Scope(
                    "struct" if is_struct else "class",
                    class_name,
                    brace_depth,
                    visibility="public" if is_struct else "private",
                ))
                brace_depth += opens - closes
                # Generated wire-type / payload structs are METHOD-LESS; the
                # method-detection path below never registers a class with zero
                # public methods. Force-register it here (empty method list) so the
                # reference's bare method-less type surfaces. Scoped strictly to the
                # generated-type namespaces so no ordinary empty struct leaks in.
                if _is_generated_type_ns(_ns_here):
                    collected.setdefault((_ns_here, class_name), [])
                continue

        # --- Visibility specifier
        m = VISIBILITY_RE.match(code_line)
        if m and scopes and scopes[-1].kind in ("class", "struct"):
            scopes[-1].visibility = m.group(1)
            # Visibility lines usually don't include braces, but be safe.
            brace_depth += code_line.count("{") - code_line.count("}")
            continue

        # --- Method detection inside a class scope with public visibility
        # Only at the immediate class-body depth: class_brace_depth + 1.
        # Anything deeper is inside a function body (local variables like
        # ``std::lock_guard<std::mutex> lock(mutex_);`` mustn't be misread as
        # methods).
        if scopes and scopes[-1].kind in ("class", "struct") and \
                scopes[-1].visibility == "public" and \
                brace_depth == scopes[-1].brace_depth + 1:
            method_name = extract_method_name(code_line, scopes[-1].name)
            if method_name is not None:
                ns_path = "::".join(
                    s.name for s in scopes if s.kind == "namespace"
                )
                # Nested classes: include the outer class name chain,
                # but for this SDK that's rare; we only emit the immediate
                # class's methods under its own name.
                class_name = scopes[-1].name
                # The GENERATED REST resources (signalwire::rest::generated) use
                # camelCase operation-method names (listAddresses / createEmbedToken)
                # per the C++ emit idiom; the Python oracle records them snake_case.
                # Canonicalise there exactly as the signature enumerator does
                # (camel_to_snake). Elsewhere the SDK is already snake_case, so
                # this idempotent transform is scoped to the generated namespace to
                # avoid perturbing acronym-bearing hand names.
                if ns_path == "signalwire::rest::generated":
                    method_name = camel_to_snake(method_name)
                # Map ``delete_`` (C++ keyword-avoidance) -> Python ``delete``.
                emit_method = _METHOD_RENAMES.get(method_name, method_name)
                collected.setdefault((ns_path, class_name), []).append(emit_method)

        # --- Update brace depth for any other line with braces
        # (skip string braces already via strip_strings)
        brace_depth += code_line.count("{") - code_line.count("}")

        # Pop scopes whose brace_depth matches current depth-1
        while scopes and brace_depth <= scopes[-1].brace_depth:
            scopes.pop()

    # Dedup and sort methods per class
    for (ns, cls), methods in collected.items():
        seen = []
        seen_set = set()
        for m in methods:
            if m not in seen_set:
                seen.append(m)
                seen_set.add(m)
        findings.append((ns, cls, sorted(seen)))

    return findings


def extract_method_name(code_line: str, class_name: str) -> str | None:
    """If this line looks like a public method declaration, return its name.

    Returns None otherwise. Applies a bunch of filters to weed out:
      - destructors (we don't map them)
      - operators
      - data members (``int foo_ = 0;`` has no paren before ``;``)
      - typedefs and usings
      - control-flow keywords
      - return statements
    """
    stripped = code_line.strip()
    if not stripped:
        return None
    # Skip lines that are plainly not declarations
    if stripped.startswith(("//", "#", "return ", "return;", "return(")):
        return None
    if stripped.startswith(("using ", "typedef ", "friend ", "template ", "enum ")):
        return None
    # A method declaration must contain '(' before ';' or '{'.
    # If there's no '(' at all, it's not a method.
    if "(" not in stripped:
        return None

    # Try the regex
    m = METHOD_RE.match(code_line)
    if not m:
        return None

    name = m.group("name")
    tilde = m.group("tilde")
    prefix = m.group("prefix") or ""

    # Skip destructors (~Foo())
    if tilde:
        return None

    # Skip control-flow / reserved words matched as "name"
    if name in {"if", "else", "for", "while", "do", "switch", "case",
                "return", "sizeof", "throw", "new", "delete",
                "typedef", "using", "template", "friend",
                "enum", "union", "struct", "class", "namespace"}:
        return None

    # Skip operator overloads
    if name == "operator":
        return None

    # Skip when the line is a member declaration that happens to have '(',
    # e.g. "std::function<void()> cb_;". Heuristic: if the "name" is followed
    # immediately by an identifier that itself opens a paren, or if '(' in the
    # prefix (indicating a type expression like std::function<...>(...)).
    # Safer approach: require that the '(' right after the name isn't
    # preceded by '>' without space.
    # In practice the METHOD_RE already handles this since it requires
    # "name\s*\(" at the end. Still, drop if the prefix ends with '>'.
    if prefix.rstrip().endswith(">"):
        # e.g. "std::function<void()> cb;" -- here name is the member,
        # but prefix doesn't end with '>' because the match anchors at the
        # last identifier before '('. This branch is defensive.
        pass

    # Skip if this line is actually a typedef/using for a callable type:
    # "using DynamicConfigCallback = std::function<void(...)>;"
    # Already filtered by stripped.startswith("using ").

    # Skip macro invocations (all-caps names) like REGISTER_SKILL(Foo)
    if name.isupper() and len(name) > 2:
        # Heuristic: all-caps 3+ chars is a macro call, not a method.
        return None

    # Constructor detection: name matches class name
    if name == class_name:
        return "__init__"

    # Skip names starting with underscore (C++ "private-ish" convention)
    if name.startswith("_"):
        return None

    # Skip operator-like names that the regex somehow captured
    if name in SKIP_METHOD_NAMES:
        return None

    return name


# ---------------------------------------------------------------------------
# Module-path translation
# ---------------------------------------------------------------------------

def native_ns_to_module(ns_path: str) -> str:
    """Translate ``signalwire::rest`` -> ``signalwire.rest``.

    Used as a fallback for classes not in CLASS_MODULE_MAP.
    """
    return ns_path.replace("::", ".")


def module_for_class(class_name: str, ns_path: str) -> str | None:
    """Return the Python module path for ``class_name`` in ``ns_path``.

    None means "skip this class entirely" (not used in this SDK right now).
    """
    if class_name in CLASSES_TO_SKIP:
        return None
    # The Python-parity typed RELAY events live in ``signalwire::relay::events``
    # (typed_events.hpp) and route to Python's ``signalwire.relay.event`` module —
    # a NAMESPACE-keyed override, because the class names (RelayEvent/DialEvent)
    # collide with the port's transport-side structs in ``signalwire::relay``.
    if ns_path == "signalwire::relay::events":
        return "signalwire.relay.event"
    if class_name in CLASS_MODULE_MAP:
        return CLASS_MODULE_MAP[class_name]
    # Port-only class: use native translation, with class name snake_cased
    # as the module leaf. Example: signalwire::rest::PhoneCallHandler ->
    # signalwire.rest.phone_call_handler
    leaf = camel_to_snake(class_name)
    base = native_ns_to_module(ns_path)
    if not base:
        return f"signalwire.{leaf}"
    return f"{base}.{leaf}"


def camel_to_snake(name: str) -> str:
    """CamelCase -> snake_case. Handles acronym runs."""
    # Insert an underscore before each capital letter that follows a lowercase
    # or is followed by a lowercase.
    s1 = re.sub(r"(.)([A-Z][a-z]+)", r"\1_\2", name)
    s2 = re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", s1)
    return s2.lower()


# ---------------------------------------------------------------------------
# Top-level
# ---------------------------------------------------------------------------

def git_sha(repo: Path) -> str:
    try:
        return subprocess.check_output(
            ["git", "-C", str(repo), "rev-parse", "HEAD"],
            stderr=subprocess.DEVNULL,
        ).decode().strip()
    except Exception:
        return "N/A"


def _project_generated_rest_methods(modules: dict) -> None:
    """Union each generated REST resource's full oracle-declared method set
    (from the generator's rest_signatures.json) into its surface class list,
    materialising inherited base CRUD verbs the header walker can't see."""
    here = Path(__file__).resolve().parent
    gen_dir = (here.parent / "include" / "signalwire" / "rest" / "namespaces"
               / "generated")
    smap_path = gen_dir / "generated_surface_map.json"
    sc_path = gen_dir / "rest_signatures.json"
    if not smap_path.is_file() or not sc_path.is_file():
        return
    class_mod = json.loads(smap_path.read_text())
    sidecar = json.loads(sc_path.read_text()).get("methods", {})
    # The surface oracle (griffe) records a generated resource's DECLARED
    # methods only — it does NOT surface base-INHERITED list/get/delete for the
    # write-base (CrudResource/FabricResource/ReadResource) classes, but it DOES
    # record the ``create``/``update`` write overrides. The header walker already
    # sees every DECLARED method (operation/command/set + the BaseResource
    # classes' own list/get/delete + typed create/update); the ONLY methods it
    # misses are the inherited ``create``/``update`` on the write-base classes.
    # So project ONLY create/update from the sidecar — never the inherited
    # list/get/delete (griffe drops them here) and never the container members
    # (griffe records the container as ``__init__``-only in the surface oracle).
    for key in sidecar:
        if "::" not in key:
            continue
        cls, native = key.split("::", 1)
        canon = _METHOD_RENAMES.get(camel_to_snake(native), camel_to_snake(native))
        if canon not in ("create", "update"):
            continue
        mod = class_mod.get(cls)
        if mod is None:
            raise SystemExit(
                f"enumerate_surface: sidecar class {cls!r} not in "
                f"generated_surface_map.json (regenerate the REST layer)")
        mod_entry = modules.setdefault(mod, {"classes": {}, "functions": []})
        existing = set(mod_entry["classes"].get(cls, []))
        existing.add(canon)
        mod_entry["classes"][cls] = sorted(existing)


def build_snapshot(repo: Path, include_dir: Path) -> dict:
    modules: dict[str, dict] = {}

    # Walk every .hpp/.h under include/
    patterns = ("**/*.hpp", "**/*.h")
    header_files: list[Path] = []
    for p in patterns:
        header_files.extend(sorted(include_dir.glob(p)))

    for path in header_files:
        try:
            findings = parse_header(path)
        except Exception as e:  # pragma: no cover
            print(f"warning: failed to parse {path}: {e}", file=sys.stderr)
            continue

        for ns_path, class_name, methods in findings:
            # Apply class rename (e.g. swml::Service -> SWMLService)
            emit_class = class_name
            emit_mod = None
            # Generated wire-type / read-side-payload structs route by PATH — the
            # namespace-prefix router wins over the name-keyed lookups (the names
            # recur cross-module / collide with SDK classes). Emitted method-less.
            gen_mod = generated_type_module(ns_path)
            if gen_mod is not None:
                emit_mod = gen_mod
            elif (ns_path, class_name) in CLASS_RENAME_MAP:
                emit_mod, emit_class = CLASS_RENAME_MAP[(ns_path, class_name)]
            if emit_mod is None:
                emit_mod = module_for_class(emit_class, ns_path)
            if emit_mod is None:
                continue
            mod_entry = modules.setdefault(emit_mod, {"classes": {}, "functions": []})
            # If the class was already seen in another header (unlikely but
            # possible for split public/impl headers), merge the method list.
            existing = mod_entry["classes"].get(emit_class, [])
            merged = sorted(set(existing) | set(methods))
            mod_entry["classes"][emit_class] = merged

    # Apply mixin projections: the C++ AgentBase flattens Python's 9 mixin
    # classes. Emit the same method list under each mixin module path so
    # the diff against python_surface.json recognises the symbols as
    # implemented.
    # The mixin methods live on AgentBase directly OR on its base
    # ``swml::Service`` (which the C++ AgentBase inherits — the header walker
    # doesn't follow inheritance, so pool both classes as donors, mirroring
    # ruby's SURFACE_METHOD_DONORS: an AuthMixin/WebMixin method C++ defines on
    # the shared Service base is legitimately part of AgentBase's callable
    # surface). Service is enumerated as ``swml_service.SWMLService``.
    agent_base_methods: set[str] = set()
    ab_entry = modules.get("signalwire.core.agent_base", {})
    ab_methods = ab_entry.get("classes", {}).get("AgentBase", [])
    agent_base_methods = set(ab_methods)
    svc_entry = modules.get("signalwire.core.swml_service", {})
    svc_methods = svc_entry.get("classes", {}).get("SWMLService", [])
    agent_base_methods |= set(svc_methods)

    for (mod, cls), expected_methods in MIXIN_PROJECTIONS.items():
        present = [m for m in expected_methods if m in agent_base_methods]
        mod_entry = modules.setdefault(mod, {"classes": {}, "functions": []})
        # Mixin class always exists (even if empty) so the class symbol
        # itself isn't flagged missing.
        mod_entry["classes"][cls] = sorted(present)

    # Generated REST base hierarchy projection. The generated resource bases
    # live in ``signalwire::rest::generated`` (base_resource.hpp) but the
    # regex header walker cannot parse their triple-nested / member-init form,
    # so they never reach the surface; libclang (the SIGNATURES enumerator)
    # sees them fine. Inject the base classes onto ``signalwire.rest._base``
    # with the method sets the Python oracle records (BaseResource.__init__,
    # ReadResource.get/list, CrudResource.create/delete/update,
    # CrudWithAddresses.list_addresses) so the base surface matches. The C++
    # FabricResource folds Python's CrudWithAddresses (it carries list_addresses)
    # and FabricResourcePUT is a Python-only PUT-marker subclass with no members
    # (recorded empty). The concrete resources still carry their own method
    # membership; this only reconciles the shared base layer.
    _base = modules.setdefault("signalwire.rest._base", {"classes": {}, "functions": []})
    for _bcls, _bmeths in (
        ("BaseResource", ["__init__"]),
        ("ReadResource", ["get", "list", "paginate"]),
        ("CrudResource", ["create", "delete", "update"]),
        ("CrudWithAddresses", ["list_addresses"]),
        ("FabricResource", []),
        ("FabricResourcePUT", []),
    ):
        _base["classes"][_bcls] = sorted(_bmeths)

    # Generated REST resource-method projection (item A/B adoption). The regex
    # header walker sees only a generated class's DECLARED methods; it misses
    # the CRUD verbs each resource INHERITS from base_resource.hpp
    # (list/get/create/update/delete_) — but the Python oracle records those
    # per-class (griffe records the subclass's own overrides). The generator's
    # rest_signatures.json lists every method the Python class declares, keyed
    # ``Class::method`` in the C++ spelling; union those (canonicalised) into
    # each generated class so the surface membership matches. Idiom via
    # emit+projection, never omission.
    _project_generated_rest_methods(modules)

    # Built-in skill projection: each skill class lives in src/skills/builtin/<name>.cpp
    # (implementation file, invisible to the header walker), registered at static-init.
    # Project each into its Python-canonical ``signalwire.skills.<name>.skill`` module.
    _project_builtin_skills(modules, repo)

    # Module-level free functions the header walker can't see (e.g. relay parse_event).
    _project_module_functions(modules, repo)

    # Serialization idiom: C++ ``to_json`` == Python ``to_dict`` on contexts +
    # FunctionResult; surface the ``to_dict`` alias where ``to_json`` exists.
    _project_to_dict_aliases(modules)

    # RELAY concrete call-action control methods: the C++ ``Action`` class
    # flattens stop/pause/resume/volume/start_input_timers; each concrete
    # subclass inherits them. The oracle records the control methods directly on
    # each concrete action — project the oracle's per-action set from the methods
    # the C++ Action genuinely defines (see RELAY_ACTION_CONTROL_METHODS + the
    # subclass-projection block below).
    action_methods: set[str] = set()
    for _mod in ("signalwire.relay.action", "signalwire.relay.call"):
        _e = modules.get(_mod, {})
        action_methods |= set(_e.get("classes", {}).get("Action", []))

    # The base ``Action`` class: Python records it in ``signalwire.relay.call`` with
    # ``__init__``/``is_done``/``wait``. The C++ ``Action`` (native module
    # ``signalwire.relay.action``, a PORT_ADDITION) carries these; project the
    # Python-recorded subset onto relay.call so the base symbol lines up (its
    # richer C++ surface stays under relay.action as the port addition).
    _action_own = modules.get("signalwire.relay.action", {}).get("classes", {}).get("Action", [])
    if _action_own:
        proj = sorted({"__init__"} | {m for m in ("is_done", "wait") if m in _action_own})
        modules.setdefault("signalwire.relay.call", {"classes": {}, "functions": []})
        modules["signalwire.relay.call"]["classes"]["Action"] = proj

    # Concrete RELAY call-action subclasses (PlayAction/RecordAction/…). The C++
    # port FLATTENS every control onto the unified ``Action`` and declares each
    # concrete subclass via the ``SIGNALWIRE_RELAY_ACTION_SUBCLASS(Name)`` macro
    # (``class Name : public Action { using Action::Action; };``). The regex
    # header walker cannot see macro-generated classes, so project each subclass
    # the macro actually declares in action.hpp. Every subclass inherits the
    # unified Action's ctor (``__init__``) PLUS the control methods the oracle
    # records on that concrete action (RELAY_ACTION_CONTROL_METHODS) — the
    # inherited stop/pause/resume/volume/start_input_timers are the real
    # user-facing control surface, only projected where the C++ Action genuinely
    # defines that method (never inventing surface).
    _action_hpp = repo / "include/signalwire/relay/action.hpp"
    if _action_hpp.is_file():
        _txt = _action_hpp.read_text(encoding="utf-8")
        _declared = set(
            re.findall(r"SIGNALWIRE_RELAY_ACTION_SUBCLASS\(([A-Za-z_]\w*)\)", _txt)
        )
        _declared.discard("NAME")  # the macro parameter, not a real subclass
        call_mod = modules.setdefault("signalwire.relay.call", {"classes": {}, "functions": []})
        for _sub in _declared:
            _meths = {"__init__"}
            for _ctl in RELAY_ACTION_CONTROL_METHODS.get(_sub, []):
                if _ctl in action_methods:  # only if the C++ Action truly has it
                    _meths.add(_ctl)
            call_mod["classes"][_sub] = sorted(_meths)

    # Constructor / call-operator idiom projections. The regex header walker
    # emits ``__init__`` only for a class with an explicitly-declared public
    # constructor, and never emits ``__call__`` (C++ spells it ``operator()``,
    # which is in SKIP_METHOD_NAMES). These classes genuinely have the member
    # (a private singleton ctor, a defaulted ctor, or ``operator()``); surface
    # the Python-canonical name when the C++ class is present.
    def _ensure_member(mod: str, cls: str, member: str) -> None:
        entry = modules.get(mod, {}).get("classes", {}).get(cls)
        if entry is not None and member not in entry:
            entry.append(member)
            modules[mod]["classes"][cls] = sorted(set(entry))

    # SWAIGFunction exposes ``operator()`` + ``call`` -> Python ``__call__``.
    _ensure_member("signalwire.core.swaig_function", "SWAIGFunction", "__call__")
    # SkillRegistry is a singleton (private ctor); SkillBase has a defaulted
    # protected ctor. Both genuinely construct — surface ``__init__``.
    _ensure_member("signalwire.skills.registry", "SkillRegistry", "__init__")
    _ensure_member("signalwire.core.skill_base", "SkillBase", "__init__")

    # Remove empty modules (shouldn't happen in practice but be tidy)
    modules = {k: v for k, v in modules.items() if v["classes"] or v["functions"]}

    return {
        "version": "1",
        "generated_from": f"signalwire-cpp @ {git_sha(repo)}",
        "modules": modules,
    }


def main(argv: list[str]) -> int:
    repo = Path(__file__).resolve().parent.parent
    default_include = repo / "include" / "signalwire"
    default_output = repo / "port_surface.json"

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--include-dir", type=Path, default=default_include,
        help=f"Header root to walk (default: {default_include})",
    )
    parser.add_argument(
        "--output", type=Path, default=default_output,
        help=f"Where to write JSON (default: {default_output})",
    )
    parser.add_argument(
        "--stdout", action="store_true",
        help="Print JSON to stdout instead of writing --output",
    )
    parser.add_argument(
        "--check", action="store_true",
        help="Compare against the file at --output; exit 1 on drift",
    )
    args = parser.parse_args(argv)

    if not args.include_dir.is_dir():
        print(f"error: include dir not found: {args.include_dir}", file=sys.stderr)
        return 1

    snapshot = build_snapshot(repo, args.include_dir)
    rendered = json.dumps(snapshot, indent=2, sort_keys=True) + "\n"

    if args.check:
        if not args.output.is_file():
            print(f"error: {args.output} does not exist", file=sys.stderr)
            return 1
        existing = args.output.read_text(encoding="utf-8")

        def strip_meta(s: str) -> str:
            obj = json.loads(s)
            obj.pop("generated_from", None)
            return json.dumps(obj, indent=2, sort_keys=True) + "\n"

        if strip_meta(rendered) != strip_meta(existing):
            print(
                "DRIFT: port_surface.json is stale relative to headers.\n"
                "  Regenerate:\n"
                "    python3 scripts/enumerate_surface.py",
                file=sys.stderr,
            )
            return 1
        return 0

    if args.stdout:
        sys.stdout.write(rendered)
    else:
        args.output.write_text(rendered, encoding="utf-8")
        print(f"wrote {args.output} "
              f"({len(snapshot['modules'])} modules, "
              f"{sum(len(m['classes']) for m in snapshot['modules'].values())} classes, "
              f"{sum(sum(len(ms) for ms in m['classes'].values()) for m in snapshot['modules'].values())} methods)",
              file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
