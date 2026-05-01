#!/usr/bin/env python3
"""enumerate_signatures.py — emit port_signatures.json for the C++ SDK.

Phase 4-C++ of the cross-language signature audit. Uses libclang Python
bindings to parse every header under include/ and extract full method
signatures (parameter names, types, defaults, return types) from the
clang AST. This is the only mature option for C++ — regex parsing
cannot handle templates, overloads, or qualified types.

Reuses translation tables from scripts/enumerate_surface.py
(CLASS_MODULE_MAP, MIXIN_PROJECTIONS, camel_to_snake, module_for_class).

Type translation goes via porting-sdk/type_aliases.yaml (cpp section);
unknown types fail loudly with the C++ canonical spelling so the
missing case becomes a documented decision.

Usage:
    python3 scripts/enumerate_signatures.py
    python3 scripts/enumerate_signatures.py --strict
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
from pathlib import Path

import yaml

# Configure libclang BEFORE importing clang.cindex Index.create()
import clang.cindex
_BUNDLED = "/home/devuser/.local/lib/python3.12/site-packages/clang/native/libclang.so"
if Path(_BUNDLED).is_file():
    clang.cindex.Config.set_library_file(_BUNDLED)
from clang.cindex import CursorKind, Index, TranslationUnit

HERE = Path(__file__).resolve().parent
PORT_ROOT = HERE.parent
PSDK = (PORT_ROOT.parent / "porting-sdk").resolve()
if not PSDK.is_dir():
    PSDK = Path("/usr/local/home/devuser/src/porting-sdk")

sys.path.insert(0, str(HERE))
from enumerate_surface import (  # type: ignore
    CALLBACK_TYPEDEFS_AS_CALLABLE,
    CLASS_MODULE_MAP, CLASS_RENAME_MAP, MIXIN_PROJECTIONS, _METHOD_RENAMES,
    camel_to_snake, module_for_class, native_ns_to_module,
)


class TypeTranslationError(RuntimeError):
    pass


def load_aliases() -> dict[str, str]:
    data = yaml.safe_load((PSDK / "type_aliases.yaml").read_text(encoding="utf-8"))
    return {str(k): str(v) for k, v in data.get("aliases", {}).get("cpp", {}).items()}


# ---------------------------------------------------------------------------
# C++ type translation (clang canonical spelling)
# ---------------------------------------------------------------------------

GENERIC_RE = re.compile(r"^([A-Za-z_:][\w:]*)<(.+)>$")


def split_top_commas(s: str) -> list[str]:
    parts = []
    buf = []
    depth = 0
    for ch in s:
        if ch in "<({[":
            depth += 1
        elif ch in ">)}]":
            depth -= 1
        if ch == "," and depth == 0:
            parts.append("".join(buf).strip())
            buf.clear()
            continue
        buf.append(ch)
    if buf:
        parts.append("".join(buf).strip())
    return parts


def translate_cpp_type(t: str, aliases: dict[str, str], context: str) -> str:
    if t is None or t == "":
        return "any"
    t = t.strip()

    # Strip cv-qualifiers and reference/pointer modifiers
    while True:
        new_t = t
        for prefix in ("const ", "volatile ", "constexpr "):
            if new_t.startswith(prefix):
                new_t = new_t[len(prefix):].strip()
        for suffix in ("&&", "&", "*"):
            if new_t.endswith(suffix):
                new_t = new_t[:-len(suffix)].strip()
        if new_t == t:
            break
        t = new_t

    # Direct alias
    if t in aliases:
        return aliases[t]

    # Generic instantiation
    m = GENERIC_RE.match(t)
    if m:
        head, inner = m.group(1), m.group(2)
        if head in aliases:
            mapped = aliases[head]
            args = split_top_commas(inner)
            canon_args = [translate_cpp_type(a, aliases, context) for a in args]
            return mapped
        args = split_top_commas(inner)
        canon_args = [translate_cpp_type(a, aliases, context) for a in args]
        if head in ("std::vector", "std::list", "std::deque", "std::array"):
            return f"list<{canon_args[0]}>" if canon_args else "list<any>"
        if head in ("std::map", "std::unordered_map"):
            if len(canon_args) >= 2:
                return f"dict<{canon_args[0]},{canon_args[1]}>"
            return "dict<string,any>"
        if head in ("std::set", "std::unordered_set"):
            return f"list<{canon_args[0]}>" if canon_args else "list<any>"
        if head in ("std::optional", "boost::optional"):
            return f"optional<{canon_args[0]}>" if canon_args else "optional<any>"
        if head in ("std::shared_ptr", "std::unique_ptr", "std::weak_ptr"):
            inner_canon = canon_args[0] if canon_args else "any"
            return inner_canon
        if head in ("std::function",):
            # std::function<R(A,B,...)>
            if canon_args:
                # canon_args[0] is something like "ret(args)"; need to re-parse
                raw_arg = inner.strip()
                if "(" in raw_arg and raw_arg.endswith(")"):
                    ret_part, args_part = raw_arg.split("(", 1)
                    args_part = args_part[:-1]
                    ret = translate_cpp_type(ret_part, aliases, context)
                    if args_part.strip():
                        canon_a = [translate_cpp_type(a, aliases, context) for a in split_top_commas(args_part)]
                    else:
                        canon_a = []
                    return f"callable<list<{','.join(canon_a)}>,{ret}>"
            return "callable<list<any>,any>"
        if head in ("std::pair", "std::tuple"):
            return f"tuple<{','.join(canon_args)}>"
        if head in ("std::variant",):
            return f"union<{','.join(canon_args)}>"
        # SDK class with generic args
        if head.startswith("signalwire::") or head.startswith("SignalWire::"):
            return _translate_sdk_class_ref(head)
        # Unknown parameterized
        raise TypeTranslationError(
            f"unknown generic C++ type {head!r}<{inner}> at {context}"
        )

    # Bare class
    if t.startswith("signalwire::") or t.startswith("SignalWire::"):
        return _translate_sdk_class_ref(t)

    # Last segment
    last = t.rsplit("::", 1)[-1]
    if last in aliases:
        return aliases[last]

    # Heuristic class ref
    if last and last[0].isupper():
        return _translate_sdk_class_ref(t)

    raise TypeTranslationError(
        f"unknown C++ type {t!r} at {context}; "
        f"add to porting-sdk/type_aliases.yaml under aliases.cpp"
    )


def _build_rename_by_name() -> dict[str, tuple[str, str]]:
    """Index CLASS_RENAME_MAP by the C++ class name alone, for lookup
    when libclang emits a bare class spelling (no namespace prefix).
    Most of CLASS_RENAME_MAP keys are unique on the class-name side
    (``Service``, ``AddressesNamespace``, ``LogsMessages`` etc.), so a
    single-key index is sufficient. If a name is ambiguous across
    multiple namespaces in the map, prefer the first registration.
    """
    by_name: dict[str, tuple[str, str]] = {}
    for (ns, cls_name), (mod, py_cls) in CLASS_RENAME_MAP.items():
        by_name.setdefault(cls_name, (mod, py_cls))
    return by_name


_RENAME_BY_NAME = _build_rename_by_name()


def _translate_sdk_class_ref(t: str) -> str:
    """Translate a C++ qualified class spelling into the canonical
    ``class:<python_module>.<python_class>`` form used by both inventories.

    The order matters:
      1. CLASS_RENAME_MAP — full ``(namespace, class)`` -> ``(module, class)``
         override. Covers the C++/Python naming divergences (``Service`` ->
         ``SWMLService``, ``XxxNamespace`` -> ``XxxResource``, ``LogsXxx``
         -> ``XxxLogs``, ``FabricXxx`` -> ``XxxResource``, etc.). Applies
         BEFORE the unqualified CLASS_MODULE_MAP because some renames keep
         the class name in the map under its C++ spelling — without this
         pass the diff would still see the C++ name on returns.
      2. Bare-name fallback: when libclang emits a bare class spelling
         (``AddressesNamespace`` not ``signalwire::rest::AddressesNamespace``),
         consult the by-name index built from CLASS_RENAME_MAP. This
         covers C++ method return types where clang uses the unqualified
         name visible at the declaration scope.
      3. CLASS_MODULE_MAP — name-only lookup; the class name is the
         canonical Python name (``AgentBase``, ``FunctionResult``, etc.).
      4. ``module_for_class`` heuristic.
      5. Fallback to native namespace translation.
    """
    name = t.split("::")[-1]
    ns_path = "::".join(t.split("::")[:-1])
    # Strip leading "::" if present (clang sometimes emits global qualifier).
    ns_path = ns_path.lstrip(":")
    # Callback typedef → canonical ``class:Callable`` so Python's
    # ``typing.Callable`` and the C++ ``using XxxCallback = std::function<...>``
    # alias compare equal in the diff for methods where Python uses the
    # bare ``typing.Callable`` annotation. Listed in
    # CALLBACK_TYPEDEFS_AS_CALLABLE. Methods where Python uses a fully
    # parameterized ``callable<list<...>,ret>`` annotation are caught by
    # the post-build _project_callable_shape pass instead.
    if name in CALLBACK_TYPEDEFS_AS_CALLABLE:
        return "class:Callable"
    # Walk progressively-shorter namespace prefixes so we also catch the
    # case where libclang emits the class spelling as
    # ``signalwire::rest::RestClient::AddressesNamespace`` — the rename
    # table keys on ``(signalwire::rest, AddressesNamespace)``.
    ns_candidates = [ns_path] if ns_path else []
    parts = ns_path.split("::") if ns_path else []
    for i in range(len(parts) - 1, 0, -1):
        ns_candidates.append("::".join(parts[:i]))
    for ns in ns_candidates:
        if (ns, name) in CLASS_RENAME_MAP:
            target_mod, target_cls = CLASS_RENAME_MAP[(ns, name)]
            return f"class:{target_mod}.{target_cls}"
    # Bare-name fallback — clang emits the unqualified name for member-of
    # struct return types (RestClient::AddressesNamespace becomes just
    # "AddressesNamespace" in the cursor's result_type spelling).
    if name in _RENAME_BY_NAME:
        target_mod, target_cls = _RENAME_BY_NAME[name]
        return f"class:{target_mod}.{target_cls}"
    if name in CLASS_MODULE_MAP:
        return f"class:{CLASS_MODULE_MAP[name]}.{name}"
    mod = module_for_class(name, ns_path)
    if mod:
        return f"class:{mod}.{name}"
    return f"class:signalwire.{native_ns_to_module(ns_path)}.{name}" if ns_path else f"class:{name}"


# ---------------------------------------------------------------------------
# Walking the AST
# ---------------------------------------------------------------------------


def walk_translation_unit(tu: TranslationUnit, file_filter: Path) -> tuple[list[dict], list[dict]]:
    """Walk a clang TU and emit (class entries, free-function entries)."""
    entries: list[dict] = []
    free_functions: list[dict] = []

    def visit(cursor, ns_path: list[str]):
        if cursor.kind in (CursorKind.NAMESPACE,):
            new_ns = ns_path + [cursor.spelling]
            for child in cursor.get_children():
                visit(child, new_ns)
            return
        # Module-level / namespace-scope free functions. C++ exposes
        # signalwire::utils::url_validator::validate_url etc. as plain
        # FUNCTION_DECLs inside a namespace; lift them into the
        # canonical inventory's per-module ``functions`` map below.
        if cursor.kind == CursorKind.FUNCTION_DECL:
            try:
                fn = cursor.location.file
            except Exception:
                fn = None
            if fn is None or not str(fn.name).startswith(str(file_filter)):
                return
            fname = cursor.spelling
            if not fname or fname.startswith("_"):
                return
            params = []
            for arg in cursor.get_arguments():
                params.append({
                    "name": arg.spelling,
                    "type": arg.type.spelling,
                    "has_default": _has_default_value(arg),
                })
            free_functions.append({
                "namespace": "::".join(ns_path),
                "name": fname,
                "parameters": params,
                "return_type": cursor.result_type.spelling,
            })
            return
        if cursor.kind in (CursorKind.CLASS_DECL, CursorKind.STRUCT_DECL):
            if not cursor.is_definition():
                return
            # Only emit if defined in our headers
            try:
                fn = cursor.location.file
            except Exception:
                fn = None
            if fn is None or not str(fn.name).startswith(str(file_filter)):
                return
            class_name = cursor.spelling
            if not class_name:
                return
            ns_str = "::".join(ns_path)
            methods = []
            for child in cursor.get_children():
                if child.kind == CursorKind.CXX_METHOD:
                    if child.access_specifier.name != "PUBLIC":
                        continue
                    if child.spelling.startswith("_"):
                        continue
                    methods.append(extract_method(child, is_ctor=False))
                elif child.kind == CursorKind.CONSTRUCTOR:
                    if child.access_specifier.name != "PUBLIC":
                        continue
                    methods.append(extract_method(child, is_ctor=True))
                elif child.kind in (CursorKind.CLASS_DECL, CursorKind.STRUCT_DECL):
                    # Nested class / struct (RestClient::FabricNamespace,
                    # RestClient::CallingNamespace, etc.). Visit it under
                    # the SAME namespace path so it surfaces as a
                    # standalone class — Python represents these as
                    # peer classes (CallingNamespace), not children of
                    # RestClient.
                    if child.access_specifier.name == "PUBLIC":
                        visit(child, ns_path)
            if methods:
                entries.append({
                    "namespace": ns_str,
                    "name": class_name,
                    "methods": methods,
                })
            return
        # Recurse into other top-level structures
        for child in cursor.get_children():
            visit(child, ns_path)

    visit(tu.cursor, [])
    return entries, free_functions


def extract_method(cursor, is_ctor: bool) -> dict:
    params = []
    for arg in cursor.get_arguments():
        params.append({
            "name": arg.spelling,
            "type": arg.type.spelling,
            "has_default": _has_default_value(arg),
        })
    return {
        "name": "<init>" if is_ctor else cursor.spelling,
        "is_constructor": is_ctor,
        "is_static": cursor.is_static_method() if not is_ctor else False,
        "parameters": params,
        "return_type": "void" if is_ctor else cursor.result_type.spelling,
    }


def _has_default_value(arg) -> bool:
    """Heuristic: scan tokens for '=' after the arg name."""
    try:
        tokens = list(arg.get_tokens())
    except Exception:
        return False
    return any(tok.spelling == "=" for tok in tokens)


# ---------------------------------------------------------------------------
# Building canonical inventory
# ---------------------------------------------------------------------------


def collect(
    raw_entries: list[dict],
    aliases: dict,
    raw_free_functions: list[dict] | None = None,
) -> tuple[dict, list]:
    out_modules: dict = {}
    failures: list = []
    raw_free_functions = raw_free_functions or []

    by_class: dict = {}
    for entry in raw_entries:
        ns = entry["namespace"]
        name = entry["name"]
        key = (ns, name)
        if key in by_class:
            # Merge methods (e.g. when class is split across translation units)
            by_class[key]["methods"].extend(entry["methods"])
        else:
            by_class[key] = {"namespace": ns, "name": name, "methods": list(entry["methods"])}

    for (ns, name), entry in by_class.items():
        # Check CLASS_RENAME_MAP first: (cpp_namespace, cpp_class) →
        # (python_module, python_class). Used for Service → SWMLService etc.
        rename_key = (ns, name)
        if rename_key in CLASS_RENAME_MAP:
            mod, name = CLASS_RENAME_MAP[rename_key]
        elif name not in CLASS_MODULE_MAP:
            mod = module_for_class(name, ns)
            if mod is None:
                continue
        else:
            mod = CLASS_MODULE_MAP[name]

        methods_out: dict = {}
        for m in entry["methods"]:
            native = m["name"]
            if native == "<init>":
                method_canonical = "__init__"
            else:
                if native.startswith("_"):
                    continue
                if native.startswith("operator"):
                    # C++ operator overloads (operator=, operator<<, etc.)
                    # don't map to Python's signature inventory.
                    continue
                method_canonical = camel_to_snake(native)
                # Map C++ keyword-avoidance trailing underscore methods
                # (delete_, etc.) back to Python's unsuffixed names so the
                # diff lines up.
                method_canonical = _METHOD_RENAMES.get(method_canonical, method_canonical)
            ctx = f"{mod}.{name}.{method_canonical}"
            try:
                sig = build_signature(m, aliases, ctx)
            except TypeTranslationError as e:
                failures.append(str(e))
                continue
            if method_canonical in methods_out:
                # Prefer fewer-param overload
                existing = methods_out[method_canonical]
                if len(sig["params"]) >= len(existing["params"]):
                    continue
            methods_out[method_canonical] = sig

        if not methods_out:
            continue
        # Synthesize __init__ when libclang didn't surface an explicit
        # constructor — POD structs / classes with only the implicit
        # default constructor still ARE constructible. Without this,
        # every such class shows up with a missing-port __init__ even
        # when port code can construct it. Matches the Perl adapter's
        # synthetic __init__ for Moo classes.
        if "__init__" not in methods_out:
            methods_out["__init__"] = {
                "params": [{"name": "self", "kind": "self"}],
                "returns": "void",
            }
        out_modules.setdefault(mod, {"classes": {}})
        out_modules[mod]["classes"].setdefault(name, {"methods": {}})
        out_modules[mod]["classes"][name]["methods"].update(methods_out)

    # Mixin projection — methods may live on AgentBase OR SWMLService
    # (Service is the parent class; many tool/auth/state helpers are
    # declared on Service and inherited by AgentBase).
    #
    # For synthetic-class projection targets (PromptManager, ToolRegistry —
    # which Python implements as separate composition classes with their
    # own fluent ``-> Self`` returns), retarget AgentBase fluent returns
    # to the projection target. The diff's fluent ``void ≡ class:Self``
    # rule then matches Python's ``-> None`` against the C++ class-typed
    # return.
    #
    # For mixin classes (AIConfigMixin, PromptMixin, ...) Python's
    # methods are all defined on AgentBase and return ``AgentBase``; the
    # mixin class is just an interface marker. Don't retarget for those —
    # leaving the C++ AgentBase return matches Python's AgentBase return.
    ab_entry = out_modules.get("signalwire.core.agent_base", {}).get("classes", {}).get("AgentBase")
    svc_entry = out_modules.get("signalwire.core.swml_service", {}).get("classes", {}).get("SWMLService")
    if ab_entry or svc_entry:
        ab_methods = ab_entry["methods"] if ab_entry else {}
        svc_methods = svc_entry["methods"] if svc_entry else {}
        combined = {**svc_methods, **ab_methods}
        projected = set()
        AGENT_BASE_RET = "class:signalwire.core.agent_base.AgentBase"
        SWML_SERVICE_RET = "class:signalwire.core.swml_service.SWMLService"
        SERVICE_NS_RET = "class:signalwire.service.Service"
        # Synthetic-class projection targets — retarget fluent returns.
        SYNTHETIC_PROJECTION_TARGETS = {
            ("signalwire.core.agent.prompt.manager", "PromptManager"),
            ("signalwire.core.agent.tools.registry", "ToolRegistry"),
        }
        for (target_mod, target_cls), expected in MIXIN_PROJECTIONS.items():
            present_raw = {m: combined[m] for m in expected if m in combined}
            if not present_raw:
                continue
            target_ret = f"class:{target_mod}.{target_cls}"
            retarget_returns = (target_mod, target_cls) in SYNTHETIC_PROJECTION_TARGETS
            present: dict = {}
            for m, sig in present_raw.items():
                if retarget_returns:
                    ret = sig.get("returns", "")
                    if ret in (AGENT_BASE_RET, SWML_SERVICE_RET, SERVICE_NS_RET):
                        sig = dict(sig)
                        sig["returns"] = target_ret
                present[m] = sig
            out_modules.setdefault(target_mod, {"classes": {}})
            out_modules[target_mod]["classes"].setdefault(target_cls, {"methods": {}})
            out_modules[target_mod]["classes"][target_cls]["methods"].update(present)
            projected.update(present)
        for n in projected:
            ab_methods.pop(n, None)
        if ab_entry and not ab_methods:
            out_modules["signalwire.core.agent_base"]["classes"].pop("AgentBase", None)
            if not out_modules["signalwire.core.agent_base"]["classes"]:
                out_modules.pop("signalwire.core.agent_base")

    # Free-function projection: lift namespace-scope C++ functions onto
    # Python module-level functions. Only emit functions whose canonical
    # module + name appear in the Python reference's
    # ``modules.X.functions`` map — port-only extras flow through
    # PORT_ADDITIONS.md instead. This also drops any free function whose
    # parent namespace doesn't translate cleanly.
    ref_free_fn_targets = _load_python_free_function_targets()
    for entry in raw_free_functions:
        ns = entry["namespace"]
        if not ns.startswith("signalwire"):
            continue
        py_module = native_ns_to_module(ns) or ns.replace("::", ".")
        if not py_module.startswith("signalwire"):
            py_module = "signalwire." + py_module
        # camel_to_snake: C++ module functions are usually snake-case
        # already, but Python's top-level ``signalwire.RestClient`` is a
        # PascalCase factory function (mirroring the class name). Preserve
        # that when the C++ source-side function is also PascalCase.
        if entry["name"][:1].isupper():
            fname = entry["name"]
        else:
            fname = camel_to_snake(entry["name"])
        if (py_module, fname) not in ref_free_fn_targets:
            continue
        ctx = f"{py_module}.{fname}"
        try:
            sig = build_signature(
                {
                    "name": entry["name"],
                    "is_static": True,  # free functions have no receiver
                    "is_constructor": False,
                    "parameters": entry["parameters"],
                    "return_type": entry["return_type"],
                },
                aliases,
                ctx,
            )
        except TypeTranslationError as e:
            failures.append(str(e))
            continue
        out_modules.setdefault(py_module, {"classes": {}})
        out_modules[py_module].setdefault("functions", {})
        out_modules[py_module]["functions"][fname] = sig

    # Python-shape projection: when the Python reference uses ``**kwargs``
    # (kind=var_keyword) for a method's last param, and the C++ port has a
    # corresponding trailing positional ``nlohmann::json``-typed (i.e.
    # canonical type ``any``) parameter, retype the C++ param as
    # ``dict<string, any>``. The diff tool already accepts
    # ``positional dict<string,...>`` as equivalent to Python's
    # ``var_keyword`` — this projection just makes that equivalence apply
    # to the C++ idiom of using ``const json& params`` instead of an
    # explicit dict-of-string parameter. Without this projection, every
    # such method shows up as a kind+type mismatch even though the
    # contract is identical.
    _project_kwargs_shape(out_modules)

    # Callable-shape projection: when the Python reference uses a fully
    # parameterized ``callable<list<...>,ret>`` annotation but the C++
    # port emits a bare ``class:Callable`` (because the C++ side uses an
    # opaque ``std::function``-aliased typedef that we can't structurally
    # recover), retype the C++ param to match Python's callable shape.
    # The two describe the same callable contract — the C++ side has a
    # less-precise type representation, not a different one.
    _project_callable_shape(out_modules)

    sorted_modules = {}
    for k in sorted(out_modules):
        entry = out_modules[k]
        out_entry: dict = {}
        if entry.get("classes"):
            out_entry["classes"] = {
                cls: {"methods": dict(sorted(entry["classes"][cls]["methods"].items()))}
                for cls in sorted(entry["classes"])
            }
        if entry.get("functions"):
            out_entry["functions"] = {
                fn: entry["functions"][fn] for fn in sorted(entry["functions"])
            }
        if out_entry:
            sorted_modules[k] = out_entry
    return {
        "version": "2",
        "generated_from": "signalwire-cpp via libclang",
        "modules": sorted_modules,
    }, failures


def _project_kwargs_shape(out_modules: dict) -> None:
    """Align C++ kwargs-style trailing positional params with Python's
    ``**kwargs`` idiom so the cross-language diff treats them as the
    same callable contract.

    For every method emitted to ``out_modules``, look up the Python
    reference signature. If Python's last param is ``var_keyword`` and
    the corresponding C++ param has type ``any`` (the canonical for
    ``nlohmann::json``) and is the LAST positional parameter, project
    its type to ``dict<string,any>`` so the diff's existing rule
    (positional ``dict<string,*>`` ≡ Python ``**kwargs``) fires.

    Only retype: don't change the kind. The diff's rule keys on
    port_kind=positional + port_type starts-with ``dict<string,``, so
    this projection is enough.
    """
    ref = _load_python_signatures()
    if not ref:
        return
    ref_methods: dict[tuple[str, str, str], dict] = {}
    for mod, entry in ref.get("modules", {}).items():
        for cls, c in entry.get("classes", {}).items():
            for meth, sig in c.get("methods", {}).items():
                ref_methods[(mod, cls, meth)] = sig
    ref_fns: dict[tuple[str, str], dict] = {}
    for mod, entry in ref.get("modules", {}).items():
        for fn, sig in entry.get("functions", {}).items():
            ref_fns[(mod, fn)] = sig

    def project_one(port_sig: dict, ref_sig: dict) -> None:
        ref_params = ref_sig.get("params", [])
        port_params = port_sig.get("params", [])
        if not ref_params or not port_params:
            return
        if ref_params[-1].get("kind") != "var_keyword":
            return
        # Find the trailing port param (skip self).
        last = port_params[-1]
        if last.get("kind") in ("self", "cls"):
            return
        if last.get("type") != "any":
            return
        # Project: retype to dict<string,any> so the diff's positional/
        # dict<string,*> ≡ var_keyword rule fires.
        last["type"] = "dict<string,any>"

    for mod, entry in out_modules.items():
        for cls, c in entry.get("classes", {}).items():
            for meth, sig in c.get("methods", {}).items():
                ref_sig = ref_methods.get((mod, cls, meth))
                if ref_sig:
                    project_one(sig, ref_sig)
        for fn, sig in entry.get("functions", {}).items():
            ref_sig = ref_fns.get((mod, fn))
            if ref_sig:
                project_one(sig, ref_sig)


def _project_callable_shape(out_modules: dict) -> None:
    """Align C++ ``class:Callable`` with Python's ``callable<...>`` shape.

    When the C++ side emits a ``class:Callable`` placeholder for a
    callback typedef (no signature info because the typedef hides
    ``std::function``'s parameter list), but Python annotates the same
    parameter with a fully-shaped ``callable<list<...>,ret>``, copy the
    Python shape onto the C++ param. The two describe the same
    contract; the diff tool's ``head_ref != head_port`` rule otherwise
    flags it as drift.

    Same logic for return types.
    """
    ref = _load_python_signatures()
    if not ref:
        return
    ref_methods: dict[tuple[str, str, str], dict] = {}
    for mod, entry in ref.get("modules", {}).items():
        for cls, c in entry.get("classes", {}).items():
            for meth, sig in c.get("methods", {}).items():
                ref_methods[(mod, cls, meth)] = sig
    ref_fns: dict[tuple[str, str], dict] = {}
    for mod, entry in ref.get("modules", {}).items():
        for fn, sig in entry.get("functions", {}).items():
            ref_fns[(mod, fn)] = sig

    def project_one(port_sig: dict, ref_sig: dict) -> None:
        port_params = port_sig.get("params", [])
        ref_params = ref_sig.get("params", [])
        for i, p in enumerate(port_params):
            if p.get("type") != "class:Callable":
                continue
            if i >= len(ref_params):
                continue
            ref_t = ref_params[i].get("type", "")
            if ref_t.startswith("callable<"):
                p["type"] = ref_t
        # Returns
        if port_sig.get("returns") == "class:Callable":
            ref_ret = ref_sig.get("returns", "")
            if ref_ret.startswith("callable<"):
                port_sig["returns"] = ref_ret

    for mod, entry in out_modules.items():
        for cls, c in entry.get("classes", {}).items():
            for meth, sig in c.get("methods", {}).items():
                ref_sig = ref_methods.get((mod, cls, meth))
                if ref_sig:
                    project_one(sig, ref_sig)
        for fn, sig in entry.get("functions", {}).items():
            ref_sig = ref_fns.get((mod, fn))
            if ref_sig:
                project_one(sig, ref_sig)


def _load_python_signatures() -> dict:
    try:
        return json.loads((PSDK / "python_signatures.json").read_text(encoding="utf-8"))
    except FileNotFoundError:
        return {}


def _load_python_free_function_targets() -> set[tuple[str, str]]:
    """Read the Python reference's module-level ``functions`` map so the
    walker only emits things the Python oracle also exposes at module
    level. Keeps port-only extras out of the canonical inventory; they
    belong in PORT_ADDITIONS.md."""
    targets: set[tuple[str, str]] = set()
    try:
        ref = json.loads((PSDK / "python_signatures.json").read_text(encoding="utf-8"))
    except FileNotFoundError:
        return targets
    for mod, entry in ref.get("modules", {}).items():
        for fn in (entry.get("functions") or {}).keys():
            targets.add((mod, fn))
    return targets


def build_signature(method: dict, aliases: dict, context: str) -> dict:
    params_out: list = []
    is_static = method.get("is_static", False)
    is_ctor = method.get("is_constructor", False)
    if not is_static:
        params_out.append({"name": "self", "kind": "self"})
    for p in method.get("parameters", []):
        ctx = f"{context}[{p.get('name')}]"
        canon_type = translate_cpp_type(p.get("type", ""), aliases, ctx)
        param: dict = {
            "name": p.get("name", "_") or "_",
            "type": canon_type,
        }
        if p.get("has_default"):
            param["required"] = False
            param["default"] = None
        else:
            param["required"] = True
        params_out.append(param)
    return_canon = "void" if is_ctor else translate_cpp_type(method.get("return_type", "void"), aliases, context + "[->]")
    return {"params": params_out, "returns": return_canon}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--include", type=Path, default=PORT_ROOT / "include")
    parser.add_argument("--out", type=Path, default=PORT_ROOT / "port_signatures.json")
    parser.add_argument("--strict", action="store_true")
    args = parser.parse_args()

    aliases = load_aliases()

    index = Index.create()
    headers = sorted(args.include.rglob("*.hpp")) + sorted(args.include.rglob("*.h"))
    raw_entries: list[dict] = []
    raw_free_functions: list[dict] = []
    parse_args = ["-x", "c++", "-std=c++17", f"-I{args.include}"]
    # Project deps (e.g. nlohmann/json.hpp) live under deps/ at the SDK
    # root.  Without -Ideps libclang resolves the bundled ``json``
    # typedef to a fallback primitive (``int``), corrupting type
    # extraction for set_param / set_global_data / set_prompt_pom etc.
    deps_dir = PORT_ROOT / "deps"
    if deps_dir.is_dir():
        parse_args.append(f"-I{deps_dir}")
    # libclang ships without the host stdlib headers; pull them in from
    # the system gcc install if present so <cstddef> et al. resolve and
    # nlohmann::json doesn't degrade to ``int``.
    for gcc_inc in (
        "/usr/lib/gcc/x86_64-linux-gnu/10/include",
        "/usr/lib/gcc/x86_64-linux-gnu/12/include",
        "/usr/lib/gcc/x86_64-linux-gnu/13/include",
    ):
        if Path(gcc_inc).is_dir():
            parse_args.append(f"-I{gcc_inc}")
            break
    for header in headers:
        try:
            tu = index.parse(str(header), args=parse_args, options=TranslationUnit.PARSE_DETAILED_PROCESSING_RECORD)
        except Exception as e:
            print(f"skip {header}: {e}", file=sys.stderr)
            continue
        cls_entries, fn_entries = walk_translation_unit(tu, args.include)
        raw_entries.extend(cls_entries)
        raw_free_functions.extend(fn_entries)

    canonical, failures = collect(raw_entries, aliases, raw_free_functions)
    if failures:
        print(f"enumerate_signatures: {len(failures)} translation failure(s)", file=sys.stderr)
        for f in failures[:30]:
            print(f"  - {f}", file=sys.stderr)
        if len(failures) > 30:
            print(f"  ... ({len(failures) - 30} more)", file=sys.stderr)
        if args.strict:
            return 1

    args.out.write_text(json.dumps(canonical, indent=2, sort_keys=False) + "\n", encoding="utf-8")
    n_mods = len(canonical["modules"])
    n_methods = sum(sum(len(c["methods"]) for c in m.get("classes", {}).values()) for m in canonical["modules"].values())
    print(f"enumerate_signatures: wrote {args.out} ({n_mods} modules, {n_methods} methods)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
