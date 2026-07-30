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
import os
import re
import subprocess
import sys
from pathlib import Path


def _resolve_psdk() -> Path:
    """Resolve the porting-sdk checkout (for the reference surface oracle).

    Honours PORTING_SDK_DIR, then PORTING_SDK (the var run-ci / the surface suite
    export), then the adjacent ``<repo>/../porting-sdk``. The env fallback makes a
    WORKTREE run resolve the real checkout (a worktree's parent has no porting-sdk
    sibling); real CI uses adjacency. Mirrors enumerate_signatures._resolve_psdk."""
    for var in ("PORTING_SDK_DIR", "PORTING_SDK"):
        val = os.environ.get(var)
        if val and (Path(val) / "type_aliases.yaml").is_file():
            return Path(val).resolve()
    return (Path(__file__).resolve().parent.parent.parent / "porting-sdk").resolve()


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
    # The credential carriers live BESIDE AuthHandler in the reference module
    # (the oracle records signalwire.core.auth_handler.BasicCredentials /
    # .BearerCredentials since porting-sdk dcff742 resolved the FastAPI names).
    # Without this the port-only fallback would snake_case the class name into
    # its own module leaf (signalwire.core.basic_credentials) and the carriers
    # would never meet their reference counterparts.
    "BasicCredentials": "signalwire.core.auth_handler",
    "BearerCredentials": "signalwire.core.auth_handler",
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
    "SignalWireRestTransportError": "signalwire.rest._base",
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
        "signalwire.core.swml_service",
        "SWMLService",
    ),
    # ``signalwire::pom::Section`` projects to ``signalwire.pom.pom.Section``;
    # disambiguates from ``signalwire::swml::Section`` (which falls through
    # to the native namespace translation as ``signalwire.swml.section``).
    ("signalwire::pom", "Section"): (
        "signalwire.pom.pom",
        "Section",
    ),
    # C++ uses ``XxxNamespace`` for all REST namespaces; Python uses
    # ``XxxResource`` for single-resource namespaces and ``XxxNamespace``
    # for multi-resource ones. Map the single-resource cases.
    # RequestOptions lives in signalwire::rest; the oracle records it in the
    # private module signalwire.rest._request_options. Route it there (the native
    # namespace translation would drop the leading underscore). Only merge() is a
    # method — the value struct's data fields (timeout/retries/…) are not surface
    # symbols, exactly as the Python dataclass fields aren't (go/ts/ruby/java match).
    ("signalwire::rest", "RequestOptions"): (
        "signalwire.rest._request_options",
        "RequestOptions",
    ),
    ("signalwire::rest", "AddressesNamespace"): (
        "signalwire.rest.namespaces.addresses",
        "AddressesResource",
    ),
    ("signalwire::rest", "ChatNamespace"): (
        "signalwire.rest.namespaces.chat",
        "ChatResource",
    ),
    ("signalwire::rest", "ImportedNumbersNamespace"): (
        "signalwire.rest.namespaces.imported_numbers",
        "ImportedNumbersResource",
    ),
    ("signalwire::rest", "LookupNamespace"): (
        "signalwire.rest.namespaces.lookup",
        "LookupResource",
    ),
    ("signalwire::rest", "MFANamespace"): (
        "signalwire.rest.namespaces.mfa",
        "MfaResource",
    ),
    ("signalwire::rest", "NumberGroupsNamespace"): (
        "signalwire.rest.namespaces.number_groups",
        "NumberGroupsResource",
    ),
    ("signalwire::rest", "PhoneNumbersNamespace"): (
        "signalwire.rest.namespaces.phone_numbers",
        "PhoneNumbersResource",
    ),
    ("signalwire::rest", "PubSubNamespace"): (
        "signalwire.rest.namespaces.pubsub",
        "PubSubResource",
    ),
    ("signalwire::rest", "QueuesNamespace"): (
        "signalwire.rest.namespaces.queues",
        "QueuesResource",
    ),
    ("signalwire::rest", "RecordingsNamespace"): (
        "signalwire.rest.namespaces.recordings",
        "RecordingsResource",
    ),
    ("signalwire::rest", "ShortCodesNamespace"): (
        "signalwire.rest.namespaces.short_codes",
        "ShortCodesResource",
    ),
    ("signalwire::rest", "SipProfileNamespace"): (
        "signalwire.rest.namespaces.sip_profile",
        "SipProfileResource",
    ),
    ("signalwire::rest", "VerifiedCallersNamespace"): (
        "signalwire.rest.namespaces.verified_callers",
        "VerifiedCallersResource",
    ),
    # ProjectTokens is exposed as a nested class on the project namespace.
    ("signalwire::rest", "ProjectTokens"): (
        "signalwire.rest.namespaces.project",
        "ProjectTokens",
    ),
    # DatasphereDocuments is the typed wrapper around the documents
    # CrudResource; Python exposes it as DatasphereDocuments inside
    # namespaces/datasphere.py.
    ("signalwire::rest", "DatasphereDocuments"): (
        "signalwire.rest.namespaces.datasphere",
        "DatasphereDocuments",
    ),
    # Fabric: C++ uses ``FabricXxx`` names for sub-resources; Python uses
    # ``XxxResource`` (or shorter names). Map at emit time so the audit
    # treats them as the same class.
    ("signalwire::rest", "FabricCallFlows"): (
        "signalwire.rest.namespaces.fabric",
        "CallFlowsResource",
    ),
    ("signalwire::rest", "FabricConferenceRooms"): (
        "signalwire.rest.namespaces.fabric",
        "ConferenceRoomsResource",
    ),
    ("signalwire::rest", "FabricCxmlApplications"): (
        "signalwire.rest.namespaces.fabric",
        "CxmlApplicationsResource",
    ),
    ("signalwire::rest", "FabricGenericResources"): (
        "signalwire.rest.namespaces.fabric",
        "GenericResources",
    ),
    ("signalwire::rest", "FabricSubscribers"): (
        "signalwire.rest.namespaces.fabric",
        "SubscribersResource",
    ),
    # Logs: Python names are ``MessageLogs`` / ``VoiceLogs`` etc; C++ uses
    # ``LogsMessages`` / ``LogsVoice`` for namespace-prefix consistency.
    ("signalwire::rest", "LogsMessages"): (
        "signalwire.rest.namespaces.logs",
        "MessageLogs",
    ),
    ("signalwire::rest", "LogsVoice"): (
        "signalwire.rest.namespaces.logs",
        "VoiceLogs",
    ),
    ("signalwire::rest", "LogsFax"): (
        "signalwire.rest.namespaces.logs",
        "FaxLogs",
    ),
    ("signalwire::rest", "LogsConferences"): (
        "signalwire.rest.namespaces.logs",
        "ConferenceLogs",
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
        "signalwire.relay.client",
        "CallHandler",
    ),
    ("signalwire::relay", "InboundMessageHandler"): (
        "signalwire.relay.client",
        "MessageHandler",
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
    smap = (
        here.parent
        / "include"
        / "signalwire"
        / "rest"
        / "namespaces"
        / "generated"
        / "generated_surface_map.json"
    )
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
    "RelayRest": "relay_rest",
    "Fabric": "fabric",
    "Calling": "calling",
    "Video": "video",
    "Datasphere": "datasphere",
    "Logs": "logs",
    "Message": "message",
    "Messages": "messages",
    "Voice": "voice",
    "Fax": "fax",
    "Project": "project",
    "Projects": "projects",
    "Chat": "chat",
    "PubSub": "pubsub",
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
GENERATED_TYPE_NS_PREFIXES = (
    _TYPES_NS_PREFIX.rstrip(":"),
    *tuple(GENERATED_PAYLOAD_NS),
)

# Set at build_snapshot entry: the ``…/include`` root under which the generated
# payload header namespaces resolve (``signalwire::core::foo`` -> <root>/signalwire/core/foo).
_INCLUDE_ROOT: Path = Path()


def generated_type_module(ns_path: str) -> str | None:
    """If ``ns_path`` is one of the generated wire-type / payload namespaces,
    return the canonical Python reference module (routed by PATH). Else None."""
    if ns_path in GENERATED_PAYLOAD_NS:
        return GENERATED_PAYLOAD_NS[ns_path]
    if ns_path.startswith(_TYPES_NS_PREFIX):
        seg = ns_path[len(_TYPES_NS_PREFIX) :].split("::", 1)[0]
        key = _TYPES_NS_KEY.get(seg)
        if key is None:
            raise SystemExit(
                f"enumerate_surface.py: generated types namespace {ns_path!r} has "
                f"unknown segment {seg!r} (add to _TYPES_NS_KEY)"
            )
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
    # SWML keyword-escape verbs. ``goto``/``return``/``switch`` are C++ reserved
    # words, so swml::Service spells the verbs ``goto_section``/``return_section``/
    # ``switch_section``. The Python reference routes every SWML verb dynamically
    # (no per-verb symbol); the G fold (diff-side _fold_swml_verbs) drops a
    # SWMLService.<verb> whose leaf is a canonical schema verb name. Normalise the
    # keyword-escaped spelling back to the canonical verb here (emission), so the
    # fold retires them like the other ~35 typed verbs — idiom fixed at emission,
    # not an allow-list entry.
    "goto_section": "goto",
    "return_section": "return",
    "switch_section": "switch",
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
        "signalwire.core.security.webhook_validator",
        "validate_webhook_signature",
    ),
    ("signalwire::security", "ValidateRequest"): (
        "signalwire.core.security.webhook_validator",
        "validate_request",
    ),
    # The framework-free webhook-validation decision core (porting-sdk
    # webhooks.md + HIDDEN_SURFACE_AUDIT Pass 1). Python exposes it as a
    # module-level ``validate`` under ``webhook_middleware``; C++ ships the
    # same decomposed ``(method,url,headers,body,signing_key) ->
    # optional<(status,headers,body)>`` core as a free function. The
    # cpp-httplib ``WrapWithSignatureValidation`` wrapper stays a
    # PORT_ADDITION idiom on top of this.
    ("signalwire::security", "Validate"): (
        "signalwire.core.security.webhook_middleware",
        "validate",
    ),
    # Standalone security-hygiene utils (security_utils.py). C++ groups them in
    # a nested ``signalwire::security::security_utils`` namespace with PascalCase
    # names; Python keeps them as module-level snake_case functions under
    # ``signalwire.core.security.security_utils``.
    ("signalwire::security::security_utils", "FilterSensitiveHeaders"): (
        "signalwire.core.security.security_utils",
        "filter_sensitive_headers",
    ),
    ("signalwire::security::security_utils", "RedactUrl"): (
        "signalwire.core.security.security_utils",
        "redact_url",
    ),
    ("signalwire::security::security_utils", "IsValidHostname"): (
        "signalwire.core.security.security_utils",
        "is_valid_hostname",
    ),
    # SWAIG schema inference (type_inference.py). C++ groups these in a nested
    # ``signalwire::swaig::type_inference`` namespace (snake_case names);
    # Python keeps them module-level under
    # ``signalwire.core.agent.tools.type_inference``.
    ("signalwire::swaig::type_inference", "infer_schema"): (
        "signalwire.core.agent.tools.type_inference",
        "infer_schema",
    ),
    ("signalwire::swaig::type_inference", "create_typed_handler_wrapper"): (
        "signalwire.core.agent.tools.type_inference",
        "create_typed_handler_wrapper",
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
    "if",
    "else",
    "for",
    "while",
    "do",
    "switch",
    "case",
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
        "add_function_include",
        "add_hint",
        "add_hints",
        "add_internal_filler",
        "add_language",
        "add_mcp_server",
        "add_pattern_hint",
        "add_pronunciation",
        "enable_debug_events",
        "enable_mcp_server",
        "get_language_params",
        "set_function_includes",
        "set_global_data",
        "set_internal_fillers",
        "set_language_params",
        "set_languages",
        "set_multilingual",
        "set_native_functions",
        "set_param",
        "set_params",
        "set_post_prompt_llm_params",
        "set_prompt_llm_params",
        "set_pronunciations",
        "update_global_data",
    ],
    ("signalwire.core.mixins.mcp_server_mixin", "MCPServerMixin"): [
        # Empty in Python -- class exists as a marker only.
    ],
    ("signalwire.core.mixins.prompt_mixin", "PromptMixin"): [
        "contexts",
        "define_contexts",
        "get_post_prompt",
        "get_prompt",
        "prompt_add_section",
        "prompt_add_subsection",
        "prompt_add_to_section",
        "prompt_has_section",
        "reset_contexts",
        "set_post_prompt",
        "set_prompt_pom",
        "set_prompt_text",
    ],
    # Python additionally extracted a ``PromptManager`` class that
    # PromptMixin delegates to. The user-facing surface is identical
    # (``agent.prompt_manager.X`` ≡ ``agent.X``). Project the same set of
    # AgentBase methods to PromptManager so the cross-language audit
    # treats both paths as covered.
    #
    # ``agent``: the reference's ``PromptManager(self)`` back-reference to the
    # owning AgentBase (``self.agent``, a ctor param the oracle's class-B2 rule
    # records). C++ does not extract a separate manager OBJECT — the prompt
    # methods live directly on AgentBase, so the manager and its ``agent`` are
    # the SAME object and the back-reference is ``*this``. Projecting it here is
    # the fold of that merge: reaching the agent from the manager is exactly as
    # available in C++ as in Python, it is simply already in hand.
    ("signalwire.core.agent.prompt.manager", "PromptManager"): [
        "__init__",
        "define_contexts",
        "get_contexts",
        "get_post_prompt",
        "get_prompt",
        "get_raw_prompt",
        "prompt_add_section",
        "prompt_add_subsection",
        "prompt_add_to_section",
        "prompt_has_section",
        "set_post_prompt",
        "set_prompt_pom",
        "set_prompt_text",
    ],
    ("signalwire.core.mixins.serverless_mixin", "ServerlessMixin"): [
        # AgentBase::handle_serverless_request delegates to the per-platform
        # dispatchers in signalwire::utils. Projected here so the ServerlessMixin
        # method matches (surface + signature).
        "handle_serverless_request",
    ],
    ("signalwire.core.mixins.skill_mixin", "SkillMixin"): [
        "add_skill",
        "has_skill",
        "list_skills",
        "remove_skill",
    ],
    ("signalwire.core.mixins.state_mixin", "StateMixin"): [
        "validate_tool_token",
    ],
    ("signalwire.core.mixins.tool_mixin", "ToolMixin"): [
        "define_tool",
        "define_tools",
        "on_function_call",
        "register_swaig_function",
    ],
    ("signalwire.core.agent.tools.registry", "ToolRegistry"): [
        "__init__",
        "define_tool",
        "register_swaig_function",
        "has_function",
        "get_function",
        "get_all_functions",
        "remove_function",
    ],
    # Both methods ARE public C++ surface — swml::Service declares them at
    # include/signalwire/swml/service.hpp:96 and :100 — so they project.
    #
    # This key was previously declared TWICE in this dict: an earlier stanza bound
    # it to an empty list with a comment asserting the two methods were
    # "implementation-detail protected helpers ... not part of the public C++
    # surface. Tracked as a PORT_OMISSIONS exemption, not a projection." That
    # comment was false on both counts (the methods are public, and PORT_OMISSIONS
    # has no such entry), and because a later duplicate key silently wins in a
    # Python dict literal, the empty stanza had never had any effect. The dead
    # stanza is deleted; this one is and always was the live binding.
    ("signalwire.core.mixins.auth_mixin", "AuthMixin"): [
        "validate_basic_auth",
        "get_basic_auth_credentials",
    ],
    ("signalwire.core.mixins.web_mixin", "WebMixin"): [
        "as_router",
        "enable_debug_routes",
        "manual_set_proxy_url",
        "run",
        "serve",
        "set_dynamic_config_callback",
        "on_request",
        "on_swml_request",
        "register_routing_callback",
        "setup_graceful_shutdown",
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
    "setup",
    "register_tools",
    "get_hints",
    "get_global_data",
    "get_prompt_sections",
    "get_parameter_schema",
    "get_instance_key",
    "cleanup",
    "get_datamap_functions",
    "skill_name",
    "skill_description",
}
SKILL_PROJECTIONS: dict[str, tuple[str, str, list[str]]] = {
    # cpp_class: (python_module, python_class, python_recorded_methods)
    "ApiNinjasTriviaSkill": (
        "signalwire.skills.api_ninjas_trivia.skill",
        "ApiNinjasTriviaSkill",
        [
            "__init__",
            "get_instance_key",
            "get_parameter_schema",
            "get_tools",
            "register_tools",
            "setup",
        ],
    ),
    "ClaudeSkillsSkill": (
        "signalwire.skills.claude_skills.skill",
        "ClaudeSkillsSkill",
        [
            "get_hints",
            "get_instance_key",
            "get_parameter_schema",
            "register_tools",
            "setup",
        ],
    ),
    "DatasphereSkill": (
        "signalwire.skills.datasphere.skill",
        "DataSphereSkill",
        [
            "cleanup",
            "get_global_data",
            "get_hints",
            "get_instance_key",
            "get_parameter_schema",
            "get_prompt_sections",
            "register_tools",
            "setup",
        ],
    ),
    "DatasphereServerlessSkill": (
        "signalwire.skills.datasphere_serverless.skill",
        "DataSphereServerlessSkill",
        [
            "get_global_data",
            "get_hints",
            "get_instance_key",
            "get_parameter_schema",
            "get_prompt_sections",
            "register_tools",
            "setup",
        ],
    ),
    "DateTimeSkill": (
        "signalwire.skills.datetime.skill",
        "DateTimeSkill",
        [
            "get_hints",
            "get_parameter_schema",
            "get_prompt_sections",
            "register_tools",
            "setup",
        ],
    ),
    "GoogleMapsSkill": (
        "signalwire.skills.google_maps.skill",
        "GoogleMapsSkill",
        [
            "get_hints",
            "get_parameter_schema",
            "get_prompt_sections",
            "register_tools",
            "setup",
        ],
    ),
    "InfoGathererSkill": (
        "signalwire.skills.info_gatherer.skill",
        "InfoGathererSkill",
        [
            "get_global_data",
            "get_instance_key",
            "get_parameter_schema",
            "register_tools",
            "setup",
        ],
    ),
    "JokeSkill": (
        "signalwire.skills.joke.skill",
        "JokeSkill",
        [
            "get_global_data",
            "get_hints",
            "get_parameter_schema",
            "get_prompt_sections",
            "register_tools",
            "setup",
        ],
    ),
    "MathSkill": (
        "signalwire.skills.math.skill",
        "MathSkill",
        [
            "get_hints",
            "get_parameter_schema",
            "get_prompt_sections",
            "register_tools",
            "setup",
        ],
    ),
    "NativeVectorSearchSkill": (
        "signalwire.skills.native_vector_search.skill",
        "NativeVectorSearchSkill",
        [
            "cleanup",
            "get_global_data",
            "get_hints",
            "get_instance_key",
            "get_parameter_schema",
            "get_prompt_sections",
            "register_tools",
            "setup",
        ],
    ),
    "PlayBackgroundFileSkill": (
        "signalwire.skills.play_background_file.skill",
        "PlayBackgroundFileSkill",
        [
            "__init__",
            "get_instance_key",
            "get_parameter_schema",
            "get_tools",
            "register_tools",
            "setup",
        ],
    ),
    "SpiderSkill": (
        "signalwire.skills.spider.skill",
        "SpiderSkill",
        [
            "__init__",
            "cleanup",
            "get_hints",
            "get_instance_key",
            "get_parameter_schema",
            "register_tools",
            "remove_xpaths",
            "setup",
        ],
    ),
    "SwmlTransferSkill": (
        "signalwire.skills.swml_transfer.skill",
        "SWMLTransferSkill",
        [
            "get_hints",
            "get_instance_key",
            "get_parameter_schema",
            "get_prompt_sections",
            "register_tools",
            "setup",
        ],
    ),
    "WeatherApiSkill": (
        "signalwire.skills.weather_api.skill",
        "WeatherApiSkill",
        ["__init__", "get_parameter_schema", "get_tools", "register_tools", "setup"],
    ),
    "WebSearchSkill": (
        "signalwire.skills.web_search.skill",
        "WebSearchSkill",
        [
            "get_global_data",
            "get_hints",
            "get_instance_key",
            "get_parameter_schema",
            "get_prompt_sections",
            "register_tools",
            "setup",
        ],
    ),
    "WikipediaSearchSkill": (
        "signalwire.skills.wikipedia_search.skill",
        "WikipediaSearchSkill",
        [
            "get_hints",
            "get_parameter_schema",
            "get_prompt_sections",
            "register_tools",
            "search_wiki",
            "setup",
        ],
    ),
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
    for mod, cls in _TO_DICT_ALIAS_CLASSES:
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
        (
            "include/signalwire/utils/serverless.hpp",
            "is_serverless_mode",
            "is_serverless_mode",
        ),
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
        (
            "include/signalwire/security/security_utils.hpp",
            "FilterSensitiveHeaders",
            "filter_sensitive_headers",
        ),
        ("include/signalwire/security/security_utils.hpp", "RedactUrl", "redact_url"),
        (
            "include/signalwire/security/security_utils.hpp",
            "IsValidHostname",
            "is_valid_hostname",
        ),
    ],
    # Inbound-webhook signature validation (webhooks.md). C++ exposes these as
    # PascalCase free functions in ``signalwire::security``; Python keeps them
    # module-level under ``signalwire.core.security.webhook_validator``.
    "signalwire.core.security.webhook_validator": [
        (
            "include/signalwire/security/webhook_validator.hpp",
            "ValidateWebhookSignature",
            "validate_webhook_signature",
        ),
        (
            "include/signalwire/security/webhook_validator.hpp",
            "ValidateRequest",
            "validate_request",
        ),
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
        ("include/signalwire/security/webhook_validator.hpp", "Validate", "validate"),
    ],
    # Top-level ``signalwire/__init__.py`` package helpers. C++ implements them
    # as free functions in ``namespace signalwire`` (src/signalwire.cpp,
    # declared in include/signalwire/signalwire.hpp). ``RestClient`` keeps its
    # PascalCase spelling on both sides (it is a factory named for the class).
    "signalwire": [
        ("include/signalwire/signalwire.hpp", "RestClient", "RestClient"),
        ("include/signalwire/signalwire.hpp", "register_skill", "register_skill"),
        (
            "include/signalwire/signalwire.hpp",
            "add_skill_directory",
            "add_skill_directory",
        ),
        (
            "include/signalwire/signalwire.hpp",
            "list_skills_with_params",
            "list_skills_with_params",
        ),
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
        (
            "include/signalwire/swaig/type_inference.hpp",
            "create_typed_handler_wrapper",
            "create_typed_handler_wrapper",
        ),
    ],
    # Logging-config module-level helpers. C++ implements them as free functions
    # in ``signalwire::core::logging_config`` (same snake_case names as Python).
    "signalwire.core.logging_config": [
        (
            "include/signalwire/core/logging_config.hpp",
            "configure_logging",
            "configure_logging",
        ),
        ("include/signalwire/core/logging_config.hpp", "get_logger", "get_logger"),
        (
            "include/signalwire/core/logging_config.hpp",
            "reset_logging_configuration",
            "reset_logging_configuration",
        ),
        (
            "include/signalwire/core/logging_config.hpp",
            "strip_control_chars",
            "strip_control_chars",
        ),
        (
            "include/signalwire/core/logging_config.hpp",
            "get_execution_mode",
            "get_execution_mode",
        ),
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
    method_re = re.compile(
        r"\b([a-z_][a-z0-9_]*)\s*\([^;{]*\)\s*(?:const\s*)?(?:override|noexcept|\{)"
    )
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
        present.extend(
            m
            for m in py_methods
            if m == "__init__" or m in own or m in _SKILL_BASE_METHODS
        )
        mod_entry = modules.setdefault(mod, {"classes": {}, "functions": []})
        mod_entry["classes"][py_cls] = sorted(set(present))


# ---------------------------------------------------------------------------
# AI-Chat surface projection (idiom fold onto the python oracle)
# ---------------------------------------------------------------------------
# The Python reference records the whole AI-Chat surface in ONE module,
# ``signalwire.ai_chat.client`` (client.py holds the client, the typed error
# family, and the result dataclasses). The C++ port splits it across
# ``ai_chat_client.hpp`` / ``ai_chat_error.hpp`` and expresses several members
# idiomatically; the header walker therefore mis-routes the module names, leaks
# the C++ error getters/protected fields, and drops the method-less error
# subclasses + result structs (a struct with no public method is never
# registered). This projection folds the C++ idiom onto the oracle shape:
#
#   module  ai_chat.ai_chat_client / ai_chat.ai_chat_error  -> ai_chat.client
#   AIChatClient ctor              -> __init__       (already emitted)
#   close()  (genuine method)      -> close          (FOLDED: the client ships a
#                                     real ``close()`` -- a well-defined no-op,
#                                     since cpp-httplib is stateless -- mirroring
#                                     the python reference's explicit release and
#                                     the TS no-op close())
#   __aenter__ / __aexit__         -> FOLDED onto the C++ RAII lifecycle (verified
#                                     present below): the reference's async
#                                     context-manager IS a scoped-resource
#                                     capability, which C++ expresses natively as
#                                     RAII -- a scope-bound ``AIChatClient`` whose
#                                     destructor (enter=construction, exit=``~`` /
#                                     ``close()``) is the direct analogue of
#                                     ``async with`` / a ``using`` block. Same fold
#                                     dotnet takes via IDisposable/using; the
#                                     capability is real, only the idiom differs, so
#                                     this is ZERO omissions, not an impossible:.
#   url()  read-only getter        -> url            (FOLDED: Python exposes
#                                     ``self.url`` as a public __init__ attribute
#                                     that is ALSO a ctor param — caller-supplied
#                                     configuration the caller reads back. The
#                                     oracle's class-B2 rule records it, and the
#                                     C++ getter is that attribute's read.)
#   AIChatError code() / server_message()
#                                  -> code / message (FOLDED, same class-B2 rule:
#                                     both are ctor params the reference stores
#                                     publicly. ``has_code()`` stays a port
#                                     addition — it is the C++ spelling of
#                                     "``code`` is None", which a C++ ``int``
#                                     cannot express.)
#
# Every symbol is verified present in the header before it is emitted (abort-loud
# on a missing one) so the projection can never invent surface the port lost.
_AI_CHAT_CLIENT_METHODS = [
    "__aenter__",
    "__aexit__",
    "__init__",
    "chat",
    "close",
    "create_conversation",
    "delete",
    "end",
    "log",
    "summarize",
    "url",
]
# Method-less classes the oracle records in ai_chat.client: the base error
# carries __init__; every error subclass + result struct is bare.
_AI_CHAT_EMPTY_CLASSES = [
    "AuthenticationError",
    "ChatInProgressError",
    "ChatLog",
    "ChatResponse",
    "ConversationInfo",
    "ConversationNotFoundError",
    "RateLimitError",
    "SummaryError",
]


def _project_ai_chat(modules: dict, repo: Path) -> None:
    """Fold the C++ AI-Chat surface onto the oracle's ``signalwire.ai_chat.client``
    module. Verifies each symbol genuinely exists in the headers, then emits the
    canonical shape and drops the mis-routed native ai_chat modules."""
    client_hpp = repo / "include/signalwire/ai_chat/ai_chat_client.hpp"
    if not client_hpp.is_file():
        return  # port doesn't ship AI-Chat -- nothing to project
    txt = client_hpp.read_text(encoding="utf-8")

    def _require(pattern: str, what: str) -> None:
        if not re.search(pattern, txt):
            raise SystemExit(
                f"enumerate_surface: AI-Chat projection expected {what} in "
                f"{client_hpp.name} but it is gone -- fix the projection, do not "
                f"emit a symbol the port no longer has"
            )

    # The client class + the RAII lifecycle members the close/enter/exit fold
    # relies on (a public ctor and a declared destructor) must genuinely exist.
    _require(r"\bclass\s+AIChatClient\b", "class AIChatClient")
    _require(r"\bexplicit\s+AIChatClient\s*\(", "AIChatClient constructor")
    _require(r"~AIChatClient\s*\(", "AIChatClient destructor (RAII)")
    _require(r"\bvoid\s+close\s*\(", "AIChatClient::close (folds reference close)")
    # The wire-verb methods the oracle records (del() is the reserved-word
    # spelling of the reference ``delete``).
    for _m in ("create_conversation", "chat", "end", "log", "summarize"):
        _require(rf"\b{_m}\s*\(", f"AIChatClient::{_m}")
    _require(r"\bbool\s+del\s*\(", "AIChatClient::del (reference ``delete``)")
    # The ctor-param reads the oracle's class-B2 rule records.
    _require(r"\burl\s*\(\s*\)\s*const", "AIChatClient::url (reference ``self.url``)")

    # The base error + every typed subclass and result struct.
    _require(r"\bclass\s+AIChatError\b", "class AIChatError")
    _require(r"\bint\s+code\s*\(\s*\)\s*const", "AIChatError::code")
    _require(
        r"\bserver_message\s*\(\s*\)\s*const",
        "AIChatError::server_message (reference ``message``)",
    )
    for _c in _AI_CHAT_EMPTY_CLASSES:
        kind = r"class" if _c.endswith("Error") else r"struct"
        _require(rf"\b{kind}\s+{_c}\b", f"{kind} {_c}")

    # Drop the mis-routed native modules, then emit the single canonical one.
    for _native in (
        "signalwire.ai_chat.ai_chat_client",
        "signalwire.ai_chat.ai_chat_error",
    ):
        modules.pop(_native, None)

    client_mod = modules.setdefault(
        "signalwire.ai_chat.client", {"classes": {}, "functions": []}
    )
    client_mod["classes"]["AIChatClient"] = sorted(_AI_CHAT_CLIENT_METHODS)
    # ``server_message()`` is the C++ spelling of the reference's ``message``
    # attribute — ``message`` alone would collide with std::runtime_error::what()
    # semantics, so the port disambiguates the name. Rename, never omission.
    client_mod["classes"]["AIChatError"] = ["__init__", "code", "message"]
    for _c in _AI_CHAT_EMPTY_CLASSES:
        client_mod["classes"][_c] = []

    # The result DTOs (ChatResponse/ChatLog/ConversationInfo) are @dataclass-shaped
    # in the reference: their surface members are bare public data fields, which the
    # C++ port carries as struct fields the method-walker skipped. Emit those fields,
    # gated on the oracle's per-class set (drops port-internal scalars like
    # ConversationInfo.has_initial_message that the reference does not record). The
    # error subclasses stay method-less (they carry no oracle-recorded field).
    _emit_oracle_gated_fields(modules, "signalwire.ai_chat.client", client_hpp)


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
        if text[i : i + 2] == "/*":
            end = text.find("*/", i + 2)
            if end == -1:
                break
            # Preserve newlines inside the comment to keep line numbers sane.
            block = text[i : end + 2]
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

    def __init__(
        self, kind: str, name: str, brace_depth: int, visibility: str | None = None
    ):
        self.kind = kind  # "namespace" | "class" | "struct" | "anon"
        self.name = name
        self.brace_depth = brace_depth
        # Visibility applies to class/struct scopes. struct defaults to public.
        self.visibility = visibility


def parse_header(path: Path) -> list[tuple[str, str, list[str], list[str]]]:
    """Return list of (namespace_path, class_name, public_members).

    namespace_path is a "::"-joined string like "signalwire::agent".
    Members are already sorted.

    "Members" is methods PLUS public data-member fields. C++ spells a piece of
    caller-readable state either way — ``std::string body;`` on a struct and
    ``const std::string& body() const`` on a class are the SAME read surface, and
    the reference spells both as a plain ``self.body`` attribute. Emitting only
    methods made every public FIELD read as missing-port drift even though the
    member is right there (pom::Section's title/body/bullets, relay::Message's
    message_id/body/media/tags). Field-vs-accessor SHAPE is idiom, folded at
    emission (RULES §2). Field names are still intersected against the oracle
    downstream (``_gate_field_members``), so a port-internal public field the
    reference does not record never becomes invented surface.
    """
    raw = path.read_text(encoding="utf-8", errors="replace")
    text = strip_block_comments(raw)

    findings: list[tuple[str, str, list[str]]] = []

    scopes: list[Scope] = []
    brace_depth = 0

    # Per (namespace, class) collect methods
    # Key: (ns_path, class_name) -> ordered list of methods (dedup at end)
    collected: dict[tuple[str, str], list[str]] = {}
    # Same keying for public DATA-MEMBER fields, kept apart from methods so the
    # caller can oracle-gate them (a method the port declares is real surface;
    # a public field is only surface where the reference records the attribute).
    fields: dict[tuple[str, str], list[str]] = {}

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
            for _i, _p in enumerate(parts):
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
                scopes.append(
                    Scope(
                        "struct" if is_struct else "class",
                        class_name,
                        brace_depth,
                        visibility="public" if is_struct else "private",
                    )
                )
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
        if (
            scopes
            and scopes[-1].kind in ("class", "struct")
            and scopes[-1].visibility == "public"
            and brace_depth == scopes[-1].brace_depth + 1
        ):
            method_name = extract_method_name(code_line, scopes[-1].name)
            if method_name is not None:
                ns_path = "::".join(s.name for s in scopes if s.kind == "namespace")
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
            else:
                # Not a method — try a public DATA-MEMBER field. Same read
                # surface as an accessor (see the docstring); collected
                # separately because fields are oracle-gated downstream.
                field_name = extract_field_name(code_line)
                if field_name is not None:
                    ns_path = "::".join(s.name for s in scopes if s.kind == "namespace")
                    class_name = scopes[-1].name
                    emit_field = _METHOD_RENAMES.get(field_name, field_name)
                    fields.setdefault((ns_path, class_name), []).append(emit_field)

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
        findings.append((ns, cls, sorted(seen), sorted(set(fields.get((ns, cls), [])))))

    # A class with ONLY public fields and no methods is deliberately NOT
    # registered here. Registering it would put the port's internal
    # options/DTO structs (RelayConfig, ChatOptions, CreateParams, …) on the
    # surface as invented CLASS symbols — they exist because C++ needs a named
    # type where the reference passes kwargs. The field fold's job is to
    # complete a class the walker already found, not to add new classes.

    return findings


# A public data-member declaration at class-body depth: an optional
# ``[[attr]]`` / ``mutable`` / ``static`` / ``constexpr`` / ``const`` prefix, a
# type expression (possibly templated / ``::``-qualified / ref / pointer), the
# member identifier, an optional ``= init`` or ``{init}``, then ``;``.
# ``strip_line_comments`` has already removed any trailing comment.
_FIELD_DECL_RE = re.compile(
    r"^\s*(?P<quals>(?:mutable\s+|static\s+|constexpr\s+|const\s+|inline\s+)*)"
    r"(?P<type>[A-Za-z_][\w:]*(?:\s*<.*>)?(?:\s*(?:const|\*|&))*)"
    r"\s+(?P<name>[A-Za-z_]\w*)"
    r"\s*(?:=[^;]*|\{[^;]*\})?;\s*$"
)

# Type-expression keywords that mean the line is a declaration of something
# other than a data member (a nested type, an alias, a template).
_FIELD_TYPE_REJECT = {
    "using",
    "typedef",
    "friend",
    "template",
    "enum",
    "struct",
    "class",
    "union",
    "namespace",
    "return",
    "static_assert",
    "public",
    "private",
    "protected",
    "operator",
}


def extract_field_name(code_line: str) -> str | None:
    """If this line declares a public DATA MEMBER, return the member name.

    A field is caller-readable state exactly like a zero-arg accessor is; both
    are the reference's plain ``self.<name>`` attribute (see ``parse_header``).
    Rejects aliases/nested types/statics-as-constants and anything with a call
    paren (that is a method or an initializer-with-args, handled elsewhere).
    """
    stripped = code_line.strip()
    if not stripped or not stripped.endswith(";"):
        return None
    if stripped.startswith(("//", "#")):
        return None
    first = stripped.split(None, 1)[0].rstrip(":")
    if first in _FIELD_TYPE_REJECT:
        return None
    m = _FIELD_DECL_RE.match(code_line)
    if not m:
        return None
    # A ``(`` before the member name means a type expression like
    # ``std::function<void()> cb_;`` (fine — the paren is inside ``<>``) or a
    # declaration we should not treat as a field. Only reject when the paren
    # sits OUTSIDE any angle brackets.
    head = code_line[: m.start("name")]
    depth = 0
    for ch in head:
        if ch == "<":
            depth += 1
        elif ch == ">":
            depth = max(0, depth - 1)
        elif ch == "(" and depth == 0:
            return None
    name = m.group("name")
    # A trailing-underscore name is the port's PRIVATE-member convention; a
    # public one is an implementation detail that leaked, never surface.
    if name.endswith("_"):
        return None
    # An ALL-CAPS static is a constant, not instance state.
    if "static" in m.group("quals") and name.isupper():
        return None
    return name


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
    if name in {
        "if",
        "else",
        "for",
        "while",
        "do",
        "switch",
        "case",
        "return",
        "sizeof",
        "throw",
        "new",
        "delete",
        "typedef",
        "using",
        "template",
        "friend",
        "enum",
        "union",
        "struct",
        "class",
        "namespace",
    }:
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
        return (
            subprocess.check_output(
                ["git", "-C", str(repo), "rev-parse", "HEAD"],
                stderr=subprocess.DEVNULL,
            )
            .decode()
            .strip()
        )
    except Exception:
        return "N/A"


def _project_generated_rest_methods(modules: dict) -> None:
    """Union each generated REST resource's full oracle-declared method set
    (from the generator's rest_signatures.json) into its surface class list,
    materialising inherited base CRUD verbs the header walker can't see."""
    here = Path(__file__).resolve().parent
    gen_dir = (
        here.parent / "include" / "signalwire" / "rest" / "namespaces" / "generated"
    )
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
                f"generated_surface_map.json (regenerate the REST layer)"
            )
        mod_entry = modules.setdefault(mod, {"classes": {}, "functions": []})
        existing = set(mod_entry["classes"].get(cls, []))
        existing.add(canon)
        mod_entry["classes"][cls] = sorted(existing)


# Generated read-side payload structs (swml_verbs_generated, post_prompt_generated,
# swaig_request_generated, …) are METHOD-LESS PODs: one ``std::optional<T>`` data
# member per snake wire key. The reference SURFACE oracle records the B1 composition
# attributes on these classes (a self-only member holding an SDK class / json /
# optional-wrapped value) as bare members. The regex header walker only emits methods
# (things with ``(``), so those fields never reach the surface and read as missing-port
# DRIFT even though the field IS implemented. Project each public data-member field as
# a surface member — but ONLY the fields the oracle records for that class (never invent
# surface: the open ``extras`` member and scalar fields the reference does NOT expose are
# not projected). Field-vs-attribute SHAPE idiom, reconciled via the enumerator (RULES
# §2) — the surface analogue of the signature side's ``_project_gen_payload_getters``.
_GEN_FIELD_RE_SURF = re.compile(
    r"^\s+(?:\[\[[^\]]*\]\]\s*)?[A-Za-z_][\w:<>,\s]*?[>\w]\s+([A-Za-z_]\w*)\s*(?:=\s*[^;]+)?;\s*(//.*)?$"
)
_WIRE_KEY_RE_SURF = re.compile(r"wire key:\s*(\S+)")


def _gen_payload_struct_fields_surf(payload_dir: Path) -> dict[str, list[str]]:
    """``{StructName: [wire_field, …]}`` for the generated payload headers under
    ``payload_dir``. Honours the ``// wire key: <name>`` comment on reserved-word
    renames. Lines with ``(`` are skipped (method / initializer, not a data member).
    Identical parse to enumerate_signatures._gen_payload_struct_fields."""
    out: dict[str, list[str]] = {}
    if not payload_dir.is_dir():
        return out
    for hdr in sorted(payload_dir.glob("*.hpp")):
        src_txt = hdr.read_text(encoding="utf-8")
        for sm in re.finditer(r"struct\s+(\w+)\s*\{(.*?)\n\};", src_txt, re.S):
            cls, body = sm.group(1), sm.group(2)
            fields: list[str] = []
            for line in body.splitlines():
                if "(" in line:
                    continue
                m = _GEN_FIELD_RE_SURF.match(line)
                if not m:
                    continue
                ident, comment = m.group(1), m.group(2) or ""
                wk = _WIRE_KEY_RE_SURF.search(comment)
                fields.append(wk.group(1) if wk else ident)
            if fields:
                out.setdefault(cls, []).extend(fields)
    return out


def _load_reference_surface() -> dict:
    """The reference ``python_surface.json`` (the surface oracle the diff compares
    against). Empty dict if unresolvable — the projection then no-ops (safe)."""
    psdk = _resolve_psdk()
    path = psdk / "python_surface.json"
    if not path.is_file():
        return {}
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return {}


def _fold_setters(module: str, cls: str, members: list[str]) -> list[str]:
    """Collapse a ``set_<x>`` writer onto ``<x>`` when the reference oracle
    records ``<x>`` as a member of the SAME class.

    The reference spells caller-supplied configuration as a plain public
    attribute: ``self.venue_name = venue_name`` is BOTH the read and the write.
    C++ splits that into an accessor + a fluent setter. Both spellings drive the
    same capability, so the setter is IDIOM and folds at emission
    (ALLOWLIST_DISCIPLINE §0) — it is not additional surface.

    Same-class gating is what keeps this honest: a ``set_x`` whose ``x`` the
    reference does not have on this class is left alone (it stays visible as
    whatever it really is), so the fold can never launder genuinely port-only
    surface, and it never performs the cross-class fold RULES §4 forbids."""
    ref = _load_reference_surface()
    if not ref:
        return members
    ref_members = ref.get("modules", {}).get(module, {}).get("classes", {}).get(cls)
    allowed: set[str] = set()
    if ref_members:
        allowed = set(
            ref_members
            if isinstance(ref_members, list)
            else ref_members.get("members", ref_members)
        )
    if module in _FAMILY_GATE_MODULES:
        allowed |= _agentbase_family_members(ref)
    if not allowed:
        return members
    out: set[str] = set()
    for m in members:
        # The reference may itself expose a real ``set_<x>`` METHOD alongside
        # the attribute (FunctionResult.set_response, AgentBase
        # .set_native_functions). Those are surface in their own right — never
        # fold a name the oracle records verbatim.
        if m.startswith("set_") and m not in allowed and m[4:] in allowed:
            out.add(m[4:])
        else:
            out.add(m)
    return sorted(out)


def _gate_field_members(module: str, cls: str, decl_fields: list[str]) -> list[str]:
    """Filter a class's public data-member fields down to the ones the reference
    oracle actually records for that class.

    A public FIELD is caller-readable state, the same as a zero-arg accessor —
    but unlike a declared method (which is unambiguously the port's surface), a
    field is often incidental (a struct's working buffer, a helper's cached
    handle). Intersecting with the oracle folds the field-vs-attribute idiom
    exactly where the reference has the attribute and drops it everywhere else,
    so this never invents surface (RULES §2 / ALLOWLIST_DISCIPLINE §0a).

    For an ``agentbase-family`` class the diff collapses the ``module.Class``
    prefix away entirely, so gate against the union of every family class's
    members rather than this one class's."""
    if not decl_fields:
        return []
    ref = _load_reference_surface()
    if not ref:
        return []
    ref_modules = ref.get("modules", {})
    ref_members = ref_modules.get(module, {}).get("classes", {}).get(cls)
    allowed: set[str] = set()
    if ref_members:
        allowed = set(
            ref_members
            if isinstance(ref_members, list)
            else ref_members.get("members", ref_members)
        )
    if module in _FAMILY_GATE_MODULES:
        allowed |= _agentbase_family_members(ref)
    return [f for f in decl_fields if f in allowed]


# Modules whose classes the diff folds into the `agentbase-family` token: a
# member declared on the C++ AgentBase may be recorded by the reference on ANY
# family class (a mixin), so the field gate must consult the whole family.
_FAMILY_GATE_MODULES = frozenset({"signalwire.core.agent_base"})
_FAMILY_MIXIN_PREFIX = "signalwire.core.mixins."


def _agentbase_family_members(ref: dict) -> set[str]:
    """Every member the reference records on an AgentBase-family class (AgentBase
    itself plus the mixins it multiply-inherits). Mirrors the diff tool's
    ``_fold_agentbase_family`` membership rule."""
    out: set[str] = set()
    for mod, entry in ref.get("modules", {}).items():
        if mod != "signalwire.core.agent_base" and not mod.startswith(
            _FAMILY_MIXIN_PREFIX
        ):
            continue
        for members in entry.get("classes", {}).values():
            out |= set(
                members
                if isinstance(members, list)
                else members.get("members", members)
            )
    return out


def _project_gen_payload_members(modules: dict) -> None:
    """Emit each generated payload struct's public data-member fields as surface
    members, intersected with the fields the reference oracle records for that class.

    A field only surfaces if the oracle lists it as a member of the same class — so
    the port's field-idiom folds exactly onto the reference's B1 composition attributes,
    never inventing a member the reference lacks (the open ``extras`` member and any
    scalar field the reference does not expose are dropped)."""
    ref = _load_reference_surface()
    if not ref:
        return
    ref_modules = ref.get("modules", {})
    for ns, module in GENERATED_PAYLOAD_NS.items():
        ref_classes = ref_modules.get(module, {}).get("classes", {})
        if not ref_classes:
            continue
        payload_dir = _INCLUDE_ROOT / Path(*ns.split("::"))
        struct_fields = _gen_payload_struct_fields_surf(payload_dir)
        if not struct_fields:
            continue
        mod_entry = modules.setdefault(module, {"classes": {}, "functions": []})
        for cls, fields in struct_fields.items():
            ref_members = ref_classes.get(cls)
            if not ref_members:
                continue
            ref_set = set(
                ref_members
                if isinstance(ref_members, list)
                else ref_members.get("members", ref_members)
            )
            present = [f for f in fields if f in ref_set]
            if not present:
                continue
            existing = mod_entry["classes"].get(cls, [])
            mod_entry["classes"][cls] = sorted(set(existing) | set(present))


_CLIENT_TREE_MODULE = "signalwire.rest.namespaces._client_tree_generated"


def _project_client_tree_members(modules: dict) -> None:
    """Emit each generated namespace container's resource-accessor data members
    (``FabricAddresses addresses;`` -> ``addresses``) as surface members, intersected
    with the fields the oracle records for that container class.

    The reference records these B1 composition attributes (a container field holding an
    SDK resource class) as members. The C++ container declares them as ``<Type> <member>;``
    public data members; the regex header walker only picks up ctor-init-list entries
    (and misses the first, e.g. ``addresses``), so parse the real data members. Intersect
    with the oracle so nothing not in the reference is invented."""
    ref = _load_reference_surface()
    if not ref:
        return
    ref_classes = ref.get("modules", {}).get(_CLIENT_TREE_MODULE, {}).get("classes", {})
    if not ref_classes:
        return
    gen_dir = _INCLUDE_ROOT / "signalwire" / "rest" / "namespaces" / "generated"
    if not gen_dir.is_dir():
        return
    mod_entry = modules.setdefault(
        _CLIENT_TREE_MODULE, {"classes": {}, "functions": []}
    )
    for hdr in sorted(gen_dir.glob("*Namespace.hpp")):
        srctxt = hdr.read_text(encoding="utf-8")
        m = re.search(r"(?:class|struct) (\w+Namespace)\s*\{(.*?)\n\};", srctxt, re.S)
        if not m:
            continue
        cls, body = m.group(1), m.group(2)
        ref_members = ref_classes.get(cls)
        if not ref_members:
            continue
        ref_set = set(
            ref_members
            if isinstance(ref_members, list)
            else ref_members.get("members", ref_members)
        )
        # public data members: ``<TypeName> <member>;`` at 2-space indent.
        fields = re.findall(r"^\s{2}([A-Z]\w+)\s+([a-z_]\w*);", body, re.M)
        present = [mem for _t, mem in fields if mem in ref_set]
        if not present:
            continue
        existing = mod_entry["classes"].get(cls, [])
        mod_entry["classes"][cls] = sorted(set(existing) | set(present))


# ---------------------------------------------------------------------------
# Public data-member field projection (field-idiom fold onto the oracle)
# ---------------------------------------------------------------------------
# The regex header walker only registers members with a ``(`` (methods). Several
# reference classes are @dataclass-shaped: their surface members are bare public
# DATA FIELDS (``std::string call_state;``), which the walker never emits. The
# reference oracle now records those fields (relay Event dataclasses, the AI-Chat
# result DTOs, RequestOptions). Fold the C++ field-idiom onto the oracle: parse a
# named struct/class's public data-member fields and emit exactly the ones the
# oracle records for that class — never a port-internal helper the reference lacks
# (the intersection is the guard). Shape idiom reconciled via the enumerator
# (RULES §2), the surface analogue of the signature side's field projection.

# ``struct/class Name[ : bases] { ... };`` — captures the body of a named struct
# even when it inherits (``: public RelayEvent``), which the generated-payload
# parser's ``struct Name {`` regex does not handle. Non-greedy to the matching
# ``\n};`` at column 0.
_NAMED_STRUCT_RE = re.compile(
    r"(?:struct|class)\s+(\w+)\s*(?::[^{]+)?\{(.*?)\n\};", re.S
)


def _struct_public_fields(header_txt: str) -> dict[str, list[str]]:
    """``{StructName: [field, …]}`` for every named struct/class in ``header_txt``.

    Only bare data-member fields are returned (lines with ``(`` — methods,
    initializers — are skipped). Honours a ``// wire key: <name>`` comment on a
    reserved-word rename. Visibility is not tracked; the caller intersects with
    the oracle, so any private/helper field the reference does not record is
    dropped anyway."""
    out: dict[str, list[str]] = {}
    for sm in _NAMED_STRUCT_RE.finditer(header_txt):
        cls, body = sm.group(1), sm.group(2)
        fields: list[str] = []
        for line in body.splitlines():
            # A method has ``(`` BEFORE the first ``=``/``;``; a data-member field
            # may carry ``(`` only inside its initializer (``json x = json::object();``).
            # Discriminating on paren-position keeps the ``json …`` payload fields
            # (dropped by a naive ``if "(" in line``) while still skipping methods.
            eq, sc = line.find("="), line.find(";")
            bounds = [i for i in (eq, sc) if i != -1]
            boundary = min(bounds) if bounds else len(line)
            lp = line.find("(")
            if lp != -1 and lp < boundary:
                continue
            m = _GEN_FIELD_RE_SURF.match(line)
            if not m:
                continue
            ident, comment = m.group(1), m.group(2) or ""
            wk = _WIRE_KEY_RE_SURF.search(comment)
            fields.append(wk.group(1) if wk else ident)
        if fields:
            out.setdefault(cls, []).extend(fields)
    return out


def _emit_oracle_gated_fields(modules: dict, module: str, header: Path) -> None:
    """Union each struct's public data-member fields into ``module``'s class
    entries, intersected with the fields the reference oracle records for that
    class. A field surfaces only if the oracle lists it for the same class, so
    the port's field-idiom folds exactly onto the reference dataclass fields and
    never invents surface the reference lacks.

    ``__init__`` folds the same way. The reference spells these classes as
    ``@dataclass``es (and, for the credential carriers, as structural fillers with
    no source file at all), so their constructor is SYNTHESIZED rather than
    written as a ``def`` — porting-sdk 8828dd2 taught the surface oracle to record
    it, matching what the signature oracle always did. The C++ counterparts are
    aggregates with no user-declared constructor, which is the same contract:
    ``std::is_default_constructible`` is true for every one of them (verified for
    BasicCredentials/BearerCredentials/RelayEvent/PlayEvent/RequestOptions), and
    C++ aggregate-initializes them by field name. So the member is TRUE of the
    port, not paperwork to clear a gate. It stays oracle-gated like every other
    member here: if the reference does not record a constructor for this class,
    the port does not claim one."""
    if not header.is_file():
        return
    ref = _load_reference_surface()
    ref_classes = ref.get("modules", {}).get(module, {}).get("classes", {})
    if not ref_classes:
        return
    struct_fields = _struct_public_fields(header.read_text(encoding="utf-8"))
    if not struct_fields:
        return
    mod_entry = modules.setdefault(module, {"classes": {}, "functions": []})
    for cls, fields in struct_fields.items():
        ref_members = ref_classes.get(cls)
        if not ref_members:
            continue
        ref_set = set(
            ref_members
            if isinstance(ref_members, list)
            else ref_members.get("members", ref_members)
        )
        present = [f for f in fields if f in ref_set]
        if "__init__" in ref_set:
            present.append("__init__")
        if not present:
            continue
        existing = mod_entry["classes"].get(cls, [])
        mod_entry["classes"][cls] = sorted(set(existing) | set(present))


def _project_relay_event_fields(modules: dict, repo: Path) -> None:
    """Emit the public data-member fields of the typed relay Event structs
    (``typed_events.hpp``) as surface members, gated on the oracle's per-class
    member set for ``signalwire.relay.event``. The structs already surface via
    ``from_payload``; this adds the @dataclass payload fields (``call_state``,
    ``control_id``, ``message_state``, …) the walker skipped."""
    header = repo / "include/signalwire/relay/typed_events.hpp"
    _emit_oracle_gated_fields(modules, "signalwire.relay.event", header)


def _project_request_options_fields(modules: dict, repo: Path) -> None:
    """Emit RequestOptions' public ``std::optional<…>`` data-member fields
    (``timeout``/``retries``/``retry_on_status``/``retry_backoff``) as surface
    members, gated on the oracle's ``signalwire.rest._request_options`` set. The
    ``abort_signal`` pointer + ``merge`` method surface via _ensure_member / the
    method walker; these four optional fields are what the walker skipped."""
    header = repo / "include/signalwire/rest/request_options.hpp"
    _emit_oracle_gated_fields(modules, "signalwire.rest._request_options", header)


def _project_credential_carrier_fields(modules: dict, repo: Path) -> None:
    """Emit the credential carriers' public data-member fields as surface members,
    gated on the oracle's ``signalwire.core.auth_handler`` per-class set.

    ``BasicCredentials{username,password}`` and ``BearerCredentials{scheme,
    credentials}`` are pure data records: the reference spells them as FastAPI
    pydantic models whose whole surface is their fields, so griffe records the
    fields and no ``__init__``. The C++ carriers are the same shape — two
    ``std::string`` members, zero methods — which is precisely what the regex
    method-walker skips (it only registers a class once it sees a public method),
    so both classes were absent from ``port_surface.json`` entirely. The oracle
    gate is what makes this a fold rather than invented surface: a field appears
    only if the reference records it on the same class."""
    header = repo / "include/signalwire/core/auth_handler.hpp"
    _emit_oracle_gated_fields(modules, "signalwire.core.auth_handler", header)


def build_native_names(include_dir: Path) -> dict:
    """Return the port's REAL declared member names, verbatim, BEFORE any fold.

    ``build_snapshot`` deliberately reshapes the emitted surface into the
    reference's spelling so the parity diff compares equal: ``set_route`` folds
    onto ``route`` (``_fold_setters``), and a public field is dropped unless the
    oracle records the attribute (``_gate_field_members``). That folded snapshot
    is the right input for the DRIFT diff and the WRONG input for the doc gates
    — a C++ example that calls ``svc.set_route("/demo")`` is correct, compiling
    code naming a method that genuinely exists, but the folded snapshot no
    longer contains ``set_route``, so DOC-AUDIT reads it as a phantom.

    This sidecar is the resolution the doc gates already know how to consume:
    ``scripts/suites/_doc_audit.py`` passes ``port_surface_native.json`` to
    ``audit_docs.py --native-names`` for ANY port that ships one, and
    ``audit_docs.load_native_names`` unions it with the folded surface. So the
    parity diff keeps seeing the reference's spelling while the doc gates see
    what a caller can actually type. Emitted in the FLAT ``{"native_names":
    [...]}`` shape (dotnet's).

    Nothing here gates on the oracle: these are the port's own declarations, and
    they are used only to RESOLVE doc references, never to claim surface."""
    global _INCLUDE_ROOT
    _INCLUDE_ROOT = include_dir.parent
    names: set[str] = set()
    patterns = ("**/*.hpp", "**/*.h")
    header_files: list[Path] = []
    for p in patterns:
        header_files.extend(sorted(include_dir.glob(p)))
    for path in header_files:
        try:
            findings = parse_header(path)
        except Exception as e:
            # Same reasoning as build_snapshot: this feeds
            # port_surface_native.json, which the doc gates use to resolve
            # references. A skipped header makes real symbols look undefined.
            raise RuntimeError(
                f"enumerate_surface: failed to parse {path}: {e}. "
                "Refusing to emit a native-name snapshot that silently omits "
                "this header's symbols."
            ) from e
        for _ns_path, class_name, methods, decl_fields in findings:
            names.add(class_name)
            names.update(methods)
            names.update(decl_fields)
    return {"native_names": sorted(names)}


def build_snapshot(repo: Path, include_dir: Path) -> dict:
    global _INCLUDE_ROOT
    # GENERATED_PAYLOAD_NS keys begin at ``signalwire::``; the header for
    # ``signalwire::core::foo`` lives at ``<include>/signalwire/core/foo``. include_dir
    # is ``<include>/signalwire`` by default, so the base is its parent.
    _INCLUDE_ROOT = include_dir.parent
    modules: dict[str, dict] = {}

    # Walk every .hpp/.h under include/
    patterns = ("**/*.hpp", "**/*.h")
    header_files: list[Path] = []
    for p in patterns:
        header_files.extend(sorted(include_dir.glob(p)))

    for path in header_files:
        try:
            findings = parse_header(path)
        except Exception as e:
            # ABORT, do not skip. This function produces port_surface.json, which
            # the SURFACE-DIFF / DRIFT gates read as ground truth for what the
            # port exposes. Swallowing a parse failure silently DROPS every
            # symbol in that header, and the gate then reports them as omissions
            # the port is missing -- pointing the blame at the port instead of at
            # this parser. It used to `print(warning); continue`, which a gate
            # that only inspects the exit code cannot see.
            raise RuntimeError(
                f"enumerate_surface: failed to parse {path}: {e}. "
                "Refusing to emit a surface snapshot that silently omits this "
                "header's symbols."
            ) from e

        for ns_path, class_name, methods, decl_fields in findings:
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
            # Public DATA-MEMBER fields are the same read surface as an
            # accessor, but only where the reference records the attribute —
            # gate them on the oracle's member set for this class so a
            # port-internal public field never becomes invented surface.
            gated_fields = _gate_field_members(emit_mod, emit_class, decl_fields)
            # If the class was already seen in another header (unlikely but
            # possible for split public/impl headers), merge the method list.
            existing = mod_entry["classes"].get(emit_class, [])
            merged = sorted(set(existing) | set(methods) | set(gated_fields))
            # Fold ``set_<x>`` onto ``<x>`` where the reference records ``<x>``
            # on this class — writer/attribute shape idiom (see _fold_setters).
            mod_entry["classes"][emit_class] = _fold_setters(
                emit_mod, emit_class, merged
            )

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

    # ``PromptManager.agent`` / ``ToolRegistry.agent``: the reference constructs
    # each helper with a back-reference to the owning agent
    # (``PromptManager(self)`` / ``ToolRegistry(self)``, stored as ``self.agent``
    # — a ctor param the oracle's class-B2 rule records). C++ does not extract
    # the helpers as separate OBJECTS at all: their methods are declared
    # directly on AgentBase / swml::Service, which is exactly what the
    # projections above encode. When the manager and its agent are the SAME
    # object, the back-reference is ``*this`` — reaching the agent from the
    # manager is as available in C++ as in Python, it is simply already in hand.
    # Emit it only where the projection actually produced the merged class, so
    # this can never surface a member for a class the port does not have.
    for _mod, _cls in (
        ("signalwire.core.agent.prompt.manager", "PromptManager"),
        ("signalwire.core.agent.tools.registry", "ToolRegistry"),
    ):
        _members = modules.get(_mod, {}).get("classes", {}).get(_cls)
        if _members:
            modules[_mod]["classes"][_cls] = sorted(set(_members) | {"agent"})

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
    _base = modules.setdefault(
        "signalwire.rest._base", {"classes": {}, "functions": []}
    )
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

    # AI-Chat surface: fold the split ai_chat_client/ai_chat_error headers +
    # C++ RAII/getter idioms onto the oracle's single signalwire.ai_chat.client
    # module (see _project_ai_chat).
    _project_ai_chat(modules, repo)

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
    _action_own = (
        modules.get("signalwire.relay.action", {}).get("classes", {}).get("Action", [])
    )
    if _action_own:
        # ``control_id`` joins the projected set: it is a ctor param the
        # reference stores publicly (``self.control_id``), which the oracle's
        # class-B2 rule now records, and the C++ Action has the accessor.
        # ``completed`` likewise: the reference sets ``self.completed = False``
        # in ``__init__`` and flips it True on completion — a caller-observable
        # VALUE (class-B2), and the C++ Action already exposes ``completed()``
        # (``is_done()`` is the C++-idiom alias that delegates to it).
        proj = sorted(
            {"__init__"}
            | {
                m
                for m in (
                    "is_done",
                    "wait",
                    "result",
                    "control_id",
                    "call",
                    "completed",
                )
                if m in _action_own
            }
        )
        modules.setdefault("signalwire.relay.call", {"classes": {}, "functions": []})
        modules["signalwire.relay.call"]["classes"]["Action"] = proj
        # ``call`` is REFERENCE surface (``relay.call.Action.call``), projected
        # above onto the class where the reference declares it. Emitting it a
        # SECOND time under the port's native ``relay.action`` module would make
        # the same member read as a port addition there. One member, one home.
        modules["signalwire.relay.action"]["classes"]["Action"] = [
            m for m in _action_own if m != "call"
        ]

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
        call_mod = modules.setdefault(
            "signalwire.relay.call", {"classes": {}, "functions": []}
        )
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

    # PromptObjectModel.sections / Section.subsections are PUBLIC ``std::vector<Section>``
    # data members in pom.hpp; the reference records them as the B1 composition attribute.
    # Same public collection, same name — surface it (the regex walker skips data members).
    _ensure_member("signalwire.pom.pom", "PromptObjectModel", "sections")
    _ensure_member("signalwire.pom.pom", "Section", "subsections")

    # RequestOptions exposes the cooperative-cancellation ``abort_signal`` as a public
    # data member (``std::atomic<bool>* abort_signal``); the reference records it as the
    # B1 cancellation attribute. Same surface, same name — the regex walker skips data
    # members, so surface it here (idiom via the enumerator, RULES §2).
    _ensure_member("signalwire.rest._request_options", "RequestOptions", "abort_signal")

    # SWAIGFunction exposes ``operator()`` + ``call`` -> Python ``__call__``.
    _ensure_member("signalwire.core.swaig_function", "SWAIGFunction", "__call__")
    # SkillRegistry is a singleton (private ctor); SkillBase has a defaulted
    # protected ctor. Both genuinely construct — surface ``__init__``.
    _ensure_member("signalwire.skills.registry", "SkillRegistry", "__init__")
    _ensure_member("signalwire.core.skill_base", "SkillBase", "__init__")

    # Generated read-side payload structs: project their public data-member fields
    # as surface members (intersected with the oracle's recorded B1 composition attrs).
    _project_gen_payload_members(modules)

    # Generated namespace containers: project their resource-accessor data members
    # as surface members (intersected with the oracle's recorded B1 composition attrs).
    _project_client_tree_members(modules)

    # Typed relay Event structs: project their @dataclass payload fields as surface
    # members (intersected with the oracle's signalwire.relay.event per-class set).
    _project_relay_event_fields(modules, repo)

    # RequestOptions: project its public std::optional<…> data-member fields as
    # surface members (intersected with the oracle's _request_options set).
    _project_request_options_fields(modules, repo)

    # Credential carriers: project their public data-member fields as surface
    # members (intersected with the oracle's signalwire.core.auth_handler set).
    _project_credential_carrier_fields(modules, repo)

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
        "--include-dir",
        type=Path,
        default=default_include,
        help=f"Header root to walk (default: {default_include})",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=default_output,
        help=f"Where to write JSON (default: {default_output})",
    )
    parser.add_argument(
        "--stdout",
        action="store_true",
        help="Print JSON to stdout instead of writing --output",
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="Compare against the file at --output; exit 1 on drift",
    )
    args = parser.parse_args(argv)

    if not args.include_dir.is_dir():
        print(f"error: include dir not found: {args.include_dir}", file=sys.stderr)
        return 1

    snapshot = build_snapshot(repo, args.include_dir)
    rendered = json.dumps(snapshot, indent=2, sort_keys=True) + "\n"

    # The pre-fold native-name sidecar the doc gates resolve against (see
    # build_native_names). Written beside --output so it tracks it.
    native = build_native_names(args.include_dir)
    native_rendered = json.dumps(native, indent=2, sort_keys=True) + "\n"
    native_output = args.output.with_name("port_surface_native.json")

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
        if not native_output.is_file():
            print(f"error: {native_output} does not exist", file=sys.stderr)
            return 1
        if strip_meta(native_rendered) != strip_meta(
            native_output.read_text(encoding="utf-8")
        ):
            print(
                "DRIFT: port_surface_native.json is stale relative to headers.\n"
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
        native_output.write_text(native_rendered, encoding="utf-8")
        print(
            f"wrote {args.output} "
            f"({len(snapshot['modules'])} modules, "
            f"{sum(len(m['classes']) for m in snapshot['modules'].values())} classes, "
            f"{sum(sum(len(ms) for ms in m['classes'].values()) for m in snapshot['modules'].values())} methods)",
            file=sys.stderr,
        )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
