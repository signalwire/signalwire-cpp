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
import subprocess
import sys
from pathlib import Path

import yaml

# Configure libclang BEFORE importing clang.cindex Index.create()
import clang.cindex
import importlib.util as _ilu
import os as _os
import sysconfig as _sc

def _macos_llvm_prefixes() -> list[str]:
    """Candidate Homebrew LLVM install prefixes on macOS, newest-pinned first.

    A versioned `llvm@NN` keg has a libclang.dylib whose builtin + libc++
    headers exactly match its version — the matched set we want. `xcrun -f`
    is NOT used here because Apple's bundled clang doesn't expose libclang.dylib.
    """
    prefixes: list[str] = []
    # `brew --prefix llvm` / versioned kegs, without requiring brew on PATH:
    # probe the standard Apple-silicon and Intel Cellar/opt layouts.
    import glob as _glob
    for base in ("/opt/homebrew/opt", "/usr/local/opt"):
        # Prefer an explicitly versioned keg (llvm@18, llvm@19, …) then plain llvm.
        prefixes += sorted(_glob.glob(f"{base}/llvm@*"), reverse=True)
        prefixes.append(f"{base}/llvm")
    return prefixes


def _macos_clang_args(libclang_path: str | None) -> list[str]:
    """Extra libclang parse args so C++ stdlib + builtin headers resolve on macOS.

    Without these, `<cstddef>`/`<bit>`/etc. don't resolve and every type that
    touches them degrades to `int`. The working combination (verified):
      -isysroot <SDK>            base C library (stdint.h, …)
      -nostdinc++                ignore the SDK's libc++ (avoids version skew)
      -isystem <llvm libc++>     use the matched libc++ that ships with libclang
      -isystem <llvm builtins>   the resource-dir builtins (stdarg.h, __builtin_*)
    Derived from the same LLVM prefix that provided libclang.dylib so the dylib
    and headers are one self-consistent toolchain.
    """
    import glob as _glob
    args: list[str] = []
    try:
        sdk = subprocess.run(
            ["xcrun", "--show-sdk-path"], capture_output=True, text=True, check=True
        ).stdout.strip()
        if sdk and Path(sdk).is_dir():
            args += ["-isysroot", sdk]
    except (OSError, subprocess.CalledProcessError) as e:
        print(f"enumerate_signatures: xcrun --show-sdk-path failed ({e})", file=sys.stderr)
    # Find a libc++ + builtin (resource) header pair. Prefer the prefix that
    # provided libclang.dylib (a self-consistent toolchain); but the pip
    # `libclang` package's dylib lives in clang/native/ with NO headers — in
    # that case fall back to any Homebrew LLVM keg that has both, so the audit
    # doesn't silently degrade types to `int`.
    def _libcxx_and_builtins(prefix: Path):
        libcxx = prefix / "include" / "c++" / "v1"
        builtins = sorted(_glob.glob(str(prefix / "lib" / "clang" / "*" / "include")), reverse=True)
        return (str(libcxx), builtins[0]) if libcxx.is_dir() and builtins else None
    found = None
    if libclang_path:
        found = _libcxx_and_builtins(Path(libclang_path).parent.parent)  # <prefix>/lib/libclang.dylib
    if not found:
        for prefix in _macos_llvm_prefixes():
            found = _libcxx_and_builtins(Path(prefix))
            if found:
                break
    if found:
        libcxx, builtins = found
        args += ["-nostdinc++", "-isystem", libcxx, "-isystem", builtins]
    else:
        print("enumerate_signatures: no matched libc++/builtin headers found on "
              "macOS (install a Homebrew `llvm` keg); C++ types may degrade to int",
              file=sys.stderr)
    return args


def _find_libclang() -> str | None:
    """Find a usable libclang.so across local-dev / pip / system layouts."""
    # 0. Explicit override via env var — wins over everything. CI sets this
    #    when an exact path is known (avoids guessing which of `libclang`
    #    vs `clang` pip packages has the matching .so version).
    env_path = _os.environ.get("SW_LIBCLANG_PATH")
    if env_path and Path(env_path).is_file():
        return env_path
    # 0b. macOS: prefer a Homebrew LLVM whose libclang.dylib ships ALONGSIDE its
    #     own libc++ + builtin (resource) headers. Pairing the dylib with a
    #     *matching* header set is essential — mixing pip's older libclang
    #     dylib with the newer macOS SDK libc++ makes types silently degrade to
    #     `int` (missing builtins like __builtin_clzg). _macos_clang_args()
    #     below derives -isysroot / -nostdinc++ / -isystem from this same prefix.
    if sys.platform == "darwin":
        for prefix in _macos_llvm_prefixes():
            cand = Path(prefix) / "lib" / "libclang.dylib"
            if cand.is_file():
                return str(cand)
    # 1. pip's `libclang` package — bundles libclang.so at
    #    <site-packages>/libclang/native/libclang.so. Newer than pip's
    #    `clang` package's bundled .so so always prefer it. Look it up
    #    by walking site-packages directly because find_spec on this
    #    package can return origin=None depending on pip layout.
    for site_dir in (_sc.get_paths().get("purelib"), _sc.get_paths().get("platlib")):
        if site_dir:
            cand = Path(site_dir) / "libclang" / "native" / "libclang.so"
            if cand.is_file():
                return str(cand)
    # 2. `clang` PyPI package's bundled .so — older, often missing newer
    #    symbols (clang_getOffsetOfBase). Used only if `libclang` isn't
    #    installed.
    try:
        spec = _ilu.find_spec("clang")
        if spec and spec.origin:
            cand = Path(spec.origin).parent / "native" / "libclang.so"
            if cand.is_file():
                return str(cand)
    except (ImportError, ValueError):
        pass
    # 3. System-installed via apt: `libclang-dev` provides
    #    /usr/lib/x86_64-linux-gnu/libclang-NN.so.1 (and sometimes the
    #    unversioned `libclang.so` symlink).
    for cand in (
        "/usr/lib/x86_64-linux-gnu/libclang.so",
        "/usr/lib/x86_64-linux-gnu/libclang-18.so.1",
        "/usr/lib/x86_64-linux-gnu/libclang-17.so.1",
        "/usr/lib/x86_64-linux-gnu/libclang-16.so.1",
        "/usr/lib/x86_64-linux-gnu/libclang-15.so.1",
        # Local-dev fallback: the clang python bindings' bundled native lib,
        # derived from $HOME so it is machine-agnostic.
        str(Path.home() / ".local/lib/python3.12/site-packages/clang/native/libclang.so"),
    ):
        if Path(cand).is_file():
            return cand
    return None

_LIBCLANG = _find_libclang()
if _LIBCLANG:
    clang.cindex.Config.set_library_file(_LIBCLANG)
from clang.cindex import CursorKind, Index, TranslationUnit

HERE = Path(__file__).resolve().parent
PORT_ROOT = HERE.parent
PSDK = Path(os.environ["PORTING_SDK_DIR"]).resolve() if os.environ.get("PORTING_SDK_DIR") \
    else (PORT_ROOT.parent / "porting-sdk").resolve()

sys.path.insert(0, str(HERE))
from enumerate_surface import (  # type: ignore
    CALLBACK_TYPEDEFS_AS_CALLABLE,
    CLASS_MODULE_MAP, CLASS_RENAME_MAP, FREE_FUNCTION_RENAMES,
    MIXIN_PROJECTIONS, _METHOD_RENAMES,
    camel_to_snake, module_for_class, native_ns_to_module,
)

# Methods whose canonical name should resolve to the OVERLOAD WITH THE MOST
# PARAMETERS, overriding the default fewest-param dedup.
#
# Default policy is "prefer the smallest-arity overload" — it keeps the audit
# honest when a C++ class adds extra convenience overloads with more knobs than
# Python exposes (we don't want a port to look like it has more API than the
# reference). But some C++ methods deliberately ship BOTH a flat positional
# overload that mirrors Python's full signature 1:1 AND an idiomatic
# options-struct convenience overload that DELEGATES to it. For those, the flat
# (max-arity) overload is the one that lines up with the reference; the
# convenience wrapper is a strict subset (fewer adapter-visible params). Picking
# the wrapper would falsely report a param-count gap even though the full
# capability is present. Keyed by the canonical ``module.Class.method`` path.
PREFER_FULL_OVERLOAD = {
    "signalwire.core.function_result.FunctionResult.join_conference",
}

# Methods whose canonical name should resolve to the TYPED (enum-class)
# overload, overriding the default arity-based dedup when two overloads are
# the SAME arity but differ only in whether a closed-set parameter is a bare
# ``std::string`` or a typed ``enum class``.
#
# Wave-1 closed-set contract (2026-06-05): the reference oracle now emits
# ``enum<...>`` for the four strongly-grounded closed sets
# (``record_call(format, direction)`` + ``tap(direction, codec)``), and
# diff_port_signatures.py REQUIRES a typed port form (``class:``/``enum``/
# ``union``) for them, not a bare ``string``. These methods each ship TWO
# equal-arity overloads: the flat ``std::string`` form (Python-parity, the
# forward-compat wire path) AND a typed form whose closed-set params are the
# ``RecordFormat``/``RecordDirection``/``TapDirection``/``Codec`` enum classes
# (which translate to ``class:...`` refs). Default dedup breaks an arity tie by
# insertion order, which keeps the string overload (declared first) canonical
# and so surfaces a bare ``string`` -> drift. For these, prefer the overload
# that types MORE of its params (the enum form) so the closed-set params surface
# as ``class:...`` and satisfy the oracle's ``enum<...>``; the string overload
# becomes a port-only convenience addition (documented in PORT_ADDITIONS.md).
# Keyed by the canonical ``module.Class.method`` path.
PREFER_TYPED_OVERLOAD = {
    "signalwire.core.function_result.FunctionResult.record_call",
    "signalwire.core.function_result.FunctionResult.tap",
}


def _typed_param_count(sig: dict) -> int:
    """Count params whose canonical type is a *typed* closed-set form — a
    ``class:`` ref (port enum/typed-const), a port ``enum<...>``, or a
    ``union<...>``. Used to break an equal-arity overload tie toward the
    overload that renders its closed-set params with a real type rather than a
    bare ``string`` (the wave-1 closed-set contract)."""
    n = 0
    for p in sig.get("params", []):
        t = p.get("type") or ""
        if t.startswith("class:") or t.startswith("enum<") or t.startswith("union<"):
            n += 1
    return n


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
                    # Carry the canonical spelling alongside the
                    # typedef-aware spelling so the translator can fall
                    # back to it when a typedef (e.g. ``ParamsOrBody``
                    # over ``std::variant<...>``) is opaque to its
                    # bare-name lookup. Class methods use the same trick
                    # via ``extract_method``.
                    "canonical_type": arg.type.get_canonical().spelling,
                    "has_default": _has_default_value(arg),
                })
            free_functions.append({
                "namespace": "::".join(ns_path),
                "name": fname,
                "parameters": params,
                "return_type": cursor.result_type.spelling,
                "canonical_return_type": cursor.result_type.get_canonical().spelling,
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
            # Carry the canonical (typedef-expanded) spelling so the
            # translator can fall back to it when a port-internal
            # typedef hides a known type. See
            # ``_translate_with_canonical_fallback``.
            "canonical_type": arg.type.get_canonical().spelling,
            "has_default": _has_default_value(arg),
        })
    return {
        "name": "<init>" if is_ctor else cursor.spelling,
        "is_constructor": is_ctor,
        "is_static": cursor.is_static_method() if not is_ctor else False,
        "parameters": params,
        "return_type": "void" if is_ctor else cursor.result_type.spelling,
        "canonical_return_type": (
            "void" if is_ctor else cursor.result_type.get_canonical().spelling
        ),
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
                existing = methods_out[method_canonical]
                if ctx in PREFER_TYPED_OVERLOAD:
                    # Equal-arity string-vs-enum overloads: keep the one that
                    # TYPES more params (the enum-class form), so its closed-set
                    # params surface as ``class:...`` and satisfy the oracle's
                    # ``enum<...>``. Fall back to the default fewer-param rule
                    # only when the typed-param count ties (e.g. genuinely
                    # different arities sneak in). The bare-string overload is a
                    # port-only convenience addition (PORT_ADDITIONS.md).
                    new_typed = _typed_param_count(sig)
                    old_typed = _typed_param_count(existing)
                    if new_typed < old_typed:
                        continue
                    if new_typed == old_typed and \
                            len(sig["params"]) >= len(existing["params"]):
                        continue
                elif ctx in PREFER_FULL_OVERLOAD:
                    # Keep the LARGER-arity overload (the flat form that
                    # mirrors Python's full signature); drop the convenience
                    # options-struct wrapper.
                    if len(sig["params"]) <= len(existing["params"]):
                        continue
                else:
                    # Default: prefer the fewer-param overload.
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
        # Free-function module/name override: a single registry that maps
        # the C++ (namespace, function-name) to the Python (module, name)
        # the reference inventory exposes. Used when the canonical
        # ``native_ns_to_module + camel/Pascal`` rule doesn't land on the
        # right Python module — for example
        # ``signalwire::security::ValidateWebhookSignature`` lives under
        # ``signalwire.core.security.webhook_validator`` in Python because
        # that's where the validator module sits in the reference repo.
        override = FREE_FUNCTION_RENAMES.get((ns, entry["name"]))
        if override is not None:
            py_module, fname = override
        else:
            py_module = native_ns_to_module(ns) or ns.replace("::", ".")
            if not py_module.startswith("signalwire"):
                py_module = "signalwire." + py_module
            # camel_to_snake: C++ module functions are usually snake-case
            # already, but Python's top-level ``signalwire.RestClient`` is
            # a PascalCase factory function (mirroring the class name).
            # Preserve that when the C++ source-side function is also
            # PascalCase.
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

    # webhook_middleware.validate: the Python oracle marks the trailing
    # ``signing_key`` argument keyword-only (``*, signing_key``). C++ has no
    # keyword-only parameters, but its signature is otherwise identical
    # (method, url, headers, body, signing_key). Mark the trailing param
    # ``keyword`` so the decomposed webhook-validation core compares EQUAL
    # to the oracle — the difference is Python's call-site sugar, not a
    # contract divergence (porting-sdk webhooks.md + HIDDEN_SURFACE_AUDIT).
    _wm = out_modules.get("signalwire.core.security.webhook_middleware", {})
    _validate_sig = _wm.get("functions", {}).get("validate")
    if _validate_sig and _validate_sig.get("params"):
        _last = _validate_sig["params"][-1]
        if _last.get("name") == "signing_key":
            _last["kind"] = "keyword"

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

    # REST SIDECAR unfold + base-verb projection (item B adoption). The
    # generated REST resource methods take an idiomatic options-struct param
    # (``const CreateParams& p``) that libclang reflects as ONE opaque param,
    # and INHERIT their base CRUD verbs (list/get/create/update/delete_) from
    # base_resource.hpp bases — which libclang does NOT surface on the subclass.
    # The generator emits the canonical per-method param records into
    # ``rest_signatures.json`` (keyed ``Class::method``), matching the Python
    # oracle's per-class *declared*-method recording (griffe records only the
    # subclass's own overrides, not inherited base methods). Projecting every
    # sidecar record onto its generated class handles BOTH the typed-param
    # unfold AND the base-verb presence in one pass. Idiom via emit+sidecar,
    # never omission (RULES §2, L10).
    _apply_rest_sidecar(out_modules)

    # SWML gen-payload getter projection (item D). The generated read-side
    # payload structs under include/signalwire/core/swml_verbs_generated/*.hpp
    # (AIParams, AIObject, UserSWAIGFunction, Webhook, …) are METHOD-LESS PODs:
    # one ``std::optional<T>`` data member per snake wire key. The Python oracle
    # records the SAME wire keys as zero-arg PROPERTY-GETTER methods on these
    # classes. libclang only surfaces CXX_METHOD/CONSTRUCTOR cursors, so the
    # struct's fields never reach ``methods_out`` and the whole class is dropped
    # (``if not methods_out: continue``) — every oracle getter then reads as
    # missing-port DRIFT. Project each public data-member FIELD as a zero-arg
    # getter so the field-vs-getter SHAPE reconciles (the surface side already
    # reconciles these as method-less on both sides — SURFACE-DIFF green — so
    # this is the analogous signature-side projection). Idiom via the enumerator,
    # NEVER omission (RULES §2). Reserved-word field renames (``default_`` /
    # ``enum_``) carry a ``// wire key: <name>`` comment the parser honours, so
    # the getter lands on the oracle's wire-keyed name.
    _project_gen_payload_getters(out_modules)

    # RELAY concrete call-action control-method projection. The oracle records
    # the control methods (stop/pause/resume/volume/start_input_timers) directly
    # on each CONCRETE action (PlayAction/RecordAction/CollectAction/…). The C++
    # port flattens them onto the unified ``signalwire.relay.action.Action`` and
    # each concrete subclass inherits them (macro
    # ``SIGNALWIRE_RELAY_ACTION_SUBCLASS``); libclang surfaces the macro-generated
    # subclasses but not the inherited methods. Project each concrete action's
    # oracle-required control methods, reusing the unified Action's real
    # signatures, so the concrete-action control surface MATCHES the reference
    # (idiom via the enumerator; the void-vs-dict return is the documented
    # cpp_unified_action idiom, tracked in PORT_SIGNATURE_OMISSIONS).
    _project_relay_action_subclasses(out_modules)

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


def _load_rest_sidecar() -> dict:
    """Load the generator's rest_signatures.json (Class::method -> [records])."""
    sc = (PORT_ROOT / "include" / "signalwire" / "rest" / "namespaces"
          / "generated" / "rest_signatures.json")
    if not sc.is_file():
        return {}
    return json.loads(sc.read_text()).get("methods", {})


def _generated_class_modules() -> dict[str, str]:
    """Map each generated resource/container CLASS -> its python module, from
    generated_surface_map.json (the same source enumerate_surface projects)."""
    smap = (PORT_ROOT / "include" / "signalwire" / "rest" / "namespaces"
            / "generated" / "generated_surface_map.json")
    if not smap.is_file():
        return {}
    return json.loads(smap.read_text())


def _generated_container_members() -> dict[str, list[str]]:
    """Parse the generated namespace-container headers for their public resource
    member fields (FabricNamespace { AiAgents ai_agents; ... }) so the client
    tree's accessor surface can be projected onto the oracle shape."""
    gen_dir = (PORT_ROOT / "include" / "signalwire" / "rest" / "namespaces"
               / "generated")
    out: dict[str, list[str]] = {}
    import re as _re
    for hdr in gen_dir.glob("*Namespace.hpp"):
        src = hdr.read_text()
        m = _re.search(r"class (\w+Namespace)\s*\{(.*?)\n\};", src, _re.S)
        if not m:
            continue
        cls, body = m.group(1), m.group(2)
        # public data members: ``<TypeName> <member>;`` at 2-space indent.
        members = _re.findall(r"^\s{2}([A-Z]\w+)\s+([a-z_]\w*);", body, _re.M)
        out[cls] = [mem for _t, mem in members]
    return out


def _apply_rest_sidecar(out_modules: dict) -> None:
    """Project the generator's per-method param records onto each generated REST
    resource class, unfolding the reflected options-struct param into the
    oracle's exploded keyword params AND materialising inherited base CRUD verbs
    the C++ subclass doesn't declare (list/get/create/update/delete_).

    Each sidecar record is ALREADY in the oracle's param shape
    (``{name, kind, type, required}``); the method's params become
    ``[{self}] + records`` and its return is ``any`` (the methods all return
    ``json``, which the diff treats as compatible with the oracle's typed
    ``*Response`` classes). The C++ sidecar key uses the native method spelling
    (``listAddresses``/``delete_``); canonicalise it exactly as ``collect`` does
    (camel_to_snake + _METHOD_RENAMES) so it lands on the oracle name.
    """
    sidecar = _load_rest_sidecar()
    if not sidecar:
        return
    class_mod = _generated_class_modules()

    # Group sidecar entries by class.
    by_class: dict[str, dict[str, list]] = {}
    for key, records in sidecar.items():
        if "::" not in key:
            continue
        cls, native = key.split("::", 1)
        canon = _METHOD_RENAMES.get(camel_to_snake(native), camel_to_snake(native))
        by_class.setdefault(cls, {})[canon] = records

    for cls, methods in by_class.items():
        mod = class_mod.get(cls)
        if mod is None:
            # A generated resource with a sidecar entry MUST be in the surface
            # map; a miss means the map is stale — fail loud rather than drift.
            raise SystemExit(
                f"enumerate_signatures: sidecar class {cls!r} not in "
                f"generated_surface_map.json (regenerate the REST layer)")
        out_modules.setdefault(mod, {"classes": {}})
        cls_entry = out_modules[mod]["classes"].setdefault(cls, {"methods": {}})
        for canon, records in methods.items():
            cls_entry["methods"][canon] = {
                "params": [{"name": "self", "kind": "self"}] + [dict(r) for r in records],
                "returns": "any",
            }
        # Ensure a constructor is present (POD resource: implicit default ctor).
        cls_entry["methods"].setdefault("__init__", {
            "params": [{"name": "self", "kind": "self"}],
            "returns": "void",
        })

    # Client-tree container accessors: the Python oracle records each namespace
    # container's resource members (FabricNamespace.ai_agents, ...) as zero-arg
    # accessor methods; the C++ containers expose them as public MEMBER FIELDS,
    # which libclang does not surface. Project each container's fields as
    # zero-arg methods (returns ``any`` — the diff treats it as compatible with
    # the oracle's ``class:<Resource>`` return).
    for cls, members in _generated_container_members().items():
        mod = class_mod.get(cls)
        if mod is None:
            raise SystemExit(
                f"enumerate_signatures: container {cls!r} not in "
                f"generated_surface_map.json (regenerate the REST layer)")
        out_modules.setdefault(mod, {"classes": {}})
        cls_entry = out_modules[mod]["classes"].setdefault(cls, {"methods": {}})
        for member in members:
            cls_entry["methods"].setdefault(member, {
                "params": [{"name": "self", "kind": "self"}],
                "returns": "any",
            })
        cls_entry["methods"].setdefault("__init__", {
            "params": [{"name": "self", "kind": "self"},
                       {"name": "http", "type": "any", "required": True}],
            "returns": "void",
        })


# A public data-member declaration inside one of the generated payload structs,
# with the optional trailing ``// wire key: <name>`` comment the generator emits
# for reserved-word-avoidance renames (``default_``/``enum_`` → ``default``/
# ``enum``). Group 1 = the C++ field identifier, group 2 = the wire-key comment
# (if present). Method declarations (which contain ``(``) are excluded before
# this regex is applied.
_GEN_FIELD_RE = re.compile(
    r"^\s+[A-Za-z_][\w:<>,\s]*?[>\w]\s+([A-Za-z_]\w*)\s*(?:=\s*[^;]+)?;\s*(//.*)?$"
)
_WIRE_KEY_RE = re.compile(r"wire key:\s*(\S+)")


def _gen_payload_ns_to_module() -> dict[str, str]:
    """The generated read-side payload namespaces this projection covers, from
    the surface enumerator's authoritative ``GENERATED_PAYLOAD_NS`` map
    (``signalwire::core::swml_verbs_generated`` → the Python module, etc.). The
    header directory for each is the namespace with ``::`` → ``/`` under
    ``include/``. Import (don't hardcode) so a new payload namespace registered
    for the surface side is automatically covered here too."""
    from enumerate_surface import GENERATED_PAYLOAD_NS  # type: ignore
    return dict(GENERATED_PAYLOAD_NS)


def _gen_payload_struct_fields(payload_dir: Path) -> dict[str, list[str]]:
    """Parse the generated payload headers under ``payload_dir`` for each
    struct's public data-member fields, mapped to their WIRE-KEY name (honouring
    the ``// wire key:`` comment on reserved-word-avoidance renames). Returns
    ``{StructName: [wire_field, …]}``.

    These structs are one-per-file, flat, and method-less (a POD DTO: one
    ``std::optional<T>`` member per wire key plus an open ``extras`` member),
    so a line-oriented parse is exact — verified to reproduce the oracle's
    getter set 1:1. Lines containing ``(`` are skipped so no accidental method /
    initializer is read as a field.
    """
    out: dict[str, list[str]] = {}
    if not payload_dir.is_dir():
        return out
    for hdr in sorted(payload_dir.glob("*.hpp")):
        src = hdr.read_text(encoding="utf-8")
        for sm in re.finditer(r"struct\s+(\w+)\s*\{(.*?)\n\};", src, re.S):
            cls, body = sm.group(1), sm.group(2)
            fields: list[str] = []
            for line in body.splitlines():
                if "(" in line:  # a method / initializer, not a data member
                    continue
                m = _GEN_FIELD_RE.match(line)
                if not m:
                    continue
                ident, comment = m.group(1), m.group(2) or ""
                wk = _WIRE_KEY_RE.search(comment)
                fields.append(wk.group(1) if wk else ident)
            if fields:
                out.setdefault(cls, []).extend(fields)
    return out


# Oracle-recorded control methods per concrete RELAY call-action (mirrors
# enumerate_surface.RELAY_ACTION_CONTROL_METHODS). Every concrete subclass
# inherits these from the unified C++ Action.
_RELAY_ACTION_CONTROL_METHODS: dict[str, list[str]] = {
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


def _project_relay_action_subclasses(out_modules: dict) -> None:
    """Project each concrete RELAY call-action's control-method signatures onto
    ``signalwire.relay.call.<Subclass>``, reusing the unified C++
    ``signalwire.relay.action.Action`` method signatures.

    The oracle records stop/pause/resume/volume/start_input_timers on each
    concrete action; the C++ port flattens them onto one Action that every
    subclass inherits (macro ``SIGNALWIRE_RELAY_ACTION_SUBCLASS`` — libclang sees
    the subclass but not the inherited methods). Project the oracle-required set
    per subclass, only where the unified Action genuinely defines that method
    (never inventing surface). The subclass ``__init__`` is already surfaced by
    libclang via the inherited ctor.
    """
    action_cls = (
        out_modules.get("signalwire.relay.action", {})
        .get("classes", {})
        .get("Action", {})
        .get("methods", {})
    )
    if not action_cls:
        return
    call_mod = out_modules.setdefault("signalwire.relay.call", {"classes": {}, "functions": {}})
    call_classes = call_mod.setdefault("classes", {})
    for sub, methods in _RELAY_ACTION_CONTROL_METHODS.items():
        entry = call_classes.setdefault(sub, {"methods": {}})
        for m in methods:
            if m in action_cls:  # only if the C++ Action truly defines it
                entry["methods"][m] = action_cls[m]


def _project_gen_payload_getters(out_modules: dict) -> None:
    """Project the generated payload structs' data-member FIELDS as zero-arg
    property-getter methods so they match the Python oracle's getter shape,
    across ALL generated read-side payload namespaces (swml_verbs_generated,
    post_prompt_generated, swaig_request_generated, …).

    The C++ port implements each wire key as a ``std::optional<T>`` FIELD on a
    method-less POD struct; libclang surfaces no method for it, so without this
    projection every one of the oracle's getters reads as missing-port DRIFT
    even though the field IS implemented. This is field-vs-getter SHAPE idiom,
    reconciled via the enumerator (RULES §2) — the analogue of the surface
    enumerator force-registering these as method-less types (empty member list
    on both sides → SURFACE-DIFF green).

    Only project a field whose wire-key name the oracle records as a getter for
    that class. Port-only data members the reference does not expose as a
    property (e.g. the open ``extras`` member, and the wire keys Python's
    payload class simply doesn't surface) are NOT projected — projecting them
    would invent method surface the reference lacks. Each getter is emitted with
    the oracle's zero-arg shape and an ``any`` return (``types_compatible``
    treats ``any`` as compatible with the oracle's typed ``union<…>`` /
    ``class:…`` getter returns), matching the container-accessor projection in
    ``_apply_rest_sidecar``.
    """
    ref = _load_python_signatures()
    if not ref:
        return
    for ns, module in _gen_payload_ns_to_module().items():
        ref_classes = ref.get("modules", {}).get(module, {}).get("classes", {})
        if not ref_classes:
            # No oracle getters recorded for this payload module — nothing to
            # project (its structs, if any, are method-less on both sides).
            continue
        payload_dir = PORT_ROOT / "include" / Path(*ns.split("::"))
        struct_fields = _gen_payload_struct_fields(payload_dir)
        if not struct_fields:
            continue
        mod_entry = out_modules.setdefault(module, {"classes": {}})
        mod_entry.setdefault("classes", {})
        for cls, fields in struct_fields.items():
            ref_cls = ref_classes.get(cls)
            if not ref_cls:
                continue
            oracle_getters = {
                m for m in ref_cls.get("methods", {}) if m != "__init__"
            }
            present = [f for f in fields if f in oracle_getters]
            if not present:
                continue
            cls_entry = mod_entry["classes"].setdefault(cls, {"methods": {}})
            for field in present:
                cls_entry["methods"].setdefault(field, {
                    "params": [{"name": "self", "kind": "self"}],
                    "returns": "any",
                })
            # Method-less POD: implicit default constructor is available.
            cls_entry["methods"].setdefault("__init__", {
                "params": [{"name": "self", "kind": "self"}],
                "returns": "void",
            })


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


def _translate_with_canonical_fallback(spelling: str,
                                        canonical_spelling: str,
                                        aliases: dict,
                                        ctx: str) -> str:
    """Translate a C++ type spelling, with awareness of typedef expansion.

    libclang reports the typedef name in ``arg.type.spelling``
    (``ParamsOrBody``, ``InboundCallHandler``, etc.). Some typedefs have
    explicit rules (CALLBACK_TYPEDEFS_AS_CALLABLE, CLASS_RENAME_MAP,
    aliases.cpp) and translate cleanly via the spelling. Others
    (``using ParamsOrBody = std::variant<...>``) don't, and we'd happily
    invent ``class:signalwire.params_or_body.ParamsOrBody`` from the
    bare-name heuristic — landing nowhere a Python signature could
    match.

    Strategy:
      1. Try the spelling first. Preserves project-named types where
         explicit rules exist.
      2. If the spelling translation either fails OR resolves via the
         "heuristic class ref" branch (last-resort PascalCase fallback)
         to a signalwire.* path that doesn't appear in CLASS_MODULE_MAP
         or CLASS_RENAME_MAP, fall back to the canonical (typedef-
         expanded) spelling. This catches port-internal typedefs over
         standard-library types without needing a per-typedef rule.
    """
    try:
        primary = translate_cpp_type(spelling, aliases, ctx)
    except TypeTranslationError:
        if canonical_spelling and canonical_spelling != spelling:
            return translate_cpp_type(canonical_spelling, aliases, ctx)
        raise

    # Detect the heuristic-fallback case: the translator emitted
    # ``class:<module>.<TypedefName>`` invented from the typedef's
    # bare name, but the typedef actually wraps a stdlib type the
    # canonical spelling can decompose.
    if canonical_spelling and canonical_spelling != spelling and \
            primary.startswith("class:") and "." in primary:
        # Look up the typedef name in CLASS_MODULE_MAP / CLASS_RENAME_MAP
        # to see if there's an intentional class-rename target. If so,
        # keep the primary translation; otherwise prefer canonical.
        tail = primary.split(":", 1)[1]
        cls_name = tail.rsplit(".", 1)[-1]
        if cls_name not in CLASS_MODULE_MAP and \
                not any(v[1] == cls_name for v in CLASS_RENAME_MAP.values()) and \
                cls_name not in CALLBACK_TYPEDEFS_AS_CALLABLE:
            try:
                return translate_cpp_type(canonical_spelling, aliases, ctx)
            except TypeTranslationError:
                # Fall through and return whatever the spelling produced
                pass
    return primary


def build_signature(method: dict, aliases: dict, context: str) -> dict:
    params_out: list = []
    is_static = method.get("is_static", False)
    is_ctor = method.get("is_constructor", False)
    if not is_static:
        params_out.append({"name": "self", "kind": "self"})
    for p in method.get("parameters", []):
        ctx = f"{context}[{p.get('name')}]"
        canon_type = _translate_with_canonical_fallback(
            p.get("type", ""), p.get("canonical_type", ""), aliases, ctx,
        )
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
    return_canon = "void" if is_ctor else _translate_with_canonical_fallback(
        method.get("return_type", "void"),
        method.get("canonical_return_type", ""),
        aliases,
        context + "[->]",
    )
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
    # libclang ships without the host stdlib headers; pull them in so
    # <cstddef> et al. resolve and nlohmann::json doesn't degrade to ``int``
    # (which silently corrupts every type that touches the missing headers).
    # Platform-specific, so the audit can be run + validated locally on macOS
    # as well as in Linux CI:
    #   * macOS: point libclang at the active SDK via -isysroot (same
    #     mechanism the compiler uses), discovered with `xcrun --show-sdk-path`.
    #     The SDK carries the libc++ headers under usr/include/c++/v1.
    #   * Linux: -I the system gcc include dir if present.
    if sys.platform == "darwin":
        parse_args += _macos_clang_args(_LIBCLANG)
    else:
        for gcc_inc in (
            "/usr/lib/gcc/x86_64-linux-gnu/10/include",
            "/usr/lib/gcc/x86_64-linux-gnu/12/include",
            "/usr/lib/gcc/x86_64-linux-gnu/13/include",
        ):
            if Path(gcc_inc).is_dir():
                parse_args.append(f"-I{gcc_inc}")
                break
    # Parse options. PARSE_SKIP_FUNCTION_BODIES: the enumerator only reads
    # DECLARATIONS (cursor kinds / is_definition() / parameter types + tokens),
    # never a method's body, so telling libclang to skip bodies removes the bulk
    # of the per-TU parse cost (3-10x) with zero effect on emitted output. We do
    # NOT use PARSE_DETAILED_PROCESSING_RECORD: it only builds the preprocessing
    # (macro/include) record, which nothing here consults — default-value
    # detection reads AST tokens via arg.get_tokens(), independent of that record
    # — so dropping it is a further speedup that leaves the output unchanged.
    _parse_opts = TranslationUnit.PARSE_SKIP_FUNCTION_BODIES

    # Single umbrella TU. The per-header approach re-parsed the whole macOS SDK +
    # libc++ once PER header (~1000+ headers) — the SIGNATURES gate's ~27-minute
    # cost. Instead, synthesize one in-memory TU that ``#include``s every header
    # and parse it ONCE, so the SDK/libc++ headers parse a single time. Each
    # header carries ``#pragma once`` so multiple include paths reaching the same
    # header are collapsed. walk_translation_unit's file_filter still restricts
    # the emitted cursors to declarations physically defined under include/, so
    # the output is identical to the per-header union (collect() already merges a
    # class that surfaced from more than one TU). We drive the umbrella off an
    # in-memory unsaved-file so nothing is written to disk. If the single-TU
    # parse fails outright or yields nothing (e.g. an unexpected redefinition),
    # fall back to the per-header loop so the gate never silently under-reports.
    umbrella_lines = [f'#include "{h.relative_to(args.include)}"' for h in headers]
    umbrella_src = "\n".join(umbrella_lines) + "\n"
    umbrella_name = str(args.include / "__sw_enum_umbrella__.cpp")
    single_tu_ok = False
    try:
        tu = index.parse(
            umbrella_name,
            args=parse_args,
            unsaved_files=[(umbrella_name, umbrella_src)],
            options=_parse_opts,
        )
        cls_entries, fn_entries = walk_translation_unit(tu, args.include)
        if cls_entries:
            raw_entries.extend(cls_entries)
            raw_free_functions.extend(fn_entries)
            single_tu_ok = True
    except Exception as e:
        print(f"enumerate_signatures: single-TU parse failed ({e}); "
              f"falling back to per-header", file=sys.stderr)

    if not single_tu_ok:
        raw_entries.clear()
        raw_free_functions.clear()
        for header in headers:
            try:
                tu = index.parse(str(header), args=parse_args, options=_parse_opts)
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
