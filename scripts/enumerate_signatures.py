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
    CLASS_MODULE_MAP, CLASS_RENAME_MAP, MIXIN_PROJECTIONS, camel_to_snake,
    module_for_class, native_ns_to_module,
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


def _translate_sdk_class_ref(t: str) -> str:
    name = t.split("::")[-1]
    if name in CLASS_MODULE_MAP:
        return f"class:{CLASS_MODULE_MAP[name]}.{name}"
    ns_path = "::".join(t.split("::")[:-1])
    mod = module_for_class(name, ns_path)
    if mod:
        return f"class:{mod}.{name}"
    return f"class:signalwire.{native_ns_to_module(ns_path)}.{name}" if ns_path else f"class:{name}"


# ---------------------------------------------------------------------------
# Walking the AST
# ---------------------------------------------------------------------------


def walk_translation_unit(tu: TranslationUnit, file_filter: Path) -> list[dict]:
    """Walk a clang TU and emit raw type entries (one per class)."""
    entries: list[dict] = []

    def visit(cursor, ns_path: list[str]):
        if cursor.kind in (CursorKind.NAMESPACE,):
            new_ns = ns_path + [cursor.spelling]
            for child in cursor.get_children():
                visit(child, new_ns)
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
    return entries


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


def collect(raw_entries: list[dict], aliases: dict) -> tuple[dict, list]:
    out_modules: dict = {}
    failures: list = []

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

    # Mixin projection
    ab_entry = out_modules.get("signalwire.core.agent_base", {}).get("classes", {}).get("AgentBase")
    if ab_entry:
        ab_methods = ab_entry["methods"]
        projected = set()
        for (target_mod, target_cls), expected in MIXIN_PROJECTIONS.items():
            present = {m: ab_methods[m] for m in expected if m in ab_methods}
            if not present:
                continue
            out_modules.setdefault(target_mod, {"classes": {}})
            out_modules[target_mod]["classes"].setdefault(target_cls, {"methods": {}})
            out_modules[target_mod]["classes"][target_cls]["methods"].update(present)
            projected.update(present)
        for n in projected:
            ab_methods.pop(n, None)
        if not ab_methods:
            out_modules["signalwire.core.agent_base"]["classes"].pop("AgentBase", None)
            if not out_modules["signalwire.core.agent_base"]["classes"]:
                out_modules.pop("signalwire.core.agent_base")

    sorted_modules = {}
    for k in sorted(out_modules):
        entry = out_modules[k]
        sorted_modules[k] = {
            "classes": {
                cls: {"methods": dict(sorted(entry["classes"][cls]["methods"].items()))}
                for cls in sorted(entry["classes"])
            }
        }
    return {
        "version": "2",
        "generated_from": "signalwire-cpp via libclang",
        "modules": sorted_modules,
    }, failures


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
    parse_args = ["-x", "c++", "-std=c++17", f"-I{args.include}"]
    for header in headers:
        try:
            tu = index.parse(str(header), args=parse_args, options=TranslationUnit.PARSE_DETAILED_PROCESSING_RECORD)
        except Exception as e:
            print(f"skip {header}: {e}", file=sys.stderr)
            continue
        raw_entries.extend(walk_translation_unit(tu, args.include))

    canonical, failures = collect(raw_entries, aliases)
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
