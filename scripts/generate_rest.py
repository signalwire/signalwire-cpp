#!/usr/bin/env python3
"""Generate the SignalWire REST namespace resource layer for signalwire-cpp.

The C++ realization of porting-sdk/REST_GENERATOR_RULES.md — the language-neutral
contract of the REST resource generator (bases, x-sdk-resource markup, path
composition, command-dispatch, set_methods, cross-spec client-tree placement,
fail-loud invariants). The PHP generator (signalwire-php/scripts/generate_rest.py)
and Go/TS are the proven templates; this mirrors their structure in C++17.

Inputs (resolved from $PORTING_SDK or the adjacent ../porting-sdk):
    rest-apis/<ns>/openapi.yaml       (+ x-sdk-* markup)
    rest-apis/x-sdk-bases.yaml        (shared base method-sets)
    rest-apis/fabric/x-sdk-bases.yaml (FabricResource)

Outputs: C++ headers under include/signalwire/rest/namespaces/generated/ — one
.hpp per generated resource class (each class EXTENDS a hand-written base:
CrudResource/ReadResource/FabricResource/BaseResource — those stay hand-written
in http_client.hpp/base shims), plus one client-tree container .hpp per namespace
group and a resource_tree.hpp the hand RestClient composes. The generator emits
ONLY the per-resource classes, their declared/command/set methods, the container
tree, and the §4-composed base paths baked into each constructor.

C++ IDIOM (PORT_PHILOSOPHY_CPP, L13): the closed typed param set of an operation/
command/set method is emitted as a NAMED options-struct (`<Class><Method>Params`
with `std::optional<T>` optionals + member-init) + a trailing `json extras` map,
NOT flat positionals. Required fields are non-optional struct members; optionals
are `std::optional<T>`. Distinct int/double (no numeric-monotype). Classes are
named by `x-sdk-resource.name` VERBATIM (the Python oracle canonical names —
AiAgents, SipEndpoints, VideoRooms, DatasphereDocuments, …). A spec field whose
name is a C++ keyword / can't be a C++ identifier → reported as an ADAPTER RENAME
(not omitted); the wire key is preserved.

Usage:
    python3 scripts/generate_rest.py                 # write into the repo tree
    python3 scripts/generate_rest.py --check         # GEN-FRESH: fail if stale
    python3 scripts/generate_rest.py --out DIR       # scratch: emit flat into DIR
    python3 scripts/generate_rest.py --dump-classes  # print emitted class set
    python3 scripts/generate_rest.py --dump-paths    # print computed base paths
"""
from __future__ import annotations

import argparse
import json as _json
import os
import re
import sys
from pathlib import Path

try:
    import yaml
except ImportError:  # pragma: no cover
    sys.stderr.write("generate_rest.py requires PyYAML (pip install pyyaml)\n")
    raise

sys.path.insert(0, str(Path(__file__).resolve().parent))
from _cpp_fmt import format_generated_cpp  # noqa: E402


# The 12 real REST spec directories (registry has no own dir — its resources
# live inside relay-rest via namespace: registry; swml-webhooks is types-only).
SPEC_DIRS = [
    "relay-rest", "fabric", "calling", "video", "datasphere",
    "logs", "message", "voice", "fax", "project", "chat", "pubsub",
]

# C++ reserved words (C++17 keywords) that cannot be an identifier. A body/param
# field whose sanitised name collides gets a trailing ``_`` (the wire key is
# preserved in the emitted body); such renames are REPORTED as adapter renames.
CPP_KEYWORDS = {
    "alignas", "alignof", "and", "and_eq", "asm", "auto", "bitand", "bitor",
    "bool", "break", "case", "catch", "char", "char8_t", "char16_t", "char32_t",
    "class", "compl", "concept", "const", "consteval", "constexpr", "constinit",
    "const_cast", "continue", "co_await", "co_return", "co_yield", "decltype",
    "default", "delete", "do", "double", "dynamic_cast", "else", "enum",
    "explicit", "export", "extern", "false", "float", "for", "friend", "goto",
    "if", "inline", "int", "long", "mutable", "namespace", "new", "noexcept",
    "not", "not_eq", "nullptr", "operator", "or", "or_eq", "private",
    "protected", "public", "register", "reinterpret_cast", "requires", "return",
    "short", "signed", "sizeof", "static", "static_assert", "static_cast",
    "struct", "switch", "template", "this", "thread_local", "throw", "true",
    "try", "typedef", "typeid", "typename", "union", "unsigned", "using",
    "virtual", "void", "volatile", "wchar_t", "while", "xor", "xor_eq",
}

# Renames recorded during a build (field -> sanitised ident), reported at exit.
_RENAMES: list[tuple[str, str, str, str]] = []  # (class, method, wire_field, ident)


# ---------------------------------------------------------------------------
# Resolution.
# ---------------------------------------------------------------------------

def resolve_porting_sdk() -> Path:
    env = os.environ.get("PORTING_SDK")
    if env and (Path(env) / "rest-apis").is_dir():
        return Path(env).resolve()
    here = Path(__file__).resolve()
    for parent in here.parents:
        cand = parent.parent / "porting-sdk"
        if (cand / "rest-apis").is_dir():
            return cand.resolve()
    raise SystemExit("generate_rest.py: porting-sdk not found (set $PORTING_SDK or clone adjacent)")


def repo_root() -> Path:
    return Path(__file__).resolve().parents[1]


# ---------------------------------------------------------------------------
# SDK-surface policy overlay (the single source; NOT wire truth).
# ---------------------------------------------------------------------------
# porting-sdk/rest-apis/x-sdk-overlay.yaml is the ONE authoritative place that says which
# spec fields the SDKs hide (dropped from the surface) or deprecate (emitted-but-flagged).
# It is a policy overlay, NOT markup in the (often vendored) specs, so the same field is
# governed once and applied wherever it surfaces (schema.json $defs/AIParams + the
# calling/fabric REST projections + the SWML-verb structs). Each rule is (field, scope-or-
# None): scope=None matches in every schema; scope="SchemaName" matches only inside the SPEC
# schema (the $defs / components.schemas key) of that name — NOT the C++ struct name we emit.
_overlay_cache: "dict[str, set[tuple[str, str | None]]] | None" = None


def _load_overlay() -> "dict[str, set[tuple[str, str | None]]]":
    global _overlay_cache
    if _overlay_cache is None:
        def rules(key: str, data: dict) -> "set[tuple[str, str | None]]":
            out: set[tuple[str, str | None]] = set()
            for entry in data.get(key) or []:
                if isinstance(entry, dict) and entry.get("field"):
                    out.add((entry["field"], entry.get("scope")))
            return out
        path = resolve_porting_sdk() / "rest-apis" / "x-sdk-overlay.yaml"
        data = yaml.safe_load(path.read_text()) if path.is_file() else {}
        data = data or {}
        _overlay_cache = {"hidden": rules("hidden", data), "deprecated": rules("deprecated", data)}
    return _overlay_cache


def _overlay_match(rules: "set[tuple[str, str | None]]", field: str, schema_name: "str | None") -> bool:
    # A rule matches when its field equals `field` AND (it is unscoped OR its scope equals
    # the containing SPEC schema name). `schema_name` is the schema's name as it appears in
    # the spec (the $defs / components.schemas key) — NOT the C++ struct name we later emit —
    # so the scope value is identical across all ports.
    for rf, scope in rules:
        if rf == field and (scope is None or scope == schema_name):
            return True
    return False


def _overlay_hidden(field: str, schema_name: "str | None" = None) -> bool:
    return _overlay_match(_load_overlay()["hidden"], field, schema_name)


def _overlay_deprecated(field: str, schema_name: "str | None" = None) -> bool:
    return _overlay_match(_load_overlay()["deprecated"], field, schema_name)


# ---------------------------------------------------------------------------
# Base loading (x-sdk-bases; §2).
# ---------------------------------------------------------------------------

def load_bases(psdk: Path) -> dict[str, list[str]]:
    raw = yaml.safe_load((psdk / "rest-apis" / "x-sdk-bases.yaml").read_text())
    bases = dict(raw.get("x-sdk-bases") or {})
    fab = psdk / "rest-apis" / "fabric" / "x-sdk-bases.yaml"
    if fab.is_file():
        bases.update((yaml.safe_load(fab.read_text()).get("x-sdk-bases") or {}))

    def resolve(name: str, seen: set[str]) -> list[str]:
        if name in seen:
            raise SystemExit(f"x-sdk-bases: cyclic extends at {name}")
        if name not in bases:
            raise SystemExit(f"x-sdk-bases: undefined base {name!r}")
        seen = seen | {name}
        methods: list[str] = []
        ext = bases[name].get("extends")
        if ext:
            methods.extend(resolve(ext, seen))
        methods.extend(list((bases[name].get("methods") or {}).keys()))
        return methods

    return {name: resolve(name, set()) for name in bases}


# ---------------------------------------------------------------------------
# Spec model.
# ---------------------------------------------------------------------------

class Spec:
    def __init__(self, name: str, doc: dict):
        self.name = name
        self.doc = doc
        self.server_path = _url_path(doc["servers"][0]["url"])
        if self.server_path != "/" and self.server_path.endswith("/"):
            raise SystemExit(f"{name}: servers[0].url path {self.server_path!r} has a trailing slash")
        self.namespace_attr = (doc.get("x-sdk-namespace") or {}).get("attr") or ""
        self.ops: dict[str, tuple[str, str, bool]] = {}
        self.op_body: dict[str, dict] = {}
        for path, item in (doc.get("paths") or {}).items():
            for verb in ("get", "post", "put", "patch", "delete"):
                o = item.get(verb)
                if o and o.get("operationId"):
                    self.ops[o["operationId"]] = (verb, path, bool(o.get("requestBody")))
                    body = o.get("requestBody") or {}
                    content = body.get("content") or {}
                    media = content.get("application/json") or (next(iter(content.values())) if content else {})
                    self.op_body[o["operationId"]] = (media or {}).get("schema") or {}
        self.schemas = ((doc.get("components") or {}).get("schemas")) or {}

    def resources(self) -> list[tuple[str, dict]]:
        out = []
        for path, item in (self.doc.get("paths") or {}).items():
            r = item.get("x-sdk-resource")
            if r and not r.get("exclude") and r.get("name"):
                out.append((path, r))
        return out


def _url_path(url: str) -> str:
    if "://" in url:
        url = url.split("://", 1)[1]
    i = url.find("/")
    return url[i:] if i >= 0 else "/"


def load_spec(psdk: Path, ns: str) -> Spec:
    return Spec(ns, yaml.safe_load((psdk / "rest-apis" / ns / "openapi.yaml").read_text()))


# ---------------------------------------------------------------------------
# Path composition (§4).
# ---------------------------------------------------------------------------

def join_path(a: str, b: str) -> str:
    if not b:
        return a
    return a.rstrip("/") + "/" + b.lstrip("/")


def collection_segment(anchor: str, markup: dict) -> str:
    if "collection" in markup:
        return markup["collection"]
    p = anchor
    i = p.find("/{")
    if i >= 0:
        p = p[:i]
    return p


def base_path(spec: Spec, anchor: str, markup: dict) -> str:
    return join_path(spec.server_path, collection_segment(anchor, markup))


def relative_tail(spec: Spec, anchor: str, markup: dict, op_path: str):
    coll = collection_segment(anchor, markup)
    full = join_path(spec.server_path, coll)
    absp = join_path(spec.server_path, op_path)
    if coll and absp.startswith(full + "/"):
        return ([s for s in absp[len(full) + 1:].split("/") if s], False)
    if coll and absp == full:
        return ([], False)
    return ([s for s in absp.lstrip("/").split("/") if s], True)


# ---------------------------------------------------------------------------
# Naming.
# ---------------------------------------------------------------------------

def snake_to_camel(snake: str) -> str:
    parts = [p for p in snake.replace("-", "_").replace(".", "_").split("_") if p]
    if not parts:
        return snake
    return parts[0] + "".join(w[:1].upper() + w[1:] for w in parts[1:])


def snake_ident(field: str) -> str:
    """A safe C++ identifier for a wire field name (member/param). Non-identifier
    runes fold to ``_``; a leading digit gets a ``_`` prefix; a C++ keyword gets a
    trailing ``_``. Reserved-word / non-identifier fields are RECORDED as renames."""
    s = re.sub(r"[^A-Za-z0-9_]", "_", field)
    if not s:
        s = "field"
    if s[0].isdigit():
        s = "_" + s
    if s in CPP_KEYWORDS:
        s = s + "_"
    return s


# Canonical base-provided ops that a subclass may override to a divergent
# (sibling) path. Such an override MUST keep the base's exact member name so it
# HIDES the inherited base member (one canonical op, one dispatched route) rather
# than adding a second, differently-spelled method that routes elsewhere — which
# is exactly the route-collision the gate flags (list_addresses on the singular
# call_flow/conference_room sub-path vs the plural base path).
_BASE_CANONICAL_OVERRIDES = {"list_addresses"}


def method_ident(method_snake: str) -> str:
    """C++ method name for a declared method. camelCase the snake name (the port's
    snake_case-methods idiom keeps python names, but the generated resource methods
    follow the hand code's camelCase for sub-ops), then escape a C++ keyword with a
    trailing ``_`` (``delete`` → ``delete_``, matching the enumerator's
    ``_METHOD_RENAMES``). Recorded as an adapter rename when escaped.

    A base-provided canonical op (``list_addresses``) that a subclass overrides to
    a divergent sibling path keeps its snake_case base spelling so the override
    HIDES the inherited base member — one canonical name, one route."""
    if method_snake in _BASE_CANONICAL_OVERRIDES:
        return method_snake
    name = snake_to_camel(method_snake)
    if name in CPP_KEYWORDS:
        name = name + "_"
    return name


PARAM_ARG_NAME = {
    "e164_number": "e164",
}


def arg_for(brace: str) -> str:
    return PARAM_ARG_NAME.get(brace, snake_ident(brace) or "id")


def cpp_str(s: str) -> str:
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


# ---------------------------------------------------------------------------
# Base mapping (§2).
# ---------------------------------------------------------------------------

BASE_PROVIDES = {
    "CrudResource": {"list", "create", "get", "update", "delete"},
    "FabricResource": {"list", "create", "get", "update", "delete", "list_addresses"},
    "ReadResource": {"list", "get"},
    "BaseResource": set(),
}

# The C++ hand-written base each markup base maps to. These live in the base
# shims (base_resource.hpp) — hand-written like Python's _base hierarchy; the
# generated resource subclass EXTENDS one of them.
EXTENDS = {
    "CrudResource": "CrudResource",
    "FabricResource": "FabricResource",
    "ReadResource": "ReadResource",
    "BaseResource": "BaseResource",
}


# ---------------------------------------------------------------------------
# Command-dispatch (§6).
# ---------------------------------------------------------------------------

def command_method_name(cmd: str) -> str:
    s = cmd
    if s.startswith("calling."):
        s = s[len("calling."):]
    return s.replace(".", "_")


def discriminator_mapping(spec: Spec, schema_name: str) -> list[str]:
    sch = spec.schemas.get(schema_name)
    if sch is None:
        raise SystemExit(f"command-dispatch request {schema_name!r} not in components.schemas")
    mapping = (sch.get("discriminator") or {}).get("mapping")
    if not mapping:
        raise SystemExit(f"command-dispatch request {schema_name!r} has no discriminator.mapping")
    return list(mapping.keys())


# ---------------------------------------------------------------------------
# Typed inputs (§5) — schema → C++ native type.
# ---------------------------------------------------------------------------

def resolve_schema(spec: Spec, schema: dict | None, seen=None) -> dict:
    if not schema:
        return {}
    if seen is None:
        seen = set()
    ref = schema.get("$ref")
    if ref:
        if not ref.startswith("#/"):
            return {}
        leaf = ref.rsplit("/", 1)[-1]
        if leaf in seen:
            return {}
        seen.add(leaf)
        return resolve_schema(spec, spec.schemas.get(leaf), seen)
    allof = schema.get("allOf")
    if allof and len(allof) == 1 and not schema.get("properties") and not schema.get("type"):
        return resolve_schema(spec, allof[0], seen)
    return schema


def _json_type(schema: dict) -> str | None:
    t = schema.get("type")
    if isinstance(t, list):
        non_null = [x for x in t if x != "null"]
        return non_null[0] if non_null else None
    return t


# Distinct int/double — C++ is a typed-numeric language (NO numeric-monotype).
_SCALAR_CPP = {"string": "std::string", "integer": "int", "number": "double", "boolean": "bool"}


def cpp_field_type(spec: Spec, schema: dict) -> str:
    """The C++ value type for a body field (the struct member's underlying type,
    before optional wrapping). Scalars map to their distinct C++ type; array /
    object / $ref-to-object / union / unknown → the open ``json`` value."""
    resolved = resolve_schema(spec, schema)
    jt = _json_type(resolved)
    return _SCALAR_CPP.get(jt, "json")


def object_body_fields(spec: Spec, body_schema: dict) -> list[tuple[str, dict, bool]]:
    resolved = resolve_schema(spec, body_schema)
    props: dict[str, dict] = {}
    required: set[str] = set(resolved.get("required") or [])
    for name, psc in (resolved.get("properties") or {}).items():
        props.setdefault(name, psc)
    for br in resolved.get("allOf") or []:
        rb = resolve_schema(spec, br)
        required |= set(rb.get("required") or [])
        for name, psc in (rb.get("properties") or {}).items():
            props.setdefault(name, psc)
    return [(name, psc, name in required) for name, psc in props.items()]


def command_param_fields(spec: Spec, command_schema: dict) -> tuple[list[tuple[str, dict, bool]], bool]:
    """§6 union-flatten: return ([(wire_name, schema, required)], has_id)."""
    cs = resolve_schema(spec, command_schema)
    has_id = "id" in (cs.get("properties") or {})
    params_schema = (cs.get("properties") or {}).get("params")
    if params_schema is None:
        return [], has_id
    ps = resolve_schema(spec, params_schema)
    variants: list[dict] = []
    for comb in ("anyOf", "oneOf"):
        if comb in ps:
            variants = [resolve_schema(spec, v) for v in ps[comb]]
            break
    if not variants:
        variants = [ps]
    all_props: dict[str, dict] = {}
    req_sets: list[set[str]] = []
    for v in variants:
        req_sets.append(set(v.get("required") or []))
        for name, psc in (v.get("properties") or {}).items():
            all_props.setdefault(name, psc)
    req_all = set.intersection(*req_sets) if req_sets else set()
    return [(name, psc, name in req_all) for name, psc in all_props.items()], has_id


def is_object_body(spec: Spec, body_schema: dict) -> bool:
    if not body_schema:
        return False
    if "anyOf" in body_schema or "oneOf" in body_schema:
        return False
    resolved = resolve_schema(spec, body_schema)
    if "anyOf" in resolved or "oneOf" in resolved:
        return False
    if resolved.get("properties") or resolved.get("allOf"):
        return True
    return _json_type(resolved) == "object"


def ordered_fields(fields: list[tuple[str, dict, bool]]) -> list[tuple[str, dict, bool]]:
    req = [f for f in fields if f[2]]
    opt = [f for f in fields if not f[2]]
    return req + opt


# Sidecar accumulator: (ClassName, cppMethodName) -> [param records].
# Each record mirrors the Python oracle param shape (name, kind, type, required)
# so the signature enumerator can unfold the options-struct onto the oracle.
_SIDECAR: dict[tuple[str, str], list[dict]] = {}


def _canon_type(spec: Spec, schema: dict, required: bool) -> str:
    """Canonical audit type for the sidecar (matches php's rule — drift-neutral
    against the oracle: optionals → optional<any>, required scalar → concrete,
    required array → list<any>, required object/ref/union → dict<string,any>)."""
    if not required:
        return "optional<any>"
    resolved = resolve_schema(spec, schema)
    if schema.get("$ref") or (
        schema.get("allOf") and len(schema.get("allOf")) == 1
        and not schema.get("properties") and not schema.get("type")
    ):
        return "dict<string,any>"
    jt = _json_type(resolved)
    canon = {"string": "string", "integer": "int", "number": "float", "boolean": "bool"}
    if jt in canon:
        return canon[jt]
    if jt == "array":
        return "list<any>"
    return "dict<string,any>"


def _register_sidecar(cls: str, method: str, records: list[dict]) -> None:
    _SIDECAR[(cls, method)] = records


# ---------------------------------------------------------------------------
# Emitters — C++ options-struct + method.
# ---------------------------------------------------------------------------

def _indent(src: str, pad: str) -> str:
    """Indent every non-empty line of a multi-line source block by ``pad``."""
    return "\n".join((pad + ln) if ln else ln for ln in src.split("\n"))


def gen_header(desc: str, extra_includes: list[str] | None = None) -> str:
    """File preamble: banner + system includes + any extra project includes (which
    MUST precede the namespace block), then the open namespace + json alias."""
    lines = [
        "// Copyright (c) 2025 SignalWire",
        "// SPDX-License-Identifier: MIT",
        "//",
        "// Code generated by scripts/generate_rest.py; DO NOT EDIT.",
        "//",
        "// Auto-generated from the SignalWire REST API contracts — regenerate with:",
        "//   python3 scripts/generate_rest.py",
        "//",
        f"// {desc}",
        "#pragma once",
        "",
        "#include <map>",
        "#include <nlohmann/json.hpp>",
        "#include <optional>",
        "#include <string>",
        "",
    ]
    # Quoted (local project) includes form a single clang-format group, sorted
    # alphabetically — emit them pre-sorted so the formatter is a no-op.
    quoted = ['#include "signalwire/rest/base_resource.hpp"'] + list(extra_includes or [])
    lines += sorted(quoted)
    lines += [
        "",
        "namespace signalwire {",
        "namespace rest {",
        "namespace generated {",
        "",
        "using json = nlohmann::json;",
    ]
    return "\n".join(lines) + "\n"

GEN_FOOTER = """
}  // namespace generated
}  // namespace rest
}  // namespace signalwire
"""


def _params_struct(struct_name: str, fields: list[tuple[str, dict, bool]], spec: Spec,
                   cls: str, method: str, leading_records: list[dict]) -> tuple[str, list[str], list[str]]:
    """Emit a named options-struct with a member per ordered spec field
    (required → plain member, optional → std::optional<T>) + a trailing
    ``json extras`` map. Returns (struct_src, body_build_lines, records)."""
    # Struct members at 2-space base indent; the struct is nested one more level
    # (inside the resource class) by _indent(s, "  ") at the call site, so the final
    # member indent is 4 — matching clang-format (IndentWidth 2). Method-body build
    # lines likewise at 4-space base (they sit directly in the method body).
    lines = [f"struct {struct_name} {{"]
    build: list[str] = ["    json body = json::object();"]
    records: list[dict] = list(leading_records)
    for wire_name, schema, required in ordered_fields(fields):
        ident = snake_ident(wire_name)
        if ident != snake_to_camel(wire_name) and ident.rstrip("_") != wire_name:
            # only record a rename when the identifier truly diverges from wire
            pass
        if ident != wire_name and (wire_name in CPP_KEYWORDS or re.search(r"[^A-Za-z0-9_]", wire_name) or wire_name[:1].isdigit()):
            _RENAMES.append((cls, method, wire_name, ident))
        base_t = cpp_field_type(spec, schema)
        records.append({"name": wire_name, "kind": "keyword",
                        "type": _canon_type(spec, schema, required), "required": required})
        if required:
            lines.append(f"  {base_t} {ident};")
            build.append(f"    body[{cpp_str(wire_name)}] = p.{ident};")
        else:
            lines.append(f"  std::optional<{base_t}> {ident};")
            build.append(f"    if (p.{ident}.has_value()) {{")
            build.append(f"      body[{cpp_str(wire_name)}] = *p.{ident};")
            build.append("    }")
    # forward-compat door + kwargs sidecar record (kwargs has no distinct member).
    lines.append("  json extras = json::object();")
    records.append({"name": "extras", "kind": "keyword",
                    "type": "optional<dict<string,any>>", "required": False, "default": None})
    records.append({"name": "kwargs", "kind": "var_keyword", "type": "any",
                    "required": False, "default": {}})
    build.append("    if (!p.extras.is_null()) {")
    build.append("      body.update(p.extras);")
    build.append("    }")
    lines.append("};")
    _register_sidecar(cls, method, records)
    return "\n".join(lines), build, records


def method_call_path(spec: Spec, anchor: str, markup: dict, op_path: str):
    """Return (id_args, cpp_path_expr) — id_args are positional string params,
    the path expr composes ``base_path_`` (relative) or an absolute (sibling)."""
    segs, sibling = relative_tail(spec, anchor, markup, op_path)
    id_args: list[str] = []
    pieces: list[str] = []
    for s in segs:
        if s.startswith("{") and s.endswith("}"):
            arg = arg_for(s[1:-1])
            while arg in id_args:
                arg += "2"
            id_args.append(arg)
            pieces.append(("VAR", arg))
        else:
            pieces.append(("LIT", s))
    if sibling:
        full = join_path(spec.server_path, op_path.lstrip("/"))
        expr = abs_cpp_path(full, id_args)
    else:
        # relative to base_path_: base_path_ + "/" + seg + "/" + var ...
        expr = "base_path_"
        for kind, val in pieces:
            if kind == "LIT":
                expr += f' + "/" + std::string({cpp_str(val)})'
            else:
                expr += f' + "/" + {val}'
    return id_args, expr


def abs_cpp_path(full: str, id_args: list[str]) -> str:
    out = []
    literal = []
    ai = 0
    i = 0
    while i < len(full):
        if full[i] == "{":
            j = full.find("}", i)
            if literal:
                out.append(cpp_str("".join(literal)))
                literal = []
            if ai < len(id_args):
                out.append(id_args[ai])
                ai += 1
            i = j + 1
            continue
        literal.append(full[i])
        i += 1
    if literal:
        out.append(cpp_str("".join(literal)))
    # join with std::string(...) + ... so literal-only expressions are std::string
    parts = []
    for k, o in enumerate(out):
        if o in id_args:
            parts.append(o)
        else:
            parts.append(f"std::string({o})" if k == 0 else o)
    return " + ".join(parts) if parts else "std::string()"


def _verb_call(recv: str, verb: str, path_expr: str, body_arg: str | None,
               query_arg: str | None) -> str:
    fn = {"post": "post", "put": "put", "patch": "patch", "get": "get", "delete": "del"}[verb]
    if verb == "get":
        return f"return {recv}.{fn}({path_expr}, {query_arg});"
    if verb == "delete":
        return f"return {recv}.{fn}({path_expr});"
    return f"return {recv}.{fn}({path_expr}, {body_arg});"


def emit_method(spec: Spec, anchor: str, markup: dict, base: str,
                method_snake: str, op_id: str) -> tuple[list[str], list[str]]:
    """Return (struct_defs, method_lines) for one declared method."""
    if op_id not in spec.ops:
        raise SystemExit(f"{markup['name']}.{method_snake}: op {op_id!r} not in spec")
    verb, op_path, has_body = spec.ops[op_id]
    id_args, path_expr = method_call_path(spec, anchor, markup, op_path)
    name = method_ident(method_snake)
    if name != snake_to_camel(method_snake) and name != method_snake:
        _RENAMES.append((markup["name"], name, method_snake, name))
    cls = markup["name"]
    # base subclasses receive ``client_`` (protected member of the base).
    recv = "client_"

    id_records = [{"name": a, "kind": "positional", "type": "string", "required": True}
                  for a in id_args]
    id_params = ["const std::string& " + a for a in id_args]
    write_verb = verb in ("post", "put", "patch")
    structs: list[str] = []
    lines: list[str] = []

    if write_verb and has_body:
        body_schema = spec.op_body.get(op_id) or {}
        if is_object_body(spec, body_schema):
            fields = object_body_fields(spec, body_schema)
            struct_name = f"{name.rstrip('_')[:1].upper() + name.rstrip('_')[1:]}Params"
            struct_src, build, _ = _params_struct(struct_name, fields, spec, cls, name, id_records)
            structs.append(struct_src)
            sig = ", ".join(id_params + [f"const {struct_name}& p"])
            lines.append(f"  [[nodiscard]] json {name}({sig}) const {{")
            lines.extend("  " + b for b in build)
            lines.append("  " + _verb_call(recv, verb, path_expr, "body", None))
            lines.append("  }")
        else:
            # §5.2 union body → a single positional ``json body`` param.
            _register_sidecar(cls, name, id_records + [
                {"name": "body", "kind": "positional", "type": "dict<string,any>", "required": True}])
            sig = ", ".join(id_params + ["const json& body"])
            lines.append(f"  [[nodiscard]] json {name}({sig}) const {{")
            lines.append("  " + _verb_call(recv, verb, path_expr, "body", None))
            lines.append("  }")
    elif write_verb:
        _register_sidecar(cls, name, list(id_records))
        sig = ", ".join(id_params)
        lines.append(f"  [[nodiscard]] json {name}({sig}) const {{")
        lines.append("  " + _verb_call(recv, verb, path_expr, "json::object()", None))
        lines.append("  }")
    elif verb == "get":
        # §5.3 GET query door — a trailing var_keyword ``params`` map.
        _register_sidecar(cls, name, id_records + [
            {"name": "params", "kind": "var_keyword", "type": "any", "required": False, "default": {}}])
        sig = ", ".join(id_params + ["const std::map<std::string, std::string>& params = {}"])
        lines.append(f"  [[nodiscard]] json {name}({sig}) const {{")
        lines.append("  " + _verb_call(recv, verb, path_expr, None, "params"))
        lines.append("  }")
    else:  # delete
        _register_sidecar(cls, name, list(id_records))
        sig = ", ".join(id_params)
        lines.append(f"  [[nodiscard]] json {name}({sig}) const {{")
        lines.append("  " + _verb_call(recv, verb, path_expr, None, None))
        lines.append("  }")
    return structs, lines


# ---------------------------------------------------------------------------
# set_methods (§7) support.
# ---------------------------------------------------------------------------

def schema_fields(spec: Spec, schema: dict, seen=None) -> set[str]:
    if schema is None:
        return set()
    if seen is None:
        seen = set()
    ref = schema.get("$ref")
    if ref:
        if not ref.startswith("#/"):
            return set()
        leaf = ref.rsplit("/", 1)[-1]
        if leaf in seen:
            return set()
        seen.add(leaf)
        return schema_fields(spec, spec.schemas.get(leaf), seen)
    out = set(((schema.get("properties")) or {}).keys())
    for comb in ("allOf", "anyOf", "oneOf"):
        for br in schema.get(comb) or []:
            out |= schema_fields(spec, br, seen)
    return out


def _update_op_schema(spec: Spec, anchor: str, markup: dict) -> dict | None:
    coll = collection_segment(anchor, markup)
    want_verb = "put" if markup.get("update_method") == "PUT" else "patch"
    for path, item in (spec.doc.get("paths") or {}).items():
        if not path.startswith(coll + "/{"):
            continue
        if path.count("/{") != 1 or not path.endswith("}"):
            continue
        op = item.get(want_verb) or item.get("put") or item.get("patch")
        if not op:
            continue
        content = (op.get("requestBody") or {}).get("content") or {}
        for media in content.values():
            sch = media.get("schema")
            if sch:
                return sch
    return None


def update_request_fields(spec: Spec, anchor: str, markup: dict) -> set[str]:
    sch = _update_op_schema(spec, anchor, markup)
    return schema_fields(spec, sch) if sch else set()


def update_field_schemas(spec: Spec, anchor: str, markup: dict) -> dict[str, dict]:
    sch = _update_op_schema(spec, anchor, markup)
    if not sch:
        return {}
    return {name: psc for name, psc, _ in object_body_fields(spec, sch)}


def emit_set_method(spec: Spec, markup: dict, sm_name: str, sm: dict,
                    upd_fields: set[str], field_schemas: dict[str, dict]) -> tuple[list[str], list[str]]:
    handler = sm.get("handler")
    if not handler:
        raise SystemExit(f"{markup['name']}.{sm_name}: set_method missing handler")
    cls = markup["name"]
    name = snake_to_camel(sm_name)
    args = sm.get("args") or {}
    struct_name = f"{name[:1].upper() + name[1:]}Params"
    struct_fields: list[tuple[str, dict, bool]] = []
    records: list[dict] = [
        {"name": "resource_id", "kind": "positional", "type": "string", "required": True}]
    for arg_name, arg in args.items():
        field = arg.get("field")
        if not field:
            raise SystemExit(f"{markup['name']}.{sm_name}: arg {arg_name!r} missing field")
        if field not in upd_fields:
            raise SystemExit(
                f"{markup['name']}.{sm_name}: arg field {field!r} not in update request schema")
        required = bool(arg.get("required"))
        struct_fields.append((arg_name, field_schemas.get(field, {}), required))
        records.append({"name": arg_name, "kind": "positional",
                        "type": _canon_type(spec, field_schemas.get(field, {}), required),
                        "required": required})
    records.append({"name": "extra", "kind": "var_keyword", "type": "any",
                    "required": False, "default": {}})
    _register_sidecar(cls, name, records)

    # Options-struct for the set-method args (member per arg + extras).
    structs = []
    slines = [f"struct {struct_name} {{"]
    build = [f'    json body = {{{{"call_handler", {cpp_str(handler)}}}}};']
    for arg_name, fschema, required in struct_fields:
        ident = snake_ident(arg_name)
        # map arg -> bound update field for the wire key
        field = args[arg_name]["field"]
        base_t = cpp_field_type(spec, fschema)
        if required:
            slines.append(f"    {base_t} {ident};")
            build.append(f"    body[{cpp_str(field)}] = p.{ident};")
        else:
            slines.append(f"    std::optional<{base_t}> {ident};")
            build.append(f"    if (p.{ident}.has_value()) {{ body[{cpp_str(field)}] = *p.{ident}; }}")
    slines.append("    json extra = json::object();")
    slines.append("};")
    build.append("    if (!p.extra.is_null()) { body.update(p.extra); }")
    structs.append("\n".join(slines))

    lines = [f"  [[nodiscard]] json {name}(const std::string& resource_id, const {struct_name}& p) const {{"]
    lines.extend("  " + b for b in build)
    lines.append("    return update(resource_id, body);")
    lines.append("  }")
    return structs, lines


# ---------------------------------------------------------------------------
# Command-dispatch emitter (§6).
# ---------------------------------------------------------------------------

def emit_command_dispatch(spec: Spec, anchor: str, markup: dict) -> str:
    name = markup["name"]
    request = markup.get("request")
    if not request:
        raise SystemExit(f"{name}: command-dispatch requires request")
    commands = discriminator_mapping(spec, request)
    op = spec.ops.get("call-commands")
    if op:
        base = join_path(spec.server_path, op[1].lstrip("/"))
    else:
        base = join_path(spec.server_path, anchor.lstrip("/"))

    structs: list[str] = []
    methods: list[str] = []
    mapping = (spec.schemas.get(request).get("discriminator") or {}).get("mapping") or {}
    for cmd in commands:
        mname = command_method_name(cmd)
        cmd_ref = mapping.get(cmd) or ""
        cmd_leaf = cmd_ref.rsplit("/", 1)[-1] if cmd_ref else ""
        cmd_schema = spec.schemas.get(cmd_leaf, {})
        fields, with_id = command_param_fields(spec, cmd_schema)

        records: list[dict] = []
        if with_id:
            records.append({"name": "call_id", "kind": "positional",
                            "type": "string", "required": True})
        struct_name = f"{mname[:1].upper() + mname[1:]}Params"
        # build options struct
        slines = [f"struct {struct_name} {{"]
        build = ["    json params = json::object();"]
        for wire_name, schema, required in ordered_fields(fields):
            ident = snake_ident(wire_name)
            if ident != wire_name and (wire_name in CPP_KEYWORDS or re.search(r"[^A-Za-z0-9_]", wire_name) or wire_name[:1].isdigit()):
                _RENAMES.append((name, mname, wire_name, ident))
            base_t = cpp_field_type(spec, schema)
            records.append({"name": wire_name, "kind": "keyword",
                            "type": _canon_type(spec, schema, required), "required": required})
            if required:
                slines.append(f"    {base_t} {ident};")
                build.append(f"    params[{cpp_str(wire_name)}] = p.{ident};")
            else:
                slines.append(f"    std::optional<{base_t}> {ident};")
                build.append(f"    if (p.{ident}.has_value()) {{ params[{cpp_str(wire_name)}] = *p.{ident}; }}")
        slines.append("    json extras = json::object();")
        slines.append("};")
        build.append("    if (!p.extras.is_null()) { params.update(p.extras); }")
        records.append({"name": "extras", "kind": "keyword",
                        "type": "optional<dict<string,any>>", "required": False, "default": None})
        _register_sidecar(name, mname, records)
        structs.append("\n".join(slines))

        id_param = ["const std::string& call_id"] if with_id else []
        # No ``= {}`` default: a nested struct with a default member initializer
        # cannot be a default argument inside the enclosing class definition
        # (C++ rule). Callers pass ``{}`` explicitly for the all-optional commands.
        sig = ", ".join(id_param + [f"const {struct_name}& p"])
        call_arg = "call_id" if with_id else "std::nullopt"
        methods.append(f"  [[nodiscard]] json {mname}({sig}) const {{")
        methods.extend("  " + b for b in build)
        if with_id:
            methods.append(f"    return execute({cpp_str(cmd)}, params, call_id);")
        else:
            methods.append(f"    return execute({cpp_str(cmd)}, params);")
        methods.append("  }")

    lines = []
    lines.append(gen_header(
        f"Generated command-dispatch resource for the {spec.name!r} namespace."))
    lines.append("")
    lines.append(f"/// {name} — command-dispatch resource ({spec.name} spec).")
    lines.append(f"/// Each method POSTs {{command, params, id?}} to {base}.")
    lines.append(f"class {name} {{")
    lines.append(" public:")
    # Nested per-command options-structs (§5/§6) — nested so the <Command>Params
    # names live under the resource class.
    for s in structs:
        lines.append(_indent(s, "  "))
        lines.append("")
    lines.append(f"  explicit {name}(const HttpClient& http) : http_(http) {{}}")
    lines.append("")
    lines.append(f"  static constexpr const char* kBasePath = {cpp_str(base)};")
    lines.append("")
    lines.extend(methods)
    lines.append("")
    lines.append(" private:")
    lines.append("  [[nodiscard]] json execute(const std::string& command, const json& params, "
                 "const std::optional<std::string>& call_id = std::nullopt) const {")
    lines.append("    json body = {{\"command\", command}, {\"params\", params}};")
    lines.append("    if (call_id.has_value()) { body[\"id\"] = *call_id; }")
    lines.append("    return http_.post(kBasePath, body);")
    lines.append("  }")
    lines.append("")
    lines.append("  const HttpClient& http_;")
    lines.append("};")
    lines.append(GEN_FOOTER)
    return "\n".join(lines)


# ---------------------------------------------------------------------------
# Resource emitter.
# ---------------------------------------------------------------------------

def emit_resource(spec: Spec, anchor: str, markup: dict) -> str:
    name = markup["name"]
    base = markup["base"]
    if markup.get("kind") == "command-dispatch":
        return emit_command_dispatch(spec, anchor, markup)
    if base not in EXTENDS:
        raise SystemExit(f"{name}: unknown base {base!r}")

    if base in ("CrudResource", "FabricResource"):
        upd = markup.get("update_method")
        if not upd:
            raise SystemExit(f"{name}: {base} requires update_method")
        item = spec.doc["paths"][anchor]
        spec_verb = "PUT" if item.get("put") else ("PATCH" if item.get("patch") else None)
        if spec_verb and upd != spec_verb:
            raise SystemExit(f"{name}: update_method {upd} != spec update verb {spec_verb}")

    extends = EXTENDS[base]
    bp = base_path(spec, anchor, markup)
    update_method = markup.get("update_method", "PATCH")

    provided = BASE_PROVIDES[base]
    declared = markup.get("methods") or {}

    all_structs: list[str] = []
    all_methods: list[str] = []

    for method_snake, spec_ref in declared.items():
        op_id = spec_ref.get("op")
        if not op_id:
            raise SystemExit(f"{name}.{method_snake}: method markup missing op")
        if method_snake in provided:
            if method_snake == "list_addresses":
                verb, op_path, _ = spec.ops[op_id]
                _, sibling = relative_tail(spec, anchor, markup, op_path)
                if not sibling:
                    continue
            else:
                continue
        structs, mlines = emit_method(spec, anchor, markup, base, method_snake, op_id)
        all_structs.extend(structs)
        if all_methods:
            all_methods.append("")
        all_methods.extend(mlines)

    set_methods = markup.get("set_methods") or {}
    if set_methods:
        if base not in ("CrudResource", "FabricResource"):
            raise SystemExit(f"{name}: set_methods require a CRUD base, got {base}")
        upd_fields = update_request_fields(spec, anchor, markup)
        upd_field_schemas = update_field_schemas(spec, anchor, markup)
        for sm_name, sm in set_methods.items():
            structs, mlines = emit_set_method(spec, markup, sm_name, sm, upd_fields, upd_field_schemas)
            all_structs.extend(structs)
            if all_methods:
                all_methods.append("")
            all_methods.extend(mlines)

    # Base CRUD verbs (list/get/create/update/delete_) are INHERITED from the
    # base_resource.hpp bases, so they're not declared in this header and the
    # reflection-free enumerators can't see them — but the Python oracle records
    # create/update/delete as per-class declared overrides. Register sidecar
    # records for the inherited verbs the oracle expects so both enumerators
    # surface them (§5 sidecar is the single projection source). list/get/delete
    # take path-id/query only; create/update carry the write BODY, which for the
    # fabric/crud resources the generated code passes as a single loose
    # ``json body`` (item B typed-body not yet emitted for these — honest
    # ``cpp-idiom-loose-write-body`` residual, PORT_SIGNATURE_OMISSIONS, not a
    # BACKLOG). Only register a verb the resource's collection/item path actually
    # exposes, matching the oracle's bespoke per-class set (e.g. Recordings has
    # no create/update; ImportedNumbers has create only).
    # Python's REST generator emits the write-verb overrides per base:
    #   FabricResource -> create + update      (list/get/delete/list_addresses inherited)
    #   CrudResource   -> create + update + delete   (list/get inherited)
    # (verified against the oracle's per-class declared set — uniform by base).
    # The body is passed as a single loose ``json body`` for these resources
    # (item B typed-body not yet emitted here — honest cpp-idiom-loose-write-body
    # residual, PORT_SIGNATURE_OMISSIONS, never BACKLOG). Register them in the
    # sidecar so the reflection-free enumerators surface them (§5).
    # Python's REST generator, verified against the oracle's declared set:
    #   ReadResource   -> list + get
    #   CrudResource   -> list?+get? NO — create + update + delete (list/get inherited, unrecorded)
    #   FabricResource -> create + update      (list/get/delete/list_addresses inherited)
    # (ReadResource DOES record list/get; the write bases do NOT record list/get.)
    _INHERITED_VERBS_BY_BASE = {
        "ReadResource": ("list", "get"),
        "FabricResource": ("create", "update"),
        "CrudResource": ("create", "update", "delete"),
    }

    def _verb_records(verb: str) -> list[dict]:
        if verb == "list":
            return [{"name": "params", "kind": "var_keyword", "type": "any",
                     "required": False, "default": {}}]
        if verb == "get":
            return [{"name": "id", "kind": "positional", "type": "string", "required": True},
                    {"name": "params", "kind": "var_keyword", "type": "any",
                     "required": False, "default": {}}]
        if verb == "delete":
            return [{"name": "id", "kind": "positional", "type": "string", "required": True}]
        if verb == "create":
            return [{"name": "body", "kind": "positional", "type": "dict<string,any>",
                     "required": True}]
        # update
        return [{"name": "id", "kind": "positional", "type": "string", "required": True},
                {"name": "body", "kind": "positional", "type": "dict<string,any>",
                 "required": True}]

    for verb in _INHERITED_VERBS_BY_BASE.get(base, ()):
        if (name, verb) in _SIDECAR:
            continue  # a typed / declared override already registered — keep it
        _register_sidecar(name, verb, _verb_records(verb))

    # Constructor: bake the §4 base path. For write-capable bases, pass the update
    # method so the base picks PUT/PATCH (mirrors Python/php _update_method).
    lines = [gen_header(f"Generated REST resource for the {spec.name!r} namespace.")]
    lines.append("")
    lines.append(f"/// {name} — REST resource for the {spec.name!r} API (base {base}).")
    lines.append(f"class {name} : public {extends} {{")
    lines.append(" public:")
    # Nested per-method options-structs (§5, PORT_PHILOSOPHY_CPP L13) — nested in
    # the resource class so <Method>Params names never collide across resources.
    for s in all_structs:
        lines.append(_indent(s, "  "))
        lines.append("")
    if base in ("CrudResource", "FabricResource"):
        lines.append(f"  explicit {name}(const HttpClient& client)")
        lines.append(f"      : {extends}(client, {cpp_str(bp)}, {cpp_str(update_method)}) {{}}")
    else:
        lines.append(f"  explicit {name}(const HttpClient& client)")
        lines.append(f"      : {extends}(client, {cpp_str(bp)}) {{}}")
    if all_methods:
        lines.append("")
        lines.extend(all_methods)
    lines.append("};")
    lines.append(GEN_FOOTER)
    return "\n".join(lines)


# ---------------------------------------------------------------------------
# Client tree (§8).
# ---------------------------------------------------------------------------

CONTAINERS = {
    "fabric": "FabricNamespace",
    "video": "VideoNamespace",
    "logs": "LogsNamespace",
    "registry": "RegistryNamespace",
    "project": "ProjectNamespace",
    "datasphere": "DatasphereNamespace",
}

ATTR_OVERRIDE = {
    "GenericResources": "resources", "FabricAddresses": "addresses",
    "FabricTokens": "tokens", "DatasphereDocuments": "documents",
    "ProjectTokens": "tokens", "PubSub": "pubsub",
    "MessageLogs": "messages", "VoiceLogs": "voice", "FaxLogs": "fax",
    "ConferenceLogs": "conferences",
}


def container_accessor(markup: dict, name: str, container: str) -> str:
    if markup.get("attr"):
        return markup["attr"]
    if name in ATTR_OVERRIDE:
        return ATTR_OVERRIDE[name]
    lead = container[:1].upper() + container[1:]
    stem = name[len(lead):] if name.startswith(lead) else name
    # snake_case the pascal stem
    s = re.sub(r"(?<!^)(?=[A-Z])", "_", stem).lower()
    return s or name.lower()


def flat_accessor(name: str) -> str:
    if name in ATTR_OVERRIDE:
        return ATTR_OVERRIDE[name]
    return re.sub(r"(?<!^)(?=[A-Z])", "_", name).lower()


def resolve_placement(specs: list[Spec]):
    placed = []
    for spec in specs:
        for anchor, markup in spec.resources():
            container = markup.get("namespace") or spec.namespace_attr or ""
            placed.append((spec, anchor, markup, container))
    return placed


def emit_container(container: str, members: list[tuple[str, str]]) -> str:
    cls = CONTAINERS[container]
    includes = [f'#include "signalwire/rest/namespaces/generated/{class_name}.hpp"'
                for _, class_name in members]
    lines = [gen_header(
        f"Generated REST client container for the {container} namespace (§8).", includes)]
    lines.append("")
    lines.append(f"/// {cls} — generated container grouping the {container} namespace resources (§8).")
    lines.append(f"class {cls} {{")
    lines.append(" public:")
    lines.append(f"  explicit {cls}(const HttpClient& http)")
    ctor_inits = ", ".join(f"{acc}(http)" for acc, _ in members)
    lines.append(f"      : {ctor_inits} {{}}")
    lines.append("")
    for accessor, class_name in members:
        lines.append(f"  {class_name} {accessor};")
    lines.append("};")
    lines.append(GEN_FOOTER)
    return "\n".join(lines)


def emit_resource_tree(placed) -> str:
    """The generated resource tree the hand RestClient composes: a struct holding
    every FLAT resource + each container, constructed from an HttpClient. Mirrors
    Go's _GeneratedResourceTree / php's ResourceTree trait."""
    flats = []
    containers_seen = []
    seen_c = set()
    for spec, anchor, markup, container in placed:
        name = markup["name"]
        if not container:
            flats.append((flat_accessor(name), name))
        else:
            if container not in seen_c:
                seen_c.add(container)
                containers_seen.append(container)

    includes = [f'#include "signalwire/rest/namespaces/generated/{cls}.hpp"' for _, cls in flats]
    includes += [f'#include "signalwire/rest/namespaces/generated/{CONTAINERS[c]}.hpp"'
                 for c in containers_seen]
    lines = [gen_header(
        "Generated REST resource tree the hand RestClient composes (§8).", includes)]
    lines.append("")
    lines.append("/// ResourceTree — flat resources plus namespace containers.")
    lines.append("/// Groups every REST resource under its API namespace so the")
    lines.append("/// RestClient can expose them as a single accessor tree.")
    lines.append("struct ResourceTree {")
    ctor_inits = []
    for acc, cls in flats:
        ctor_inits.append(f"{acc}(http)")
    for c in containers_seen:
        ctor_inits.append(f"{c}(http)")
    lines.append("  explicit ResourceTree(const HttpClient& http)")
    lines.append("      : " + ", ".join(ctor_inits) + " {}")
    lines.append("")
    for acc, cls in flats:
        lines.append(f"  {cls} {acc};")
    for c in containers_seen:
        lines.append(f"  {CONTAINERS[c]} {c};")
    lines.append("};")
    lines.append(GEN_FOOTER)
    return "\n".join(lines)


# ---------------------------------------------------------------------------
# Generated wire-TYPE surface (item D/H — <ns>_types_generated).
# ---------------------------------------------------------------------------
#
# A method-less C++ data struct per components/schemas OBJECT schema, plus a
# closed-set struct per x-sdk-enum. Mirrors ruby's emit_type_class / emit_type_enum
# and the shared emit_methodless_struct (reused by the SWML-verbs / relay-protocol
# / SWAIG payload generators). The Python SURFACE oracle records these method-less
# (a bare class name); the diff tool folds the cross-module duplicates by leaf name
# (``gen-type``). Object schema -> struct; x-sdk-enum -> closed-set; scalar / array
# / union / plain-inline-enum aliases are NOT surfaced by the reference, so nothing
# is emitted for them. Read-side open shapes: typed members (snake wire key, keyword
# escaped) with a trailing ``json extras`` — no methods, no accessors.

# (spec-dir, C++ namespace segment, oracle <ns>_types_generated leaf).
TYPE_NS = [
    ("relay-rest", "RelayRest", "relay_rest"),
    ("fabric", "Fabric", "fabric"),
    ("calling", "Calling", "calling"),
    ("video", "Video", "video"),
    ("datasphere", "Datasphere", "datasphere"),
    ("logs", "Logs", "logs"),
    ("message", "Message", "message"),
    ("voice", "Voice", "voice"),
    ("fax", "Fax", "fax"),
    ("project", "Project", "project"),
    ("chat", "Chat", "chat"),
    ("pubsub", "PubSub", "pubsub"),
    ("swml-webhooks", "SwmlWebhooks", "swml_webhooks"),
]


def snake(name: str) -> str:
    """PascalCase/camelCase -> snake_case for a file name. Handles acronym runs
    (AIObject -> ai_object, StatusCode400 -> status_code400)."""
    s1 = re.sub(r"(.)([A-Z][a-z]+)", r"\1_\2", name)
    s2 = re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", s1)
    return re.sub(r"[^A-Za-z0-9]+", "_", s2).strip("_").lower()


def type_name(raw: str) -> str:
    """Sanitise a components/schemas key to a valid C++ struct name, folding every
    non-identifier rune to ``_`` (matches ruby/go/php/python ref_name so the LEAF
    the surface diff compares is the identical token cross-port). A leading digit is
    prefixed; the first letter is upper-cased (schema names already are)."""
    s = re.sub(r"[^A-Za-z0-9_]", "_", raw).lstrip("_")
    if not s:
        return "Schema"
    if s[0].isdigit():
        return "Schema_" + s
    if not s[0].isupper():
        s = s[0].upper() + s[1:]
    return s


def _type_schema_type(node: dict):
    t = node.get("type")
    if isinstance(t, list):
        return next((x for x in t if x != "null"), None)
    return t


def is_object_schema(node: dict) -> bool:
    """Mirror the reference is_object test: type:object (or no type but non-empty
    properties) AND not a oneOf/anyOf/allOf combinator AND properties non-empty."""
    if any(k in node for k in ("oneOf", "anyOf", "allOf")):
        return False
    props = node.get("properties")
    t = _type_schema_type(node)
    return (t == "object" or (t is None and props)) and isinstance(props, dict) and len(props) > 0


def _methodless_field_type(psc: dict) -> str:
    """The C++ member type for a wire field on a read-side method-less struct.
    Scalars map to their distinct C++ type; array/object/$ref/combinator/unknown ->
    the open ``json`` value (read-side, forward-compatible). No spec context is
    needed — a $ref/combinator resolves to a nested object/union we surface as
    ``json``, matching the loose read shape."""
    if not isinstance(psc, dict):
        return "json"
    if any(k in psc for k in ("$ref", "allOf", "oneOf", "anyOf")):
        return "json"
    t = _type_schema_type(psc)
    return _SCALAR_CPP.get(t, "json")


def emit_methodless_struct(ns_segments: list[str], name: str, properties: dict,
                           source_desc: str, regen_cmd: str,
                           schema_name: "str | None" = None) -> str:
    """Emit one method-less C++ data struct under an arbitrary nested namespace
    path, carrying one typed member per snake wire key + a trailing ``json extras``.
    Shared by the REST wire-type emitter and the SWML-verbs / relay-protocol / SWAIG
    payload generators so they never diverge. NO methods / accessors — the reference
    records these method-less on the SURFACE (and its signature oracle records a
    method-less class only for its class-typed fields, handled by the enumerator).
    A wire key that is a C++ keyword / non-identifier is escaped (wire key preserved
    only in the doc comment; a read struct has no serialiser here).

    ``schema_name`` is the field's containing SPEC schema name (the $defs /
    components.schemas key), passed to the x-sdk-overlay policy check — NOT the C++
    ``name`` we emit. Overlay-hidden fields are dropped from the surface entirely
    (still on the wire); overlay-deprecated fields are emitted with ``[[deprecated]]``."""
    lines: list[str] = [
        "// Copyright (c) 2025 SignalWire",
        "// SPDX-License-Identifier: MIT",
        "//",
        f"// Code generated by scripts/{regen_cmd}; DO NOT EDIT.",
        "//",
        f"// {source_desc}",
        "#pragma once",
        "",
        "#include <map>",
        "#include <nlohmann/json.hpp>",
        "#include <optional>",
        "#include <string>",
        "#include <vector>",
        "",
    ]
    for seg in ns_segments:
        lines.append(f"namespace {seg} {{")
    lines.append("")
    lines.append("using json = nlohmann::json;")
    lines.append("")
    lines.append(f"/// {name} — generated read-side data type.")
    lines.append(f"/// {source_desc}")
    lines.append("///")
    lines.append("/// Method-less DTO: one typed member per snake wire key + open `extras`.")
    lines.append(f"struct {name} {{")
    # Members emitted raw (single space before any trailing `// wire key:` comment);
    # the shared format_generated_cpp pass aligns trailing comments + reflows the doc
    # comments + wraps long lines exactly as clang-format, so this stays layout-agnostic.
    used: set[str] = set()
    for wire_key, psc in (properties or {}).items():
        # SDK-surface policy from the single overlay (rest-apis/x-sdk-overlay.yaml),
        # matched against the SPEC schema name — NOT markup in the (often vendored) specs.
        if _overlay_hidden(wire_key, schema_name):
            # hidden: drop from the SDK surface entirely (still on the wire).
            continue
        ident = snake_ident(wire_key)
        while ident in used or ident == "extras":
            ident += "_"
        used.add(ident)
        cpp_t = _methodless_field_type(psc if isinstance(psc, dict) else {})
        note = "" if ident == wire_key else f"  // wire key: {wire_key}"
        dep = "[[deprecated]] " if _overlay_deprecated(wire_key, schema_name) else ""
        lines.append(f"  {dep}std::optional<{cpp_t}> {ident};{note}")
    lines.append("  json extras = json::object();")
    lines.append("};")
    lines.append("")
    for seg in reversed(ns_segments):
        lines.append(f"}}  // namespace {seg}")
    return "\n".join(lines) + "\n"


def _load_types_schemas(psdk: Path, spec_dir: str) -> dict:
    """Load a spec's components/schemas WITHOUT the full Spec model (swml-webhooks
    has no servers block, so Spec() would reject it). Ordered by yaml declaration."""
    doc = yaml.safe_load((psdk / "rest-apis" / spec_dir / "openapi.yaml").read_text())
    return ((doc.get("components") or {}).get("schemas")) or {}


def _enum_const_name(value: str) -> str:
    s = re.sub(r"[^A-Za-z0-9]+", "_", value).strip("_").upper()
    if not s:
        s = "VALUE"
    if s[0].isdigit():
        s = "V_" + s
    return s


def emit_type_enum(ns_seg: str, enum_name: str, values: list, ns_key: str, raw_name: str) -> str:
    """Emit a method-less C++ struct carrying static string constants (value == wire
    string) grouped into an ``all()`` list — the port's closed-set idiom for an
    x-sdk-enum public enum. The reference records it method-less; ``all()`` is a
    private-ish static helper the surface enumerator does not surface (it is a static
    free function returning the set, not a member the oracle records)."""
    lines: list[str] = [
        "// Copyright (c) 2025 SignalWire",
        "// SPDX-License-Identifier: MIT",
        "//",
        "// Code generated by scripts/generate_rest.py; DO NOT EDIT.",
        "//",
        f"// {enum_name} — closed set of accepted values for {raw_name!r} "
        f"({ns_key!r} API).",
        "#pragma once",
        "",
        "#include <string>",
        "",
        "namespace signalwire {",
        "namespace rest {",
        "namespace generated {",
        "namespace types {",
        f"namespace {ns_seg} {{",
        "",
        f"/// {enum_name} — generated public closed-set (value IS the wire string).",
        f"struct {enum_name} {{",
    ]
    used: set[str] = set()
    for v in values:
        if v == "" or not isinstance(v, str):
            continue
        cname = _enum_const_name(v)
        while cname in used:
            cname += "_"
        used.add(cname)
        lines.append(f"  static constexpr const char* {cname} = {cpp_str(v)};")
    lines.append("};")
    lines.append("")
    lines.append(f"}}  // namespace {ns_seg}")
    lines.append("}  // namespace types")
    lines.append("}  // namespace generated")
    lines.append("}  // namespace rest")
    lines.append("}  // namespace signalwire")
    return "\n".join(lines) + "\n"


def emit_types(psdk: Path, outs: dict) -> None:
    """Emit every <ns>_types_generated data struct / closed-set into
    ``types/<ns>/<snake_name>.hpp`` keys of ``outs`` (relative to the generated
    dir)."""
    for spec_dir, ns_seg, ns_key in TYPE_NS:
        schemas = _load_types_schemas(psdk, spec_dir)
        for raw_name, node in schemas.items():
            if not isinstance(node, dict):
                continue
            xe = node.get("x-sdk-enum")
            if xe:
                enum_name = type_name(xe)
                fn = f"types/{ns_key}/{snake(enum_name)}.hpp"
                if fn not in outs:
                    outs[fn] = emit_type_enum(
                        ns_seg, enum_name, list(node.get("enum") or []), ns_key, raw_name)
                continue
            if is_object_schema(node):
                struct = type_name(raw_name)
                fn = f"types/{ns_key}/{snake(struct)}.hpp"
                if fn not in outs:
                    ns_segments = ["signalwire", "rest", "generated", "types", ns_seg]
                    outs[fn] = emit_methodless_struct(
                        ns_segments, struct, node.get("properties") or {},
                        f"Generated REST wire type for the {ns_key!r} namespace "
                        f"(components/schemas {raw_name!r}).",
                        "generate_rest.py",
                        schema_name=raw_name,
                    )


# ---------------------------------------------------------------------------
# Driver.
# ---------------------------------------------------------------------------

def build_outputs(psdk: Path) -> dict[str, str]:
    load_bases(psdk)  # validate x-sdk-bases (fail loud)
    _SIDECAR.clear()
    _RENAMES.clear()
    specs = [load_spec(psdk, ns) for ns in SPEC_DIRS]
    outs: dict[str, str] = {}
    for spec in specs:
        for anchor, markup in spec.resources():
            outs[markup["name"] + ".hpp"] = emit_resource(spec, anchor, markup)

    placed = resolve_placement(specs)
    by_container: dict[str, list[tuple[str, str]]] = {}
    order: list[str] = []
    for spec, anchor, markup, container in placed:
        if not container:
            continue
        if container not in by_container:
            by_container[container] = []
            order.append(container)
        acc = container_accessor(markup, markup["name"], container)
        by_container[container].append((acc, markup["name"]))
    for container in order:
        if container not in CONTAINERS:
            raise SystemExit(f"container attr {container!r} has no C++ container class (add to CONTAINERS)")
        cls = CONTAINERS[container]
        outs[cls + ".hpp"] = emit_container(container, by_container[container])

    outs["ResourceTree.hpp"] = emit_resource_tree(placed)

    # Item D/H: the <ns>_types_generated wire-type surface (method-less DTOs +
    # closed-sets from each spec's components/schemas).
    emit_types(psdk, outs)

    # Committed class -> canonical module map (idiom-blind projection the
    # signature/surface enumerators consume, single source of truth — never
    # hand-maintained). Every generated resource class projects onto the
    # reference's ``<ns>_resources_generated`` module (ns = spec dir, ``-``->``_``);
    # the six client-tree container classes project onto ``_client_tree_generated``.
    # Mirrors ruby's generated_surface_map.json.
    surface_map: dict[str, str] = {}
    for spec in specs:
        module = f"signalwire.rest.namespaces.{spec.name.replace('-', '_')}_resources_generated"
        for anchor, markup in spec.resources():
            surface_map[markup["name"]] = module
    for container_cls in sorted(set(CONTAINERS.values())):
        surface_map[container_cls] = "signalwire.rest.namespaces._client_tree_generated"
    outs["generated_surface_map.json"] = _json.dumps(
        dict(sorted(surface_map.items())), indent=2,
    ) + "\n"

    # Sidecar (§5): canonical typed-param records the signature enumerator unfolds
    # onto the reflected options-struct params (libclang can't express keyword-only
    # intent, the json element type, or the open extras dict).
    sidecar: dict[str, list[dict]] = {}
    for (cls, method) in sorted(_SIDECAR.keys()):
        sidecar[f"{cls}::{method}"] = _SIDECAR[(cls, method)]
    outs["rest_signatures.json"] = _json.dumps(
        {
            "_comment": "Code generated by scripts/generate_rest.py; DO NOT EDIT. "
                        "Canonical typed-param records for generated REST operation/"
                        "command/set methods; consumed by scripts/enumerate_signatures.py "
                        "to unfold the reflected C++ options-struct params onto the oracle shape.",
            "methods": sidecar,
        },
        indent=2, sort_keys=False,
    ) + "\n"
    return outs


def _print_classes(psdk: Path) -> None:
    specs = [load_spec(psdk, ns) for ns in SPEC_DIRS]
    per_ns: dict[str, list[str]] = {}
    for spec in specs:
        for anchor, markup in spec.resources():
            ns = spec.name.replace("-", "_")
            # relay-rest registry resources belong to relay_rest module (namespace:
            # registry only affects client-tree placement, not the module).
            per_ns.setdefault(ns, []).append(markup["name"])
    for ns in sorted(per_ns):
        for c in sorted(per_ns[ns]):
            print(f"{ns}\t{c}")


def _print_paths(psdk: Path) -> None:
    specs = [load_spec(psdk, ns) for ns in SPEC_DIRS]
    for spec in specs:
        for anchor, markup in spec.resources():
            if markup.get("kind") == "command-dispatch":
                op = spec.ops.get("call-commands")
                bp = join_path(spec.server_path, op[1].lstrip("/")) if op else join_path(spec.server_path, anchor.lstrip("/"))
            else:
                bp = base_path(spec, anchor, markup)
            print(f"{markup['name']}\t{bp}")


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--check", action="store_true", help="GEN-FRESH: exit non-zero if stale")
    ap.add_argument("--out", default="", help="scratch: emit flat into this dir")
    ap.add_argument("--dump-classes", action="store_true", help="print <ns>\\t<Class> and exit")
    ap.add_argument("--dump-paths", action="store_true", help="print <Class>\\t<base_path> and exit")
    args = ap.parse_args(argv)

    psdk = resolve_porting_sdk()

    if args.dump_classes:
        _print_classes(psdk)
        return 0
    if args.dump_paths:
        _print_paths(psdk)
        return 0

    outs = build_outputs(psdk)
    # Only C++ headers are formatted; any .json sidecars are emitted verbatim.
    outs = {fn: (format_generated_cpp(src) if fn.endswith((".hpp", ".h")) else src)
            for fn, src in outs.items()}

    if args.out:
        out_dir = Path(args.out)
    else:
        out_dir = repo_root() / "include" / "signalwire" / "rest" / "namespaces" / "generated"

    if args.check:
        stale = []
        for fn, src in outs.items():
            p = out_dir / fn
            if not p.is_file() or p.read_text() != src:
                stale.append(str(p))
        expected = set(outs.keys())
        for p in sorted(out_dir.rglob("*.hpp")):
            rel = p.relative_to(out_dir).as_posix()
            if rel not in expected:
                stale.append(f"{p} (leftover — not in generator output)")
        if stale:
            sys.stderr.write("GEN-FRESH FAIL: %d generated REST file(s) stale:\n" % len(stale))
            for s in stale:
                sys.stderr.write("  - %s\n" % s)
            return 1
        print("GEN-FRESH: generated REST files match the canonical specs.")
        return 0

    out_dir.mkdir(parents=True, exist_ok=True)
    for fn, src in outs.items():
        p = out_dir / fn
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(src)
    print(f"generated {len(outs)} REST file(s) into {out_dir}")
    if _RENAMES:
        print(f"\nADAPTER RENAMES ({len(_RENAMES)}) — reserved-word/non-identifier fields "
              "(wire key preserved, param identifier escaped):")
        for cls, method, wire, ident in _RENAMES:
            print(f"  {cls}.{method}: {wire!r} -> {ident}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
