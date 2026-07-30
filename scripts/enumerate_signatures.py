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
    for base in ("/opt/homebrew/opt", "/usr/local/opt"):
        # Prefer an explicitly versioned keg (llvm@18, llvm@19, …) then plain llvm.
        prefixes += sorted((str(q) for q in Path(base).glob("llvm@*")), reverse=True)
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
    args: list[str] = []
    try:
        sdk = subprocess.run(
            ["xcrun", "--show-sdk-path"], capture_output=True, text=True, check=True
        ).stdout.strip()
        if sdk and Path(sdk).is_dir():
            args += ["-isysroot", sdk]
    except (OSError, subprocess.CalledProcessError) as e:
        print(
            f"enumerate_signatures: xcrun --show-sdk-path failed ({e})", file=sys.stderr
        )

    # Find a libc++ + builtin (resource) header pair. Prefer the prefix that
    # provided libclang.dylib (a self-consistent toolchain); but the pip
    # `libclang` package's dylib lives in clang/native/ with NO headers — in
    # that case fall back to any Homebrew LLVM keg that has both, so the audit
    # doesn't silently degrade types to `int`.
    def _libcxx_and_builtins(prefix: Path):
        libcxx = prefix / "include" / "c++" / "v1"
        builtins = sorted(
            (str(q) for q in (prefix / "lib" / "clang").glob("*/include")), reverse=True
        )
        return (str(libcxx), builtins[0]) if libcxx.is_dir() and builtins else None

    found = None
    if libclang_path:
        found = _libcxx_and_builtins(
            Path(libclang_path).parent.parent
        )  # <prefix>/lib/libclang.dylib
    if not found:
        for prefix in _macos_llvm_prefixes():
            found = _libcxx_and_builtins(Path(prefix))
            if found:
                break
    if found:
        libcxx, builtins = found
        args += ["-nostdinc++", "-isystem", libcxx, "-isystem", builtins]
    else:
        print(
            "enumerate_signatures: no matched libc++/builtin headers found on "
            "macOS (install a Homebrew `llvm` keg); C++ types may degrade to int",
            file=sys.stderr,
        )
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
        str(
            Path.home() / ".local/lib/python3.12/site-packages/clang/native/libclang.so"
        ),
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


def _resolve_psdk() -> Path:
    # Honour PORTING_SDK_DIR, then PORTING_SDK (the var run-ci / the surface suite
    # export), then the adjacent ``<repo>/../porting-sdk``. The PORTING_SDK fallback
    # is what makes a WORKTREE run resolve the real checkout — a worktree's parent
    # has no porting-sdk sibling, so adjacency-only silently staled the enumerate
    # (it read the last committed port_signatures.json) and the SIGNATURES/GEN gates
    # then diffed stale output. Real CI uses adjacency, so this is purely a
    # local-worktree robustness fix; it never changes the CI-resolved path.
    for var in ("PORTING_SDK_DIR", "PORTING_SDK"):
        val = os.environ.get(var)
        if val and (Path(val) / "type_aliases.yaml").is_file():
            return Path(val).resolve()
    return (PORT_ROOT.parent / "porting-sdk").resolve()


PSDK = _resolve_psdk()

sys.path.insert(0, str(HERE))
from enumerate_surface import (  # type: ignore
    CALLBACK_TYPEDEFS_AS_CALLABLE,
    CLASS_MODULE_MAP,
    CLASS_RENAME_MAP,
    FREE_FUNCTION_RENAMES,
    MIXIN_PROJECTIONS,
    _METHOD_RENAMES,
    camel_to_snake,
    module_for_class,
    native_ns_to_module,
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
                new_t = new_t[len(prefix) :].strip()
        for suffix in ("&&", "&", "*"):
            if new_t.endswith(suffix):
                new_t = new_t[: -len(suffix)].strip()
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
            return canon_args[0] if canon_args else "any"
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
                        canon_a = [
                            translate_cpp_type(a, aliases, context)
                            for a in split_top_commas(args_part)
                        ]
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
    for (_ns, cls_name), (mod, py_cls) in CLASS_RENAME_MAP.items():
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
    ns_candidates.extend("::".join(parts[:i]) for i in range(len(parts) - 1, 0, -1))
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
    return (
        f"class:signalwire.{native_ns_to_module(ns_path)}.{name}"
        if ns_path
        else f"class:{name}"
    )


# ---------------------------------------------------------------------------
# Walking the AST
# ---------------------------------------------------------------------------


def walk_translation_unit(
    tu: TranslationUnit,
    file_filter: Path,
) -> tuple[list[dict], list[dict], dict[str, list[dict]]]:
    """Walk a clang TU and emit (class entries, free-function entries).

    Class entries carry a ``fields`` list (public data members) alongside
    ``methods``; ``options_structs`` (``cpp::Ns::Struct -> fields``) additionally
    captures fields-only PODs, which the signature inventory does not emit as
    classes but the construction contract unfolds as parameter sets."""
    entries: list[dict] = []
    free_functions: list[dict] = []
    options_structs: dict[str, list[dict]] = {}

    def visit(cursor, ns_path: list[str]):
        if cursor.kind in (CursorKind.NAMESPACE,):
            new_ns = [*ns_path, cursor.spelling]
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
            params = [_param_record(arg) for arg in cursor.get_arguments()]
            free_functions.append(
                {
                    "namespace": "::".join(ns_path),
                    "name": fname,
                    "parameters": params,
                    "return_type": cursor.result_type.spelling,
                    "canonical_return_type": cursor.result_type.get_canonical().spelling,
                }
            )
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
            # Public data members. C++ aggregate-initializes a config/payload
            # struct BY FIELD NAME, so these are construction parameters (the
            # §10 contract), not methods — collected from the AST rather than a
            # text scan so inline method bodies' locals can never leak in.
            fields = []
            for child in cursor.get_children():
                if child.kind != CursorKind.FIELD_DECL:
                    continue
                if child.access_specifier.name != "PUBLIC":
                    continue
                fname = child.spelling
                if not fname or fname.startswith("_") or fname.endswith("_"):
                    continue
                fields.append(
                    {
                        "name": fname,
                        "type": child.type.spelling,
                        "canonical_type": child.type.get_canonical().spelling,
                    }
                )
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
                    # A COPY/MOVE constructor is C++ object-lifetime plumbing,
                    # never a construction parameter set — ``AgentBase(const
                    # AgentBase&)`` configures nothing. It is also a 1-arg
                    # overload, so under the default fewest-param dedup it
                    # BEAT the real 4-arg ``AgentBase(name, route, host, port)``
                    # and erased every one of its params from the inventory.
                    if _is_copy_or_move_ctor(child):
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
            # ``methods`` still gates the SIGNATURE inventory (unchanged): a
            # fields-only POD is not an inventory class. But a fields-only POD is
            # exactly what an OPTIONS STRUCT is (``RelayConfig``), and its fields
            # ARE a construction parameter set, so record it separately for the
            # construction contract to unfold — without adding it to ``entries``
            # and thereby inventing an inventory class.
            if fields:
                options_structs[f"{ns_str}::{class_name}"] = fields
            # A fields-only POD is admitted to the inventory ONLY when the
            # reference ORACLE records a class of that name in the module this
            # class maps to — i.e. the reference genuinely has this class and
            # spells its whole surface as attributes. That is the case for the
            # credential carriers (``BasicCredentials``/``BearerCredentials``,
            # two std::string fields and no methods at all): the reference
            # records them as dataclasses whose members ARE the fields, so
            # dropping them here made an implemented carrier read as
            # missing-port drift. The oracle gate is what keeps this from
            # inventing an inventory class out of an internal options struct
            # (``RelayConfig`` has no reference counterpart and stays out).
            if (
                not methods
                and fields
                and _oracle_records_class(ns_str, class_name, str(fn.name))
            ):
                methods = []
            elif not methods:
                return
            entries.append(
                {
                    "namespace": ns_str,
                    "name": class_name,
                    "methods": methods,
                    "fields": fields,
                }
            )
            return
        # Recurse into other top-level structures
        for child in cursor.get_children():
            visit(child, ns_path)

    visit(tu.cursor, [])
    return entries, free_functions, options_structs


def _is_copy_or_move_ctor(cursor) -> bool:
    """True for ``X(const X&)`` / ``X(X&&)``.

    Prefers libclang's own predicates where the binding exposes them (they
    handle defaulted/templated forms), falling back to a structural test: a
    single argument whose canonical, reference-stripped, const-stripped type is
    the enclosing class itself."""
    for pred in ("is_copy_constructor", "is_move_constructor"):
        fn = getattr(cursor, pred, None)
        if callable(fn):
            try:
                if fn():
                    return True
            except (AttributeError, TypeError, ValueError) as e:
                # The binding exposes the name but cannot evaluate it (older
                # libclang builds raise instead of returning False). That is
                # precisely what the structural fallback below exists for, so
                # carry on -- but SAY which predicate was unusable: a silent
                # pass here is indistinguishable from "the predicate said no",
                # and the two have very different consequences for the
                # ctor/dunder classification this function drives.
                print(
                    f"enumerate_signatures: libclang {pred}() unusable ({e}); "
                    "falling back to the structural copy/move test",
                    file=sys.stderr,
                )
    args = list(cursor.get_arguments())
    if len(args) != 1:
        return False
    t = args[0].type.get_canonical().spelling
    for prefix in ("const ", "volatile "):
        while t.startswith(prefix):
            t = t[len(prefix) :]
    t = t.rstrip("&").strip()
    for prefix in ("const ", "volatile "):
        while t.startswith(prefix):
            t = t[len(prefix) :]
    return t.rsplit("::", 1)[-1] == cursor.semantic_parent.spelling


def _param_record(arg) -> dict:
    """Raw parameter record for one PARM_DECL cursor.

    ``canonical_type`` carries the typedef-EXPANDED spelling alongside the
    typedef-aware one so the translator can fall back to it when a port-internal
    typedef (e.g. ``ParamsOrBody`` over ``std::variant<...>``) is opaque to its
    bare-name lookup. See ``_translate_with_canonical_fallback``.

    ``default_value`` is the parsed default-argument literal, or ``_NO_DEFAULT``
    when the parameter has no default OR its default is a non-literal expression;
    ``has_default`` distinguishes those two cases.
    """
    has_default, default_value = _extract_default(arg)
    return {
        "name": arg.spelling,
        "type": arg.type.spelling,
        "canonical_type": arg.type.get_canonical().spelling,
        "has_default": has_default,
        "default_value": default_value,
    }


def extract_method(cursor, is_ctor: bool) -> dict:
    params = [_param_record(arg) for arg in cursor.get_arguments()]
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


# Sentinel distinguishing "this parameter has NO default" from "it has a default
# whose value we could not reduce to a JSON literal". ``None`` cannot serve for
# either, because ``None`` is also the value we emit for a genuine ``nullptr`` /
# ``std::nullopt`` default.
_NO_DEFAULT = object()


def _default_tokens(arg) -> list[str] | None:
    """Token spellings of a parameter's default-argument EXPRESSION, or None.

    libclang's Python binding has no ``clang_getParmDeclDefaultArgument``, but the
    PARM_DECL cursor's own token extent covers the whole ``<type> <name> = <expr>``
    declaration, so the default expression is recoverable as the tokens after the
    parameter's top-level ``=``.

    "Top-level" is load-bearing: the ``=`` must be located at zero bracket depth so
    a template argument list (``std::map<std::string, int> m = {}``) or a nested
    ``<...>`` cannot be mistaken for the assignment.

    Depth is counted PER CHARACTER, not per token, because libclang emits a nested
    template close as the SINGLE token ``>>`` (and ``>>>`` for triple nesting) —
    the C++ right-shift spelling. Decrementing once per token left the depth
    permanently positive for every ``std::optional<std::vector<std::string>>``
    parameter, so its ``=`` was never seen at top level and a real default was
    silently reported as NO default (flipping ``required`` to true on 32 params).
    Likewise ``->`` / ``<=`` / ``>=`` must not be counted as brackets at all.
    """
    try:
        tokens = [t.spelling for t in arg.get_tokens()]
    except Exception:
        return None
    # Multi-character operator tokens that CONTAIN an angle bracket but are not
    # template punctuation. Checked before the per-character bracket count.
    _NON_BRACKET_OPS = {"->", "->*", "<=", ">=", "<=>", "==", "!=", "<<", "&&"}
    depth = 0
    for i, tok in enumerate(tokens):
        if tok == "=" and depth == 0:
            rest = tokens[i + 1 :]
            return rest or None
        if tok in _NON_BRACKET_OPS:
            continue
        for ch in tok:
            if ch in "<([{":
                depth += 1
            elif ch in ">)]}":
                depth -= 1
    return None


# C++ default expressions that mean "absent" and translate to a JSON null. These
# are REAL defaults — a parameter carrying one is ``required: false`` with an
# explicit null, which is NOT the same as a parameter with no default at all.
_NULLISH_DEFAULTS = {
    ("nullptr",),
    ("NULL",),
    ("std", "::", "nullopt"),
    ("nullopt",),
}

# Empty brace-init ``= {}`` — a real default meaning "value-initialized". Its JSON
# form depends on the parameter's type, resolved by the caller.
_EMPTY_BRACE = ("{", "}")

_INT_RE = re.compile(r"^[+-]?(?:0[xX][0-9a-fA-F]+|0[bB][01]+|\d+)[uUlL]*$")
_FLOAT_RE = re.compile(r"^[+-]?(?:\d+\.\d*|\.\d+|\d+)(?:[eE][+-]?\d+)?[fFlL]?$")


def _parse_cpp_literal(tokens: list[str]):
    """Reduce a C++ default-argument token list to a JSON-comparable value.

    Returns ``_NO_DEFAULT`` when the expression is NOT a static literal — an enum
    value, a constructor call, an arithmetic expression, a named constant. Those
    are recorded as ``default: null`` by the caller rather than guessed at: the
    reference records concrete values, and inventing one here would manufacture a
    confident wrong answer, which is worse than a documented blind spot.
    """
    if not tokens:
        return _NO_DEFAULT

    if tuple(tokens) in _NULLISH_DEFAULTS:
        return None

    # String literals: one or more adjacent literals, concatenated as C++ does.
    # Covers ``""`` (empty string) and ``"a" "b"``. Only plain/UTF-8 literals with
    # no escape sequences beyond the common ones are decoded; anything exotic
    # falls through to non-literal.
    if all(t.startswith('"') and t.endswith('"') and len(t) >= 2 for t in tokens):
        out = []
        for t in tokens:
            body = t[1:-1]
            try:
                out.append(json.loads('"' + body + '"'))
            except ValueError:
                return _NO_DEFAULT
        return "".join(out)

    if len(tokens) == 1:
        tok = tokens[0]
        if tok == "true":
            return True
        if tok == "false":
            return False
        if _INT_RE.match(tok):
            digits = tok.rstrip("uUlL")
            try:
                return int(digits, 0)
            except ValueError:
                return _NO_DEFAULT
        if _FLOAT_RE.match(tok) and (
            "." in tok or "e" in tok.lower() or tok[-1] in "fF"
        ):
            try:
                return float(tok.rstrip("fFlL"))
            except ValueError:
                return _NO_DEFAULT
        # A bare char literal, an identifier (named constant / enum), etc.
        return _NO_DEFAULT

    # Unary sign applied to a numeric literal: ``= -1``, ``= +2.5``.
    if len(tokens) == 2 and tokens[0] in ("-", "+"):
        inner = _parse_cpp_literal([tokens[1]])
        if isinstance(inner, (int, float)) and not isinstance(inner, bool):
            return -inner if tokens[0] == "-" else inner
        return _NO_DEFAULT

    # Everything else (``Color::Red``, ``std::string("x")``, ``60 * 60``,
    # ``Opt{}``, ``SomeConstant``) is a non-literal expression.
    return _NO_DEFAULT


# Canonical types whose ``= {}`` value-initialization has a well-defined JSON form.
# A ``{}`` default on any other type (an SDK class, a struct) is a constructed
# object, not a literal, and is left non-literal.
def _empty_brace_default(canon_type: str):
    t = (canon_type or "").strip()
    if t.startswith("list<"):
        return []
    if t.startswith("dict<"):
        return {}
    if t == "string":
        return ""
    if t == "bool":
        return False
    if t in ("int", "float"):
        return 0 if t == "int" else 0.0
    if t.startswith("optional<") or t == "any":
        return None
    return _NO_DEFAULT


def _extract_default(arg):
    """(has_default, value) for one PARM_DECL cursor.

    ``value`` is ``_NO_DEFAULT`` when a default exists but is a non-literal
    expression we deliberately refuse to evaluate.
    """
    tokens = _default_tokens(arg)
    if tokens is None:
        return False, _NO_DEFAULT
    if tuple(tokens) == _EMPTY_BRACE:
        return True, _EMPTY_BRACE
    return True, _parse_cpp_literal(tokens)


def _has_default_value(arg) -> bool:
    """True when the parameter declares a default argument (value aside)."""
    return _default_tokens(arg) is not None


# ---------------------------------------------------------------------------
# The null <-> zero-value sentinel fold (the C++ "no nullable scalars" idiom)
# ---------------------------------------------------------------------------
#
# THE VOCABULARY RULE
# ===================
# Python expresses "the caller did not supply this" as ``x: T | None = None`` and
# then guards the wire with ``if x is not None:``. C++ has no nullable scalar and
# no keyword arguments, so the SAME contract is spelled as a ZERO-VALUE SENTINEL
# default plus an absence guard in the body:
#
#     python   def user_event(self, event: str | None = None)     if event is not None: p["event"] = event
#     cpp      Action user_event(const std::string& event = "")   if (!event.empty()) { p["event"] = event; }
#
# Those two are behaviourally identical: a caller who omits the argument produces
# the identical wire frame in both languages. Recording the C++ side as
# ``default: ""`` while the reference records ``default: null`` manufactures drift
# out of two spellings of "absent".
#
# So the enumerator FOLDS the sentinel to ``null`` — at the emitter, in the
# canonical vocabulary, so the comparison keeps running (an allow-list would stop
# comparing and blind the gate to a real value change).
#
# THE FOLD IS EVIDENCE-GATED. It is NOT "empty string always means null". A port
# that defaults ``prompt=""`` and then SENDS ``prompt: ""`` ships a different
# request body than a reference that omits the key, and that is a REAL divergence
# the gate must keep reporting. The fold therefore requires BOTH:
#
#   1. the default is the parameter type's ZERO VALUE / documented sentinel
#      (table below), AND
#   2. a GUARD in the method's definition body that tests that sentinel and
#      suppresses the value's use.
#
# No guard -> no fold. The sentinel is then a value the port genuinely ships, and
# the ``default-mismatch`` finding stands as a real one.
#
#   type              sentinel      guard that proves absence
#   ---------------   -----------   -----------------------------------------
#   std::string       ""            !p.empty()  /  p.empty() ? ... : p  /  p != ""
#   vector/map/json   {}            !p.empty()  /  p.empty() ? ...
#   integral          0, -1         p > 0  /  p >= 0  /  p != 0  /  p != -1
#   floating          -1.0, 0.0     p >= 0.0  /  p > 0.0  /  p != -1.0
#   optional/pointer  nullopt/null  (already recorded as null; nothing to fold)
#
# ONE TRANSITIVE HOP — AND NOT FOR STRINGS. Several methods store the sentinel on
# a member and guard it at SERIALIZATION rather than at the entry point — e.g.
# ``Step::set_gather_info`` assigns ``GatherInfo(output_key, ...)`` and
# ``GatherInfo::to_json()`` then does ``if (!output_key_.empty())``. The guard is
# still the proof that the sentinel never reaches the wire, so a member-name guard
# (``<param>_`` or ``<param>``, the repo's member spelling) anywhere in the SAME
# source file counts. Exactly one hop; the scanner never chases further, so an
# unproven chain reports drift rather than folding on a guess.
#
# The transitive hop is DISALLOWED for the ``""`` sentinel, and this is the whole
# reason the string case needs its own rule. Measured against the oracle
# (2026-07-27): ``[]`` and ``{}`` NEVER appear as a reference default — 0 of
# 1,505 recorded defaults — so an empty container in the reference is always
# ``None`` and a guarded empty-container sentinel is unambiguously "absent".
# ``""``, by contrast, is a REAL reference default 111 times, and ``0``/``0.0``
# 35 times. An empty string is a value Python genuinely sends.
#
# What separates the two IS visible on the port side: the C++ code models the
# distinction in its STORAGE type, the same way the reference models it in its
# annotation. ``pom::Section`` declares ``std::optional<std::string> title``
# (reference: ``str | None = None``) next to ``std::string body`` (reference:
# ``str = ""``) — the port and the reference agree, independently. A parameter
# stored verbatim onto a non-optional member and only guarded later at
# serialization (``body``) is a real empty-string default; a parameter the method
# itself tests before use (``event``, ``status_url``) models absence.
#
# So: strings fold ONLY on a DIRECT guard in the method's own body. Without that
# split the fold turns the 5 POM ``body`` parameters into nulls, inventing a
# ``"" vs null`` mismatch in the other direction — which is the same class of
# error as not folding at all, just pointing the other way.
#
# WHAT THIS RULE DOES NOT PROVE, stated plainly so the next reader does not
# mistake it for stronger than it is:
#
#   * NUMERICS keep the transitive hop even though ``0`` is also a real reference
#     default (35 times). Every numeric fold this produces today is a ``timeout``
#     / ``volume`` / ``max_duration`` parameter with a ``> 0`` guard and a
#     reference ``None``, verified param-by-param — the hop is simply not
#     load-bearing for any of them. If a future ``0``-defaulted numeric ever
#     folds WRONG, tighten it to direct-guard-only the way strings already are.
#   * A TRANSITIVELY-guarded STRING that the reference really does declare
#     ``str | None`` (``Step::set_gather_info``'s three parameters) is NOT folded
#     and keeps reporting drift. That is the rule choosing a false NEGATIVE over
#     a false positive: the port stores those in plain ``std::string`` members,
#     which is indistinguishable from the ``body`` shape. Closing them means
#     changing the PORT to model absence (``std::optional<std::string>``, as
#     ``pom::Section::title`` already does), not loosening this rule.
#   * The hop is scoped to ONE source file. A parameter stored into a member
#     declared and guarded in a different translation unit (``AgentBase::
#     prompt_add_section``'s ``bullets``, guarded in ``pom.cpp``) does not fold.
#     Widening to whole-tree matching would let any ``.empty()`` anywhere satisfy
#     the guard, which is not evidence.

# Sentinel default VALUES that are foldable per type, keyed by the canonical
# (translated) type prefix. A value not in this table is a real default and is
# never folded, however guarded the parameter is.
_FOLDABLE_SENTINELS: dict[str, tuple] = {
    "string": ("",),
    "list": ([],),
    "dict": ({},),
    "int": (0, -1),
    "float": (0.0, -1.0),
}


def _sentinel_kind(canon_type: str) -> str | None:
    t = (canon_type or "").strip()
    if t == "string":
        return "string"
    if t.startswith("list<"):
        return "list"
    if t.startswith("dict<") or t == "any":
        # ``any`` is the translated form of nlohmann::json, whose ``= {}`` /
        # empty-object default is guarded with ``.empty()`` exactly like a map.
        return "dict"
    if t == "int":
        return "int"
    if t == "float":
        return "float"
    return None


def _is_foldable_sentinel(canon_type: str, value) -> bool:
    kind = _sentinel_kind(canon_type)
    if kind is None:
        return False
    if isinstance(value, bool):
        # ``bool`` has no "absent" spelling: false is a real, sendable value.
        return False
    for sentinel in _FOLDABLE_SENTINELS[kind]:
        if type(sentinel) is type(value) and sentinel == value:
            return True
        if (
            kind == "float"
            and isinstance(value, (int, float))
            and float(sentinel) == float(value)
        ):
            return True
    return False


# Guard shapes, per sentinel kind, rendered against a parameter NAME placeholder.
# Each is matched against the definition body with the name substituted in.
_GUARD_PATTERNS: dict[str, list[str]] = {
    # ``!p.empty()`` / ``p.empty() ?`` / ``!p.is_null()`` / ``p != ""``
    "string": [
        r"!\s*{n}\s*\.empty\s*\(\s*\)",
        r"\b{n}\s*\.empty\s*\(\s*\)\s*\?",
        r"\b{n}\s*!=\s*\"\"",
        r"\bif\s*\(\s*{n}\s*\.empty\s*\(\s*\)\s*\)",
    ],
    "list": [
        r"!\s*{n}\s*\.empty\s*\(\s*\)",
        r"\b{n}\s*\.empty\s*\(\s*\)\s*\?",
        r"\bif\s*\(\s*{n}\s*\.empty\s*\(\s*\)\s*\)",
    ],
    "dict": [
        r"!\s*{n}\s*\.empty\s*\(\s*\)",
        r"!\s*{n}\s*\.is_null\s*\(\s*\)",
        r"\b{n}\s*\.empty\s*\(\s*\)\s*\?",
        r"\b{n}\s*\.is_object\s*\(\s*\)\s*\?",
        r"\bif\s*\(\s*{n}\s*\.empty\s*\(\s*\)\s*\)",
    ],
    "int": [
        r"\b{n}\s*(?:>|>=|!=|==)\s*[-+]?\d",
        r"\bif\s*\(\s*{n}\s*\)",
    ],
    "float": [
        r"\b{n}\s*(?:>|>=|!=|==)\s*[-+]?[\d.]",
    ],
}


def _member_spellings(param_name: str) -> list[str]:
    """Names a parameter may have been stored under for the ONE transitive hop.

    The repo's convention is a trailing-underscore private member
    (``output_key`` -> ``output_key_``); a handful store under the bare name.
    """
    return [param_name + "_", param_name]


def _param_names_from_list(param_list: str) -> list[str]:
    """Parameter NAMES, in order, from a definition's parameter-list text.

    ``param_list`` is everything between the ``(`` and the body's ``{``, minus the
    leading ``(`` the caller already consumed. Split on top-level commas (so a
    ``std::map<K, V>`` or a ``std::variant<A, B>`` argument is one parameter, not
    two) and take the trailing identifier of each part — C++ puts the declarator
    name last, after any ``const``/``&``/template spelling.
    """
    body_end = param_list.rfind(")")
    inner = param_list[:body_end] if body_end >= 0 else param_list
    parts: list[str] = []
    depth, buf = 0, []
    for ch in inner:
        if ch in "<([{":
            depth += 1
        elif ch in ">)]}":
            depth -= 1
        if ch == "," and depth == 0:
            parts.append("".join(buf))
            buf = []
            continue
        buf.append(ch)
    parts.append("".join(buf))
    names: list[str] = []
    ident = re.compile(r"([A-Za-z_]\w*)\s*(?:=[^,]*)?$")
    for part in parts:
        # Drop a default-argument expression and any trailing array extent, then
        # take the last identifier.
        head = part.split("=", 1)[0].strip().rstrip("[]").strip()
        m = ident.search(head)
        names.append(m.group(1) if m else "")
    return names


_FORWARD_RE = re.compile(
    r"^\{\s*return\s+(?:[A-Za-z_]\w*::)*([A-Za-z_]\w*)\s*\((.*)\)\s*;\s*\}$", re.S
)


def _pure_forward_target(body: str) -> tuple[str, list[str]] | None:
    """``(callee_name, [argument spellings])`` when ``body`` is one ``return f(...);``.

    Only a body whose ENTIRE content is that single statement qualifies —
    comments are stripped first, but any additional statement disqualifies it.
    Arguments are split on top-level commas and kept verbatim, so the caller can
    match a parameter by NAME and recover its position in the callee.
    """
    stripped = re.sub(r"//[^\n]*", "", body)
    stripped = re.sub(r"/\*.*?\*/", "", stripped, flags=re.S)
    stripped = " ".join(stripped.split())
    m = _FORWARD_RE.match(stripped)
    if not m:
        return None
    args, depth, buf = [], 0, []
    for ch in m.group(2):
        if ch in "<([{":
            depth += 1
        elif ch in ">)]}":
            depth -= 1
        if ch == "," and depth == 0:
            args.append("".join(buf).strip())
            buf = []
            continue
        buf.append(ch)
    args.append("".join(buf).strip())
    return m.group(1), args


class GuardIndex:
    """Which parameters of which C++ methods carry an absence guard.

    Built by a text scan of the implementation tree (``src/**/*.cpp``) plus the
    headers' inline bodies. libclang is not used here on purpose: the enumerator
    parses headers with ``PARSE_SKIP_FUNCTION_BODIES`` (a 3-10x speedup on the
    SIGNATURES gate), and re-parsing all 67 translation units to read bodies
    would give back that entire saving to answer a question a brace-matched text
    scan answers exactly as well.

    Keyed by ``(ClassName, methodName)``; a class's method may be defined in more
    than one file (and a method may be overloaded), so every matching body is
    unioned — a guard in ANY definition of that name proves the port models the
    absence.
    """

    def __init__(self, roots: list[Path]):
        # (class, method) -> list[(body_text, file_text, [param names in order])]
        self._bodies: dict[tuple[str, str], list[tuple[str, str, list[str]]]] = {}
        # Free-function definitions, keyed by bare name — the forwarding-callee
        # lookup below. Same (body, file_text, names) shape as _bodies.
        self._free: dict[str, list[tuple[str, str, list[str]]]] = {}
        self._defpat = re.compile(r"\b([A-Za-z_]\w*)::([A-Za-z_]\w*)\s*\(", re.M)
        self._freepat = re.compile(
            r"^[A-Za-z_][\w:<>,\s*&]*?\b([A-Za-z_]\w*)\s*\(", re.M
        )
        for root in roots:
            if not root.is_dir():
                continue
            for path in sorted(root.rglob("*.cpp")) + sorted(root.rglob("*.hpp")):
                try:
                    text = path.read_text(encoding="utf-8", errors="replace")
                except OSError:
                    continue
                self._index_file(text)

    def _index_file(self, text: str) -> None:
        for m in self._defpat.finditer(text):
            cls, method = m.group(1), m.group(2)
            open_brace = text.find("{", m.end())
            if open_brace < 0:
                continue
            # A ';' between the parameter list and the next '{' means this was a
            # declaration (or a call), not a definition.
            if ";" in text[m.end() : open_brace]:
                continue
            depth, i = 0, open_brace
            n = len(text)
            while i < n:
                ch = text[i]
                if ch == "{":
                    depth += 1
                elif ch == "}":
                    depth -= 1
                    if depth == 0:
                        break
                i += 1
            body = text[open_brace : i + 1]
            names = _param_names_from_list(text[m.end() : open_brace])
            self._bodies.setdefault((cls, method), []).append((body, text, names))

        # Free functions, for the pure-forwarder hop only.
        for m in self._freepat.finditer(text):
            name = m.group(1)
            if name in ("if", "for", "while", "switch", "return", "catch", "sizeof"):
                continue
            open_brace = text.find("{", m.end())
            if open_brace < 0 or ";" in text[m.end() : open_brace]:
                continue
            if "::" in text[m.start() : m.end()]:
                continue  # already captured as a member definition
            depth, i = 0, open_brace
            n = len(text)
            while i < n:
                if text[i] == "{":
                    depth += 1
                elif text[i] == "}":
                    depth -= 1
                    if depth == 0:
                        break
                i += 1
            self._free.setdefault(name, []).append(
                (
                    text[open_brace : i + 1],
                    text,
                    _param_names_from_list(text[m.end() : open_brace]),
                )
            )

    def guards(self, cls: str, method: str, param: str, index: int, kind: str) -> bool:
        """True when SOME definition of ``cls::method`` guards this parameter.

        ``param`` is the HEADER's spelling and ``index`` its position. A C++
        definition is free to rename its parameters (``dial(const std::string&
        tag)`` in the header is ``dial(..., const std::string& tag_in, ...)`` in
        the .cpp, because the body shadows it with the resolved ``tag``), so the
        definition-side name is resolved BY POSITION and the header name is only
        a fallback. Matching on the header name alone silently found no guard for
        every renamed parameter — and a missing guard reads as "the port really
        sends this", i.e. a fold refused for a bookkeeping reason.

        Direct: the guard names the parameter inside the method body.
        One transitive hop: the parameter is stored (the body mentions it) and a
        guard on the corresponding MEMBER name appears elsewhere in the same
        source file — the store-then-guard-at-serialization shape. NOT available
        to the ``""`` sentinel: see the vocabulary note above (a stored-then-
        serialization-guarded string is a real empty-string default, which is
        exactly how both the reference and this port model ``pom::Section::body``).
        """
        entries = self._bodies.get((cls, method))
        if not entries:
            return False
        patterns = _GUARD_PATTERNS.get(kind, [])
        for body, file_text, names in entries:
            local = names[index] if 0 <= index < len(names) else None
            candidates = [n for n in (local, param) if n]
            for name in candidates:
                for pat in patterns:
                    if re.search(pat.format(n=re.escape(name)), body):
                        return True
                # A LOCAL ALIAS still counts as a direct guard. C++ cannot
                # reassign a ``const T&`` parameter, so the "resolve the sentinel
                # to the real value" idiom has to copy first:
                #     std::string id = call_id;
                #     if (id.empty()) { id = <generated>; }
                # That is the same absence check as ``if (!call_id.empty())``,
                # just one named local away, and it is still INSIDE this method —
                # unlike the member/serialization hop, which is what distinguishes
                # a modelled absence from a real empty-string default. Only a
                # local DECLARED FROM this parameter qualifies.
                for alias in re.findall(
                    r"\b(?:auto|[A-Za-z_][\w:<>,\s*&]*?)\s+([A-Za-z_]\w*)\s*=\s*"
                    + re.escape(name)
                    + r"\s*;",
                    body,
                ):
                    for pat in patterns:
                        if re.search(pat.format(n=re.escape(alias)), body):
                            return True
            # PURE FORWARDER. A method whose ENTIRE body is a single
            # ``return <fn>(...);`` delegates its contract wholesale; the guard
            # lives in the callee. ``AgentBase::handle_serverless_request`` is
            # exactly this — its one statement is
            # ``return utils::handle_serverless_request(*this, event, context, mode);``
            # and the free function does ``mode.empty() ? get_execution_mode() : mode``.
            # Restricted to a body with ONE statement so it can only ever mean
            # "this method IS the callee", never "somewhere downstream something
            # is guarded". The callee's parameter is located BY NAME in the
            # forwarding call's argument list, so an argument the forwarder
            # reorders or wraps does not silently match.
            fwd = _pure_forward_target(body)
            if fwd is not None:
                callee, args = fwd
                for name in candidates:
                    if name not in args:
                        continue
                    arg_index = args.index(name)
                    for cbody, _cfile, cnames in self._free.get(callee, []):
                        cname = (
                            cnames[arg_index] if 0 <= arg_index < len(cnames) else None
                        )
                        for pat in patterns:
                            if cname and re.search(
                                pat.format(n=re.escape(cname)), cbody
                            ):
                                return True
            if kind == "string":
                continue
            # One transitive hop: the body must actually USE the parameter (under
            # EITHER spelling), and the member it lands on must be guarded in this
            # file. The member is named after the CONCEPT, so every candidate
            # spelling is tried for the member lookup once the parameter is known
            # to be used — the definition's local name (``Section::add_subsection``
            # spells ``bullets`` as ``bs``) is not the member's name.
            if not any(
                re.search(r"\b" + re.escape(n) + r"\b", body) for n in candidates
            ):
                continue
            for name in candidates:
                for member in _member_spellings(name):
                    if member == name:
                        # The bare-name hop would re-match the body itself; only
                        # accept it OUTSIDE the body.
                        outside = file_text.replace(body, "", 1)
                    else:
                        outside = file_text
                    for pat in patterns:
                        if re.search(pat.format(n=re.escape(member)), outside):
                            return True
        return False

    def bodies(self, cls: str, method: str) -> list[tuple[str, list[str]]]:
        """``[(body_text, param_names)]`` for every definition of ``cls::method``.

        The raw material the options-carrier unfold needs: whether a method's
        ``const json&`` parameter is SPREAD onto the wire frame (a bag standing
        in for the reference's keyword params) or CONSUMED as one domain value.
        Only the body and the definition-side parameter names are exposed; the
        file text stays private to the guard scan.
        """
        return [
            (body, names)
            for body, _file_text, names in self._bodies.get((cls, method), [])
        ]


# ---------------------------------------------------------------------------
# Building canonical inventory
# ---------------------------------------------------------------------------


def _merge_overload_optionality(a: dict, b: dict) -> None:
    """Union ``required``/``default`` across two equal-arity overloads, in place.

    Positional match, because C++ overloads of one name share their parameter
    ORDER (that is what makes them overloads). Only parameters whose optionality
    DISAGREES are touched, and only in the permissive direction: if either side
    declares a default, the caller can omit the argument, so both sides become
    ``required: false`` carrying that default. Types and kinds are untouched —
    those still come from whichever overload dedup selects.

    Arity mismatch means the two are not the same call shape (a convenience
    wrapper, not a typed sibling); leave them alone.
    """
    pa, pb = a.get("params", []), b.get("params", [])
    if len(pa) != len(pb):
        return
    for x, y in zip(pa, pb, strict=False):
        if x.get("kind") == "self" or y.get("kind") == "self":
            continue
        x_opt = x.get("required") is False
        y_opt = y.get("required") is False
        if x_opt == y_opt:
            continue
        src, dst = (x, y) if x_opt else (y, x)
        dst["required"] = False
        dst["default"] = src.get("default")


def collect(
    raw_entries: list[dict],
    aliases: dict,
    raw_free_functions: list[dict] | None = None,
    raw_options_structs: dict[str, list[dict]] | None = None,
    guards: GuardIndex | None = None,
) -> tuple[dict, list]:
    out_modules: dict = {}
    failures: list = []
    raw_free_functions = raw_free_functions or []
    # {"module.Class": {field: canonical_type}} — public data members, keyed by
    # the CANONICAL python name (so the construction contract never has to
    # re-derive the C++ class -> python module mapping and cannot collide on a
    # bare C++ struct name shared across namespaces).
    struct_fields: dict[str, dict[str, str]] = {}
    raw_options_structs = raw_options_structs or {}
    # {"class:<module>.<Class>": {field: canonical_type}} — options structs
    # addressable by the canonical class-ref that appears as a ctor param's
    # emitted type, so ``RelayClient(config: class:…RelayConfig)`` can unfold to
    # RelayConfig's named field set.
    options_by_ref: dict[str, dict[str, str]] = {}
    for cpp_qual, fields in raw_options_structs.items():
        ref = _translate_sdk_class_ref(cpp_qual)
        if not ref.startswith("class:"):
            continue
        typed: dict[str, str] = {}
        for f in fields:
            fctx = f"construction.{ref}.{f['name']}"
            try:
                ftype = _translate_with_canonical_fallback(
                    f.get("type", ""), f.get("canonical_type", ""), aliases, fctx
                )
            except TypeTranslationError:
                ftype = "any"
            typed[f["name"]] = ftype or "any"
        if typed:
            options_by_ref.setdefault(ref, typed)

    by_class: dict = {}
    for entry in raw_entries:
        ns = entry["namespace"]
        name = entry["name"]
        key = (ns, name)
        if key in by_class:
            # Merge methods (e.g. when class is split across translation units)
            by_class[key]["methods"].extend(entry["methods"])
            by_class[key]["fields"].extend(entry.get("fields") or [])
        else:
            by_class[key] = {
                "namespace": ns,
                "name": name,
                "methods": list(entry["methods"]),
                "fields": list(entry.get("fields") or []),
            }

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
                method_canonical = _METHOD_RENAMES.get(
                    method_canonical, method_canonical
                )
            ctx = f"{mod}.{name}.{method_canonical}"
            try:
                sig = build_signature(
                    m,
                    aliases,
                    ctx,
                    guards=guards,
                    cpp_class=entry["name"],
                    cpp_method=(entry["name"] if native == "<init>" else native),
                )
            except TypeTranslationError as e:
                failures.append(str(e))
                continue
            if method_canonical in methods_out:
                existing = methods_out[method_canonical]
                # OPTIONALITY IS A PROPERTY OF THE METHOD NAME, NOT OF ONE
                # OVERLOAD. Only one overload survives dedup, but ``required``
                # asks a question about the CALLER: can they omit this argument?
                # If ANY overload of the name defaults the parameter, they can.
                #
                # C++ forces this apart where a port ships a flat ``std::string``
                # overload alongside a typed ``enum class`` one: the string form
                # defaults ``record_call(control_id="", stereo=false,
                # format="wav", direction="both")``, but the typed form CANNOT
                # repeat those defaults — two equal-arity overloads that are both
                # callable with fewer arguments are ambiguous, so the compiler
                # rejects it. The typed overload therefore declares them bare,
                # and dedup (which prefers the typed form for the closed-set
                # contract) was reporting ``required: true`` for four parameters
                # a caller can plainly omit.
                #
                # Union optionality across equal-arity overloads before choosing
                # a winner. Types/kinds still come from the chosen overload
                # alone; only ``required``/``default`` merge.
                _merge_overload_optionality(existing, sig)
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
                    if new_typed == old_typed and len(sig["params"]) >= len(
                        existing["params"]
                    ):
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

        if not methods_out and not (entry.get("fields") or []):
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
        # Public data members -> the construction contract's field source. Typed
        # through the same translator/vocabulary the methods use; a spelling the
        # vocabulary does not know falls back to ``any`` rather than dropping the
        # param, because a construction param's NAME is the load-bearing part of
        # the contract.
        for f in entry.get("fields") or []:
            fctx = f"construction.{mod}.{name}.{f['name']}"
            try:
                ftype = _translate_with_canonical_fallback(
                    f.get("type", ""), f.get("canonical_type", ""), aliases, fctx
                )
            except TypeTranslationError:
                ftype = "any"
            struct_fields.setdefault(f"{mod}.{name}", {}).setdefault(
                f["name"], ftype or "any"
            )

        out_modules.setdefault(mod, {"classes": {}})
        out_modules[mod]["classes"].setdefault(name, {"methods": {}})
        out_modules[mod]["classes"][name]["methods"].update(methods_out)

        # A class that is ITSELF an options struct (fields + a few methods, e.g.
        # RequestOptions) can also be the unfold target of another class's
        # ctor param, so register it under its canonical class-ref too.
        if struct_fields.get(f"{mod}.{name}"):
            options_by_ref.setdefault(
                f"class:{mod}.{name}", struct_fields[f"{mod}.{name}"]
            )

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
    ab_entry = (
        out_modules.get("signalwire.core.agent_base", {})
        .get("classes", {})
        .get("AgentBase")
    )
    svc_entry = (
        out_modules.get("signalwire.core.swml_service", {})
        .get("classes", {})
        .get("SWMLService")
    )
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
            # ``agent``: the reference constructs each synthetic helper with a
            # back-reference to the owning agent (``PromptManager(self)``,
            # stored as ``self.agent`` — a ctor param the oracle's class-B2 rule
            # records). C++ does not extract the helper as a separate OBJECT at
            # all: its methods are declared directly on AgentBase, which is
            # exactly what this projection encodes. When the helper and its
            # agent are the SAME object the back-reference is ``*this`` — as
            # available in C++ as in Python, simply already in hand. Emitted
            # only for the synthetic targets, and only when the projection
            # really produced the merged class.
            if retarget_returns:
                out_modules[target_mod]["classes"][target_cls]["methods"].setdefault(
                    "agent",
                    {"params": [{"name": "self", "kind": "self"}], "returns": "any"},
                )
            projected.update(present)
        for n in projected:
            # ``__init__`` is COPIED to the synthetic projection targets
            # (PromptManager / ToolRegistry each need a constructor of their
            # own), but it must never be MOVED off AgentBase: AgentBase has its
            # own real ``AgentBase(name, route, host, port)`` ctor, and popping
            # it here erased the whole construction contract for the port's
            # widest class.
            if n == "__init__":
                continue
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

    # request_options reconciliation for the HAND-WRITTEN base resource classes
    # (ReadResource.paginate / CrudWithAddresses.list_addresses in
    # base_resource.hpp — CrudWithAddresses is the oracle name for the C++
    # FabricResource). The reference (PY-7/PY-9) types ``request_options`` as a
    # KEYWORD-ONLY slot that PRECEDES the ``**params`` door
    # (``paginate(*, request_options=None, **params)`` /
    # ``list_addresses(self, resource_id, *, request_options=None, **params)``).
    # C++ keeps the ergonomic declaration order (``params`` then a defaulted
    # ``request_options``) so existing positional callers of ``(id, params)`` still
    # compile; here we (1) mark the port ``request_options`` param keyword and
    # (2) MOVE it before any trailing ``params`` var_keyword door, so the recorded
    # signature aligns with the oracle. Pure call-site-sugar reconciliation, not a
    # contract change (RULES §2 — idiom via the enumerator, never omission).
    # ``HttpClient`` records request_options POSITIONAL on every transport verb, so
    # it is deliberately excluded.
    _RO_KEYWORD_BASE_CLASSES = {
        "ReadResource",
        "CrudResource",
        "CrudWithAddresses",
        "FabricResource",
    }
    _bm = out_modules.get("signalwire.rest._base", {})
    for _bcls_name, _bcls in _bm.get("classes", {}).items():
        if _bcls_name not in _RO_KEYWORD_BASE_CLASSES:
            continue
        for _bsig in _bcls.get("methods", {}).values():
            _bparams = _bsig.get("params", [])
            _ro_idx = next(
                (
                    i
                    for i, p in enumerate(_bparams)
                    if p.get("name") == "request_options"
                ),
                None,
            )
            if _ro_idx is None:
                continue
            _bparams[_ro_idx]["kind"] = "keyword"
            # Move request_options before a trailing ``params`` query door (recorded
            # by libclang as a positional ``dict<string,string>`` / var_keyword map),
            # so request_options lands at the reference's position and ``params`` is
            # the ignored trailing extra.
            if (
                _ro_idx == len(_bparams) - 1
                and len(_bparams) >= 2
                and _bparams[_ro_idx - 1].get("name") == "params"
            ):
                _bparams[_ro_idx - 1], _bparams[_ro_idx] = (
                    _bparams[_ro_idx],
                    _bparams[_ro_idx - 1],
                )

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

    # METHOD-LEVEL options-carrier unfold. The construction contract already
    # unfolds an options STRUCT param into its named fields (RelayClient(
    # RelayConfig{...}) vs five reference kwargs); ordinary METHODS carry the
    # same idiom and needed the same fold. Two carrier spellings, one rule —
    # see _project_options_carrier for the evidence gates.
    _project_options_carrier(out_modules, options_by_ref, guards)

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

    # Built-in-skill value accessors: the skill classes live in
    # ``src/skills/builtin/*.cpp``, which the libclang HEADER walk never opens,
    # so an implemented accessor would otherwise read as missing-port drift.
    # Signature-side twin of enumerate_surface's ``_project_builtin_skills``.
    _project_skill_accessors(out_modules)

    # AI-Chat signature fold: the Python reference records the whole AI-Chat
    # surface in ONE module (signalwire.ai_chat.client) with kwargs-exploded
    # method params; the C++ port splits it across ai_chat_client.hpp /
    # ai_chat_error.hpp and takes idiomatic options-structs. Fold the C++ shape
    # onto the oracle: canonical module, options-struct params unfolded to the
    # reference's keyword params, del()->delete, RAII->close, url() getter
    # dropped, error getters/protected fields dropped. Verified against the
    # genuine header symbols so it never emits a member the port lost.
    _project_ai_chat_signatures(out_modules)

    # GENERAL public-data-field projection. Every class libclang walked carries
    # its public data members in ``struct_fields``; the reference records such
    # caller-readable state as a self-only getter (a public ``__init__``
    # attribute under the oracle's class-B2 rule, or a @dataclass field).
    # Project each field as a zero-arg getter wherever the oracle records that
    # name on the SAME class — the intersection is what keeps a port-internal
    # public field from becoming invented surface. This is the signature-side
    # twin of enumerate_surface's ``_gate_field_members``; the per-header
    # ``_project_named_struct_getters`` calls below remain for the structs
    # libclang does not reach.
    _project_public_fields_as_getters(out_modules, struct_fields)

    # ``set_<x>`` writers fold onto ``<x>`` where the reference records ``<x>``
    # on the same class — the reference spells caller-supplied configuration as
    # a plain attribute (read AND write), C++ splits it into accessor + fluent
    # setter. Same capability, different shape: idiom, folded at emission
    # (ALLOWLIST_DISCIPLINE §0). Twin of enumerate_surface's ``_fold_setters``.
    _fold_setter_signatures(out_modules)

    # Typed relay Event dataclasses: project their public @dataclass payload fields
    # as zero-arg getters, gated on the oracle's signalwire.relay.event getter set
    # (the structs inherit from RelayEvent, so the generated-payload parser can't
    # reach them — this handles the inheriting form). Field-vs-getter idiom, RULES §2.
    _project_named_struct_getters(
        out_modules,
        "signalwire.relay.event",
        PORT_ROOT / "include" / "signalwire" / "relay" / "typed_events.hpp",
    )

    # RequestOptions optional-field getters (timeout/retries/retry_on_status/
    # retry_backoff), gated on the oracle's _request_options getter set. abort_signal
    # is a pointer field (documented cpp_field_not_property omission) and merge is a
    # real method libclang already emits — neither is touched here.
    _project_named_struct_getters(
        out_modules,
        "signalwire.rest._request_options",
        PORT_ROOT / "include" / "signalwire" / "rest" / "request_options.hpp",
    )

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
        "construction": build_construction(
            sorted_modules, struct_fields, options_by_ref
        ),
    }, failures


# ---------------------------------------------------------------------------
# Construction contract (porting-sdk ALLOWLIST_DISCIPLINE.md §10)
# ---------------------------------------------------------------------------

# Members that are construction MECHANISM, never a construction parameter.
_CONSTRUCTION_NON_PARAMS = frozenset(
    {
        "__init__",
        "__repr__",
        "__eq__",
        "from_payload",
        "from_params",
        "from_env",
        "from_json",
        "to_json",
        "merge",
        "build",
        "builder",
        "clone",
    }
)

# C++ ctor / accessor parameter spellings that name the SAME configurable as the
# reference, under a different word. A RENAME (ALLOWLIST_DISCIPLINE §7 / RULES §2
# adapter rename), never an omission: the port and the reference drive the same
# server capability, C++ just spells the knob differently. Keyed by
# ``module.Class.cpp_name`` -> canonical reference param name.
_CONSTRUCTION_PARAM_RENAMES: dict[str, str] = {
    # RestClient(space, project_id, token): ``space`` IS the reference's ``host``
    # (the SignalWire space host) and ``project_id`` its ``project``.
    "signalwire.rest.client.RestClient.space": "host",
    "signalwire.rest.client.RestClient.project_id": "project",
}

# Every REST resource/namespace class takes the shared HTTP transport as its
# first ctor arg; C++ names that parameter ``client``, the reference names it
# ``http``. Same object, same position, same capability — one spelling
# difference across ~49 generated classes, so it is expressed as a RULE keyed on
# the reference's own param set rather than 49 hand-written rename rows (a
# hand list would silently rot as resources are added). Applied ONLY when the
# reference class really does declare ``http`` and not ``client``, which leaves
# ``signalwire.relay.call.Call`` — where BOTH spell it ``client`` — untouched.
_TRANSPORT_PARAM_RENAME = ("client", "http")


def _construction_params_from_signature(
    sig: dict, options_by_ref: dict, ref_param_names: set
) -> dict:
    """Name-keyed construction params from an emitted ``__init__`` signature.

    A parameter whose TYPE is a known options struct is UNFOLDED into that
    struct's named fields rather than kept as one opaque carrier: C++ spells
    ``RelayClient(RelayConfig{project, token, host, contexts, …})`` where the
    reference spells the same capability as five kwargs, and the contract
    compares the named SET, not the carrying mechanism (§10). Without the
    unfold, six reference configurables would hide behind a single ``config``.

    The unfold is skipped when the REFERENCE itself carries a param of that
    name (``RestClient(request_options=RequestOptions(...))`` — the reference
    passes the same options object), because there the carrier IS the contract
    and flattening it would both lose the typed param and invent four extras.
    """
    params: dict = {}
    for p in sig.get("params", []):
        if not isinstance(p, dict):
            continue
        if (p.get("kind") or "positional") in ("self", "cls"):
            continue
        name = p.get("name")
        if not name or name.startswith("_"):
            continue
        unfold = options_by_ref.get(p.get("type", ""))
        if unfold and name in ref_param_names:
            unfold = None
        if unfold:
            for fname, ftype in unfold.items():
                # An options-struct field is optional by construction: C++
                # aggregate init lets you set any subset.
                params.setdefault(fname, {"type": ftype, "required": False})
            continue
        params[name] = {
            "type": p.get("type", "any"),
            "required": bool(p.get("required", True)),
        }
    return params


def build_construction(
    modules: dict, struct_fields: dict, options_by_ref: dict | None = None
) -> dict:
    """Return ``{"module.Class": {"params": {name: {type, required}}}}``.

    A NAME-KEYED, unordered SET of configurable parameters — order, arity and
    MECHANISM are the parts idiom is entitled to vary (porting-sdk
    ALLOWLIST_DISCIPLINE.md §10). This inverts the matching key for constructors
    from position (name-blind) to name (position-blind), which is what makes a
    22-kwarg Python constructor comparable against C++'s three different
    construction idioms.

    C++ reaches construction three ways, and all three are the SAME contract:

      1. **Constructor parameters** — the ordinary case (``AgentServer(host,
         port)``, ``Call(call_id, node_id)``).
      2. **Aggregate/config-struct public FIELDS** — ``RelayConfig{project,
         token, host, contexts, max_active_calls}``, ``RequestOptions{timeout,
         retries, …}`` and the 24 typed relay-event payload structs. C++
         aggregate-initializes these by field NAME, so the public data members
         ARE the named construction set. This is exactly the shape the blanket
         ``cpp_constructor_default_only`` omission used to hide.
      3. **Named accessors on a default-constructed object** — the
         ``Service()``-then-``set_host(…)``/``set_port(…)`` idiom. A ``set_x``
         setter (or a bare ``x`` getter over a settable field) names one
         configurable, the same way a Java builder setter does.

    Sources are merged in that precedence order: a real ctor param's ``required``
    flag wins over a struct field's or a setter's implicit optionality, because a
    setter/aggregate field is optional BY CONSTRUCTION (you may set any subset)
    while a ctor param may genuinely be mandatory. Where C++ spells a knob
    differently the ADAPTER canonicalizes it via ``_CONSTRUCTION_PARAM_RENAMES``
    (ADAPTER_CONTRACT rule 3) — name-keyed matching gives names weight they did
    not carry under positional matching, so the canonicalization is explicit.
    """
    out: dict = {}
    options_by_ref = options_by_ref or {}
    # The oracle's own construction node — consulted ONLY to decide whether an
    # options-struct param should stay a typed carrier (the reference passes the
    # same object) or be unfolded. It never adds a param the port lacks: the
    # gaps are the diff's job to report, not the emitter's to paper over.
    ref_construction = _load_python_signatures().get("construction", {}) or {}

    # ---- source 1: the class's own constructor parameters -----------------
    for mod, entry in modules.items():
        for cls, cinfo in entry.get("classes", {}).items():
            init = cinfo.get("methods", {}).get("__init__")
            if not isinstance(init, dict):
                continue
            ref_names = set(ref_construction.get(f"{mod}.{cls}", {}).get("params", {}))
            params = _construction_params_from_signature(
                init, options_by_ref, ref_names
            )
            if params:
                out[f"{mod}.{cls}"] = {"params": params}

    # ---- source 2: config-struct / payload-struct public FIELDS ----------
    # Public data members collected from the AST (``FIELD_DECL``), already keyed
    # by canonical ``module.Class``. C++ aggregate-initialization sets these by
    # NAME and lets you set any subset, so each is an optional construction
    # param. This is the shape ``cpp_constructor_default_only`` used to hide.
    #
    # EXCLUDED: a field whose type is another SDK CLASS. In the generated REST
    # containers (``FabricNamespace``, ``ResourceTree``, …) the public members
    # are sub-resource NAVIGATION HANDLES, every one of them built in the ctor's
    # member-init list out of the single ``http`` argument — they are reachable
    # storage, not configurables, and admitting them invented 110 construction
    # params the reference rightly does not have. (The member-init list itself is
    # invisible under ``PARSE_SKIP_FUNCTION_BODIES``, which the umbrella parse
    # needs for its 3-10x speedup, so the field's TYPE is the discriminator.) A
    # configurable genuinely carried as an SDK-class handle still reaches the
    # contract as a ctor parameter via source 1.
    for mod, entry in modules.items():
        for cls in entry.get("classes", {}):
            key = f"{mod}.{cls}"
            fields = struct_fields.get(key)
            if not fields:
                continue
            params = out.setdefault(key, {"params": {}})["params"]
            for fname, ftype in fields.items():
                if str(ftype).startswith("class:"):
                    continue
                params.setdefault(fname, {"type": ftype, "required": False})

    # ---- source 3: single-argument ``set_x`` SETTERS on the emitted class --
    # The default-construct-then-configure idiom: ``Service()`` followed by
    # ``set_host(…)`` / ``set_port(…)``. A one-arg ``set_x`` names exactly one
    # configurable ``x`` — the C++ analogue of a Java builder setter — and is
    # optional by construction (you may set any subset).
    #
    # Deliberately NOT a source: bare zero-arg methods. A zero-arg ``x()`` is
    # just as likely a BEHAVIOUR (``stop()``, ``connect()``, ``render_swml()``)
    # as a field read, and admitting them invents construction params the port
    # does not actually offer. Only the explicit ``set_`` prefix is evidence of
    # a configurable, so a genuine getter-only field reaches the contract via
    # source 2 (its struct field) or not at all.
    for mod, entry in modules.items():
        for cls, cinfo in entry.get("classes", {}).items():
            key = f"{mod}.{cls}"
            setters: dict = {}
            for mname, msig in (cinfo.get("methods") or {}).items():
                if not mname.startswith("set_") or mname in _CONSTRUCTION_NON_PARAMS:
                    continue
                if not isinstance(msig, dict):
                    continue
                args = [
                    p
                    for p in msig.get("params", [])
                    if (p.get("kind") or "positional") not in ("self", "cls")
                ]
                if len(args) != 1:
                    continue
                pname = mname[4:]
                if not pname or pname.startswith("_"):
                    continue
                setters.setdefault(pname, args[0].get("type", "any"))
            if not setters:
                continue
            params = out.setdefault(key, {"params": {}})["params"]
            for pname, ptype in setters.items():
                params.setdefault(pname, {"type": ptype, "required": False})

    # ---- adapter canonicalization + stable ordering ----------------------
    # ADAPTER_CONTRACT rule 3: translate names to Python-canonical form HERE.
    # Name-keyed matching gives names weight they never carried under positional
    # matching, so the canonicalization is explicit rather than incidental.
    canonical: dict = {}
    cpp_transport, ref_transport = _TRANSPORT_PARAM_RENAME
    for key, entry in out.items():
        ref_names = set(ref_construction.get(key, {}).get("params", {}))
        params: dict = {}
        for pname, spec in entry["params"].items():
            if (
                pname == cpp_transport
                and ref_transport in ref_names
                and cpp_transport not in ref_names
            ):
                pname = ref_transport
            pname = _CONSTRUCTION_PARAM_RENAMES.get(f"{key}.{pname}", pname)
            # A rename may collide with an already-canonical name; the ctor
            # param (added first, possibly required) wins.
            params.setdefault(pname, spec)
        if params:
            canonical[key] = {"params": dict(sorted(params.items()))}
    return dict(sorted(canonical.items()))


def _load_rest_sidecar() -> dict:
    """Load the generator's rest_signatures.json (Class::method -> [records])."""
    sc = (
        PORT_ROOT
        / "include"
        / "signalwire"
        / "rest"
        / "namespaces"
        / "generated"
        / "rest_signatures.json"
    )
    if not sc.is_file():
        return {}
    return json.loads(sc.read_text()).get("methods", {})


def _generated_class_modules() -> dict[str, str]:
    """Map each generated resource/container CLASS -> its python module, from
    generated_surface_map.json (the same source enumerate_surface projects)."""
    smap = (
        PORT_ROOT
        / "include"
        / "signalwire"
        / "rest"
        / "namespaces"
        / "generated"
        / "generated_surface_map.json"
    )
    if not smap.is_file():
        return {}
    return json.loads(smap.read_text())


def _generated_container_members() -> dict[str, list[str]]:
    """Parse the generated namespace-container headers for their public resource
    member fields (FabricNamespace { AiAgents ai_agents; ... }) so the client
    tree's accessor surface can be projected onto the oracle shape."""
    gen_dir = PORT_ROOT / "include" / "signalwire" / "rest" / "namespaces" / "generated"
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
                f"generated_surface_map.json (regenerate the REST layer)"
            )
        out_modules.setdefault(mod, {"classes": {}})
        cls_entry = out_modules[mod]["classes"].setdefault(cls, {"methods": {}})
        for canon, records in methods.items():
            cls_entry["methods"][canon] = {
                "params": [{"name": "self", "kind": "self"}]
                + [dict(r) for r in records],
                "returns": "any",
            }
        # Ensure a constructor is present (POD resource: implicit default ctor).
        cls_entry["methods"].setdefault(
            "__init__",
            {
                "params": [{"name": "self", "kind": "self"}],
                "returns": "void",
            },
        )

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
                f"generated_surface_map.json (regenerate the REST layer)"
            )
        out_modules.setdefault(mod, {"classes": {}})
        cls_entry = out_modules[mod]["classes"].setdefault(cls, {"methods": {}})
        for member in members:
            cls_entry["methods"].setdefault(
                member,
                {
                    "params": [{"name": "self", "kind": "self"}],
                    "returns": "any",
                },
            )
        cls_entry["methods"].setdefault(
            "__init__",
            {
                "params": [
                    {"name": "self", "kind": "self"},
                    {"name": "http", "type": "any", "required": True},
                ],
                "returns": "void",
            },
        )


# A public data-member declaration inside one of the generated payload structs,
# with the optional trailing ``// wire key: <name>`` comment the generator emits
# for reserved-word-avoidance renames (``default_``/``enum_`` → ``default``/
# ``enum``). Group 1 = the C++ field identifier, group 2 = the wire-key comment
# (if present). Method declarations (which contain ``(``) are excluded before
# this regex is applied.
_GEN_FIELD_RE = re.compile(
    r"^\s+(?:\[\[[^\]]*\]\]\s*)?[A-Za-z_][\w:<>,\s]*?[>\w]\s+([A-Za-z_]\w*)\s*(?:=\s*[^;]+)?;\s*(//.*)?$"
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


# ``struct/class Name[ : bases] { … };`` — captures a named struct body even when
# it inherits (``: public RelayEvent``), which the generated-payload ``struct Name {``
# regex above does not. Used for the relay Event dataclasses + RequestOptions.
_NAMED_STRUCT_RE_SIG = re.compile(
    r"(?:struct|class)\s+(\w+)\s*(?::[^{]+)?\{(.*?)\n\};", re.S
)


def _named_struct_public_fields(header: Path) -> dict[str, list[str]]:
    """``{StructName: [wire_field, …]}`` for every named struct/class in
    ``header`` (inheriting or not), each field mapped to its wire-key name
    (honouring ``// wire key:``). A method has ``(`` before the first ``=``/``;``;
    a data-member field carries ``(`` only inside an initializer
    (``json x = json::object();``) — discriminate on paren position so the
    ``json …`` payload fields survive while methods are skipped."""
    out: dict[str, list[str]] = {}
    if not header.is_file():
        return out
    src = header.read_text(encoding="utf-8")
    for sm in _NAMED_STRUCT_RE_SIG.finditer(src):
        cls, body = sm.group(1), sm.group(2)
        fields: list[str] = []
        for line in body.splitlines():
            eq, sc = line.find("="), line.find(";")
            bounds = [i for i in (eq, sc) if i != -1]
            boundary = min(bounds) if bounds else len(line)
            lp = line.find("(")
            if lp != -1 and lp < boundary:
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


def _oracle_class_members(module: str, cls: str) -> set[str]:
    """Every member the reference oracle records on ``module.Class`` (methods,
    which is how the signature oracle spells an attribute too). Empty when the
    reference has no such class.

    For an ``agentbase-family`` class the diff collapses the ``module.Class``
    prefix away entirely, so an AgentBase member may be recorded by the
    reference on ANY family class (a mixin) — union the whole family there."""
    ref = _load_python_signatures()
    ref_modules = ref.get("modules", {})
    ref_cls = ref_modules.get(module, {}).get("classes", {}).get(cls)
    out: set[str] = set(ref_cls.get("methods", {})) if ref_cls else set()
    if module == "signalwire.core.agent_base":
        for mod, entry in ref_modules.items():
            if mod != "signalwire.core.agent_base" and not mod.startswith(
                "signalwire.core.mixins."
            ):
                continue
            for cls_entry in entry.get("classes", {}).values():
                out |= set(cls_entry.get("methods", {}))
    return out


def _project_public_fields_as_getters(out_modules: dict, struct_fields: dict) -> None:
    """Emit every public data-member FIELD as a zero-arg getter, gated on the
    oracle recording that name on the SAME class.

    A field and a zero-arg accessor are the same read surface; the reference
    spells both as a plain ``self.<name>`` attribute the signature oracle
    records as a self-only method. libclang emits no method cursor for a field,
    so without this a genuinely-implemented member reads as missing-port drift.
    The same-class oracle gate is what prevents inventing surface (RULES §2 /
    ALLOWLIST_DISCIPLINE §0a)."""
    for key, fields in struct_fields.items():
        mod, _, cls = key.rpartition(".")
        if not mod or not cls:
            continue
        allowed = _oracle_class_members(mod, cls)
        if not allowed:
            continue
        cls_entry = out_modules.get(mod, {}).get("classes", {}).get(cls)
        if cls_entry is None:
            continue
        for field in fields:
            if field in allowed:
                cls_entry["methods"].setdefault(
                    field,
                    {
                        "params": [{"name": "self", "kind": "self"}],
                        "returns": "any",
                    },
                )


def _fold_setter_signatures(out_modules: dict) -> None:
    """Collapse a ``set_<x>`` writer onto ``<x>`` when the reference oracle
    records ``<x>`` — but not ``set_<x>`` itself — on the SAME class.

    See ``enumerate_surface._fold_setters``: the writer is the C++ half of the
    reference's plain public attribute, so it is idiom folded at emission, not
    additional surface. A ``set_<x>`` the oracle records verbatim
    (``FunctionResult.set_response``) is left alone; so is one whose ``<x>``
    the reference lacks on this class, which keeps the fold from laundering
    genuinely port-only surface and avoids the cross-class fold RULES §4
    forbids."""
    for mod, entry in out_modules.items():
        for cls, cls_entry in entry.get("classes", {}).items():
            allowed = _oracle_class_members(mod, cls)
            if not allowed:
                continue
            methods = cls_entry.get("methods", {})
            for name in list(methods):
                if not name.startswith("set_"):
                    continue
                target = name[4:]
                if name in allowed or target not in allowed:
                    continue
                methods.pop(name)
                # The reference's ``<x>`` is a plain public ATTRIBUTE, which the
                # signature oracle records as a self-only member. Fold to that
                # shape — carrying the setter's ``(self, value) -> Self``
                # signature over would be a spurious arity/return mismatch
                # against an attribute.
                methods.setdefault(
                    target,
                    {
                        "params": [{"name": "self", "kind": "self"}],
                        "returns": "any",
                    },
                )


def _project_named_struct_getters(out_modules: dict, module: str, header: Path) -> None:
    """Project a header's named-struct public data-member FIELDS as zero-arg
    property getters onto ``module``, gated on the oracle's per-class getter set.

    The reference records each @dataclass field as a self-only getter; the C++
    port carries them as public struct fields libclang emits no method for, so
    without this every oracle getter reads as missing-port DRIFT. Emit only a
    field the oracle records as a getter for that class (the intersection guards
    against inventing surface). Field-vs-getter SHAPE idiom via the enumerator
    (RULES §2), the analogue of ``_project_gen_payload_getters`` for the
    inheriting relay Event structs + RequestOptions."""
    ref = _load_python_signatures()
    ref_classes = ref.get("modules", {}).get(module, {}).get("classes", {})
    if not ref_classes:
        return
    struct_fields = _named_struct_public_fields(header)
    if not struct_fields:
        return
    mod_entry = out_modules.setdefault(module, {"classes": {}})
    mod_entry.setdefault("classes", {})
    for cls, fields in struct_fields.items():
        ref_cls = ref_classes.get(cls)
        if not ref_cls:
            continue
        oracle_getters = {m for m in ref_cls.get("methods", {}) if m != "__init__"}
        present = [f for f in fields if f in oracle_getters]
        if not present:
            continue
        cls_entry = mod_entry["classes"].setdefault(cls, {"methods": {}})
        for field in present:
            cls_entry["methods"].setdefault(
                field,
                {
                    "params": [{"name": "self", "kind": "self"}],
                    "returns": "any",
                },
            )


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


# ---------------------------------------------------------------------------
# AI-Chat signature projection (idiom fold onto the python oracle)
# ---------------------------------------------------------------------------
def _project_ai_chat_signatures(out_modules: dict) -> None:
    """Emit signalwire.ai_chat.client signatures in the oracle's kwargs-exploded
    shape, unfolding the C++ options-structs. Verified against the genuine header
    symbols (abort-loud on a missing one) so nothing is invented. Drops the
    mis-routed native ai_chat modules."""
    import re as _re

    client_hpp = PORT_ROOT / "include" / "signalwire" / "ai_chat" / "ai_chat_client.hpp"
    if not client_hpp.is_file():
        return  # port doesn't ship AI-Chat
    txt = client_hpp.read_text(encoding="utf-8")

    def _need(pattern: str, what: str) -> None:
        if not _re.search(pattern, txt):
            raise SystemExit(
                f"enumerate_signatures: AI-Chat projection expected {what} in "
                f"{client_hpp.name} but it is gone -- fix the projection, do not "
                f"emit a member the port no longer has"
            )

    # Client + RAII/verbs the projection reconciles must genuinely exist.
    _need(r"\bclass\s+AIChatClient\b", "class AIChatClient")
    _need(r"\bexplicit\s+AIChatClient\s*\(", "AIChatClient ctor (options struct)")
    _need(r"~AIChatClient\s*\(", "AIChatClient dtor (RAII -> close)")
    for _m in ("create_conversation", "chat", "end", "log", "summarize"):
        _need(rf"\b{_m}\s*\(", f"AIChatClient::{_m}")
    _need(r"\bbool\s+del\s*\(", "AIChatClient::del (reference delete)")
    _need(r"\bvoid\s+close\s*\(", "AIChatClient::close (folds reference close)")
    # The class-B2 ctor-param reads the projection emits below.
    _need(r"\burl\s*\(\s*\)\s*const", "AIChatClient::url (reference self.url)")
    _need(r"\bint\s+code\s*\(\s*\)\s*const", "AIChatError::code")
    _need(
        r"\bserver_message\s*\(\s*\)\s*const",
        "AIChatError::server_message (reference message)",
    )
    # Options structs whose fields the unfold below relies on.
    for _s in (
        "AIChatClientOptions",
        "CreateConversationOptions",
        "ChatOptions",
        "SummarizeOptions",
        "ConversationTurnOptions",
    ):
        _need(rf"\bstruct\s+{_s}\b", f"struct {_s}")
    # Error family + result structs.
    _need(r"\bclass\s+AIChatError\b", "class AIChatError")
    for _c in ("ConversationInfo", "ChatResponse", "ChatLog"):
        _need(rf"\bstruct\s+{_c}\b", f"struct {_c}")

    def _self():
        return {"name": "self", "kind": "self"}

    def _p(name, typ, required, default=None):
        d = {"name": name, "type": typ, "required": required}
        if not required:
            d["default"] = default
        return d

    # Oracle-shaped signatures. Every non-self param below is backed by a genuine
    # C++ options-struct field or method arg (verified present above); the C++
    # options-struct is the idiomatic carrier of the reference's keyword params.
    #
    # __init__: the AIChatClientOptions carrier explodes to project/token/space/url.
    # The reference's 5th param ``session`` (an injectable aiohttp.ClientSession)
    # has NO C++ analog -- cpp-httplib is stateless/per-request, so there is no
    # persistent session object to inject. Recorded as a PORT_SIGNATURE_OMISSIONS
    # idiom divergence (cpp-stateless-transport), NOT invented here.
    aic = {
        "__init__": {
            "params": [
                _self(),
                _p("project", "optional<string>", False),
                _p("token", "optional<string>", False),
                _p("space", "optional<string>", False),
                _p("url", "optional<string>", False),
            ],
            "returns": "void",
        },
        "chat": {
            "params": [
                _self(),
                _p("conversation_id", "string", True),
                _p("message", "string", True),
                _p("role", "string", False, "user"),
                _p("config_url", "optional<string>", False),
                _p("user_metadata", "optional<dict<string,any>>", False),
                _p("timeout", "optional<int>", False),
                _p("reinit", "bool", False, False),
            ],
            "returns": "class:signalwire.ai_chat.client.ChatResponse",
        },
        "create_conversation": {
            "params": [
                _self(),
                _p("conversation_id", "string", True),
                _p("config_url", "string", True),
                _p("user_message", "optional<string>", False),
                _p("timeout", "optional<int>", False),
                _p("user_metadata", "optional<dict<string,any>>", False),
                _p("reinit", "bool", False, False),
            ],
            "returns": "class:signalwire.ai_chat.client.ConversationInfo",
        },
        "end": {
            "params": [_self(), _p("conversation_id", "string", True)],
            "returns": "bool",
        },
        "delete": {
            "params": [_self(), _p("conversation_id", "string", True)],
            "returns": "bool",
        },
        "log": {
            "params": [_self(), _p("conversation_id", "string", True)],
            "returns": "class:signalwire.ai_chat.client.ChatLog",
        },
        "summarize": {
            "params": [
                _self(),
                _p("conversation_id", "string", True),
                _p("summary_prompt", "optional<string>", False),
            ],
            "returns": "string",
        },
        # close() is a genuine C++ method (RAII no-op) folded onto the reference
        # close. __aenter__/__aexit__ are NOT emitted: the context-manager
        # PROTOCOL dunders have no snake_case-nameable C++ member (surface
        # PORT_OMISSIONS impossible:, TS/PHP/perl/dotnet fleet-consistent).
        "close": {"params": [_self()], "returns": "void"},
        # url: the reference's `self.url` — a public __init__ attribute that is
        # ALSO a ctor param, recorded by the oracle's class-B2 rule. The C++
        # `url()` const getter is that attribute's read.
        "url": {"params": [_self()], "returns": "any"},
    }

    err = {
        "__init__": {
            "params": [
                _self(),
                _p("code", "optional<int>", True),
                _p("message", "string", True),
            ],
            "returns": "void",
        },
        # code / message: ctor params the reference stores publicly, recorded by
        # the oracle's class-B2 rule. C++ reads them via `code()` and
        # `server_message()` (the latter renamed to avoid colliding with
        # std::runtime_error's message semantics).
        "code": {"params": [{"name": "self", "kind": "self"}], "returns": "any"},
        "message": {"params": [{"name": "self", "kind": "self"}], "returns": "any"},
    }

    # Each result DTO is @dataclass-shaped in the reference: besides ``__init__``
    # the oracle records every field as a zero-arg property getter. The C++ port
    # carries them as public struct fields; emit the oracle's getter shape (self-
    # only, ``any`` return — types_compatible treats ``any`` as compatible with the
    # oracle's typed getter returns) so the field-idiom folds onto the reference.
    def _getter() -> dict:
        return {"params": [_self()], "returns": "any"}

    conv_info = {
        "__init__": {
            "params": [
                _self(),
                _p("id", "string", True),
                _p("status", "string", True),
                _p("initial_message", "optional<string>", False),
            ],
            "returns": "void",
        },
        "id": _getter(),
        "status": _getter(),
        "initial_message": _getter(),
    }
    chat_resp = {
        "__init__": {
            "params": [
                _self(),
                _p("text", "string", True),
                _p("conversation_id", "string", True),
                _p("user_event", "optional<dict<string,any>>", False),
            ],
            "returns": "void",
        },
        "text": _getter(),
        "conversation_id": _getter(),
        "user_event": _getter(),
    }
    chat_log = {
        "__init__": {
            "params": [
                _self(),
                _p("messages", "list<dict<string,any>>", False, "list()"),
                _p("call_timeline", "list<dict<string,any>>", False, "list()"),
            ],
            "returns": "void",
        },
        "messages": _getter(),
        "call_timeline": _getter(),
    }

    # Drop the mis-routed native modules, emit the single canonical one.
    for _native in (
        "signalwire.ai_chat.ai_chat_client",
        "signalwire.ai_chat.ai_chat_error",
    ):
        out_modules.pop(_native, None)

    mod = out_modules.setdefault("signalwire.ai_chat.client", {"classes": {}})
    mod["classes"]["AIChatClient"] = {"methods": aic}
    mod["classes"]["AIChatError"] = {"methods": err}
    mod["classes"]["ConversationInfo"] = {"methods": conv_info}
    mod["classes"]["ChatResponse"] = {"methods": chat_resp}
    mod["classes"]["ChatLog"] = {"methods": chat_log}


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
    call_mod = out_modules.setdefault(
        "signalwire.relay.call", {"classes": {}, "functions": {}}
    )
    call_classes = call_mod.setdefault("classes", {})

    # The BASE ``Action``: the reference declares it in ``signalwire.relay.call``
    # with __init__/is_done/wait/result plus the ctor/derived attrs it stores
    # publicly — ``call`` (the back-reference), ``control_id``, and
    # ``completed`` (the bool state flag that starts False and flips True on
    # completion) — which the oracle's class-B2 rule records. The unified C++
    # Action carries all of them (``is_done()`` delegates to ``completed()``);
    # project the reference-recorded subset onto relay.call so the base
    # symbol lines up (the richer C++ surface stays under relay.action).
    base_entry = call_classes.setdefault("Action", {"methods": {}})
    for m in (
        "__init__",
        "is_done",
        "wait",
        "result",
        "control_id",
        "call",
        "completed",
    ):
        if m in action_cls:
            base_entry["methods"].setdefault(m, action_cls[m])
    # ``call`` is REFERENCE surface (relay.call.Action.call), now homed on the
    # class where the reference declares it. Leaving a second copy under the
    # port's native relay.action module would make the same member read as a
    # port addition there. One member, one home.
    action_cls.pop("call", None)

    for sub, methods in _RELAY_ACTION_CONTROL_METHODS.items():
        entry = call_classes.setdefault(sub, {"methods": {}})
        for m in methods:
            if m in action_cls:  # only if the C++ Action truly defines it
                entry["methods"][m] = action_cls[m]


# Built-in-skill members the oracle records as a SIGNATURE (not merely surface
# membership) and that the C++ port implements as a zero-arg accessor on the
# skill class. The skill classes live in ``.cpp`` IMPLEMENTATION files the
# header walker never opens — so libclang cannot see them and the accessor
# would read as missing-port drift even though it is implemented. This is the
# signature-side twin of enumerate_surface's ``_project_builtin_skills``: same
# fold, same fail-honest rule.
#
# ``oracle_key -> (candidate cpp sources, cpp class, {member: accessor})``.
# The tuple of sources used to carry TWO entries for spider, because the skill
# was registered twice — once by ``src/skills/builtin/spider.cpp`` and once by a
# duplicate ``SpiderSkillR`` in ``src/skills/skill_registry.cpp`` — with
# ``register_skill`` silently overwriting, so which class ran was decided by
# unspecified cross-TU static-init order. That duplication is now GONE (the
# ``*SkillR`` copies were deleted and ``register_skill`` throws on a duplicate
# name), so each skill has exactly one defining source and the file the
# enumerator reads is the file that runs.
#
# A member is projected ONLY when the named accessor is genuinely present in
# EVERY listed source — a deleted or renamed accessor drops out rather than
# being invented (RULES §2/§3).
_SKILL_ACCESSOR_PROJECTIONS: dict[str, tuple[tuple[str, ...], str, dict[str, str]]] = {
    "signalwire.skills.spider.skill.SpiderSkill": (
        ("src/skills/builtin/spider.cpp",),
        "SpiderSkill",
        # ``self.remove_xpaths`` — the PREFILLED xpath list the reference sets
        # in ``__init__`` and walks in ``_fast_text_extract``. C++ idiom: a
        # field plus a ``remove_xpaths()`` reader (+ ``set_remove_xpaths``).
        {"remove_xpaths": "remove_xpaths"},
    ),
}


def _project_skill_accessors(out_modules: dict) -> None:
    """Project built-in-skill value accessors (see _SKILL_ACCESSOR_PROJECTIONS).

    The C++ skill classes are defined in ``.cpp`` implementation files, which
    the libclang header walk never parses. Verify the accessor really exists in
    every candidate source and emit the oracle-shaped zero-arg signature for
    it; skip it entirely when any source or the accessor is absent, so the
    enumerator can never invent surface the port does not have.
    """
    for oracle_key, (
        cpp_files,
        _cpp_cls,
        members,
    ) in _SKILL_ACCESSOR_PROJECTIONS.items():
        srcs = [PORT_ROOT / f for f in cpp_files]
        if not all(s.is_file() for s in srcs):
            continue  # skill not implemented in this tree — don't invent it
        texts = [s.read_text(encoding="utf-8") for s in srcs]
        module, cls = oracle_key.rsplit(".", 1)
        for member, accessor in members.items():
            # The accessor must be DEFINED (``name() const {`` / ``name() {``),
            # not merely mentioned. A bare call site does not count.
            pat = re.compile(
                r"\b"
                + re.escape(accessor)
                + r"\s*\(\s*\)\s*(?:const\s*)?(?:noexcept\s*)?\{"
            )
            if not all(pat.search(t) for t in texts):
                continue
            mod_entry = out_modules.setdefault(module, {"classes": {}, "functions": {}})
            cls_entry = mod_entry.setdefault("classes", {}).setdefault(
                cls, {"methods": {}}
            )
            cls_entry["methods"].setdefault(
                member,
                {
                    "params": [{"name": "self", "kind": "self"}],
                    "returns": "list<string>",
                },
            )


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
            oracle_getters = {m for m in ref_cls.get("methods", {}) if m != "__init__"}
            present = [f for f in fields if f in oracle_getters]
            if not present:
                continue
            cls_entry = mod_entry["classes"].setdefault(cls, {"methods": {}})
            for field in present:
                cls_entry["methods"].setdefault(
                    field,
                    {
                        "params": [{"name": "self", "kind": "self"}],
                        "returns": "any",
                    },
                )
            # Method-less POD: implicit default constructor is available.
            cls_entry["methods"].setdefault(
                "__init__",
                {
                    "params": [{"name": "self", "kind": "self"}],
                    "returns": "void",
                },
            )


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


# ---------------------------------------------------------------------------
# Method-level options-carrier unfold
# ---------------------------------------------------------------------------
#
# THE IDIOM
# =========
# Python spells "these N knobs are individually optional" as N keyword-only
# params. C++ has no keyword arguments, so the SAME contract is carried by ONE
# object parameter — either a typed options STRUCT (``RenderOptions``) or an
# untyped ``const json&`` bag. The construction contract already unfolds the
# struct form (``RelayClient(RelayConfig{project, token, …})`` vs five reference
# kwargs, see ``_construction_params_from_signature``); ordinary METHODS carry
# exactly the same idiom, and this is the same fold applied to them.
#
# Folding at the emitter is the point: the diff keeps comparing every named knob
# afterwards, so dropping or renaming one still reports. An omission would stop
# comparing the whole method — which is what the nine ``cpp_options_object`` /
# ``cpp_options_struct`` entries this replaces were doing.
#
# THE BOUNDARY — a carrier, not every dict-typed parameter
# ========================================================
# A single dict-typed parameter is NOT automatically a carrier. The reference has
# genuine DOMAIN parameters whose value simply IS a dict — ``Call.execute_swml(
# swml)``, ``Call.refer(device)``, ``DataMap.foreach(mapping)``. Unfolding one of
# those would invent params the port never had and destroy a real one. The fold
# therefore fires only on EVIDENCE, never on the parameter's type alone:
#
#   TYPED-STRUCT form — the param's emitted type resolves to a known options
#     struct (``options_by_ref``). The struct's public FIELDS are the named set;
#     they are declared, not guessed. Skipped when the REFERENCE also declares a
#     param of that name (the reference passes the same object, so the carrier IS
#     the contract — same exclusion the construction unfold makes for
#     ``RestClient(request_options=…)``).
#
#   UNTYPED-BAG form — the param is the untyped ``any`` (``const json&``) AND the
#     method body SPREADS it onto the outgoing frame rather than reading fixed
#     keys out of it. ``Call::queue_enter`` is ``json p = params.is_object() ?
#     params : json::object(); p["queue_name"] = queue_name; return
#     execute_simple("queue.enter", p);`` — every key the caller puts in the bag
#     reaches the wire verbatim, so the reference's keyword names ARE reachable
#     through it and the two calls produce identical frames. A body that instead
#     does ``params["mapping"]`` / ``p["swml"] = swml`` is consuming a domain
#     value and is left alone.
#
# In both forms the reference must have named keyword params to unfold TO, and
# the port must not already declare them. Anything unproven stays as it was and
# reports drift, which is the correct outcome for a case this cannot decide.
_BAG_SPREAD_RE = (
    # ``json p = <param>.is_object() ? <param> : json::object();`` — the copy-the
    # whole-bag-then-add-fixed-keys idiom every Call.* action method uses.
    r"=\s*{n}\s*\.\s*is_object\s*\(\s*\)\s*\?\s*{n}\b",
    # ``for (auto& [k, v] : <param>.items()) out[k] = v;`` — explicit spread.
    r"\b{n}\s*\.\s*items\s*\(\s*\)",
    # ``p.update(<param>)`` / ``p.merge_patch(<param>)`` / ``p.insert(…{n}…)``.
    r"\.\s*(?:update|merge_patch)\s*\(\s*{n}\s*\)",
)


def _is_callable_type(t: str) -> bool:
    """True for a canonical type whose value is a function, not JSON data.

    ``callable<…>`` and ``optional<callable<…>>`` are the two spellings the
    oracle records. A JSON bag cannot hold either.
    """
    t = (t or "").strip()
    if t.startswith("optional<") and t.endswith(">"):
        t = t[len("optional<") : -1].strip()
    return t.startswith("callable<") or t == "callable"


def _bag_is_spread(
    guards: GuardIndex | None, cls: str, method: str, param: str, index: int
) -> bool:
    """True when SOME definition of ``cls::method`` spreads ``param`` wholesale.

    Same position-then-name resolution the guard scan uses: a definition may
    rename its parameters relative to the header, so the definition-side name at
    this index is tried first and the header spelling is the fallback.
    """
    if guards is None:
        return False
    for body, names in guards.bodies(cls, method):
        local = names[index] if 0 <= index < len(names) else None
        for name in (n for n in (local, param) if n):
            for pat in _BAG_SPREAD_RE:
                if re.search(pat.format(n=re.escape(name)), body):
                    return True
    return False


def _project_options_carrier(
    out_modules: dict, options_by_ref: dict, guards: GuardIndex | None
) -> None:
    """Unfold a method's options carrier into the reference's keyword params.

    See the block comment above for the idiom and the two evidence gates. The
    unfolded params are emitted ``kind: keyword`` with the reference's own type
    and default, because that is what the carrier genuinely offers: C++ aggregate
    init and a JSON bag both let the caller set any subset, in any order.
    """
    ref = _load_python_signatures()
    if not ref:
        return
    for mod, entry in out_modules.items():
        ref_classes = ref.get("modules", {}).get(mod, {}).get("classes", {})
        if not ref_classes:
            continue
        for cls, cinfo in entry.get("classes", {}).items():
            ref_methods = ref_classes.get(cls, {}).get("methods", {})
            if not ref_methods:
                continue
            for meth, sig in cinfo.get("methods", {}).items():
                # ``__init__`` is the CONSTRUCTION contract's, not this fold's.
                # ``build_construction`` runs its own unfold with deliberately
                # different semantics: it emits the options struct's WHOLE field
                # set, because a construction knob the reference lacks is still a
                # real configurable the port offers (and the diff reports it as
                # such). Folding here first would consume the carrier and hide
                # those — measured: it silently dropped RelayConfig's ``port`` /
                # ``max_connections`` / ``request_timeout_ms`` from
                # ``RelayClient``'s construction params.
                if meth == "__init__":
                    continue
                ref_sig = ref_methods.get(meth)
                if ref_sig:
                    _unfold_one_carrier(sig, ref_sig, options_by_ref, guards, cls, meth)


def _unfold_one_carrier(
    sig: dict,
    ref_sig: dict,
    options_by_ref: dict,
    guards: GuardIndex | None,
    cls: str,
    meth: str,
) -> None:
    port_params = sig.get("params", [])
    ref_params = ref_sig.get("params", [])
    if not port_params or not ref_params:
        return
    # The reference's OPTIONAL named params are what a carrier stands in for.
    # Two exclusions:
    #   * ``self``/``cls`` and the trailing ``**kwargs`` — the latter is already
    #     reconciled by ``_project_kwargs_shape``, and consuming it here would
    #     let a carrier match a method whose only reference knob is the open
    #     spread.
    #   * a REQUIRED param — an options carrier is optional by construction
    #     (both aggregate init and a JSON bag let the caller set any subset), so
    #     it cannot stand in for something the reference demands. A required
    #     reference param the port lacks is a genuine gap and must keep
    #     reporting.
    # The reference records keyword-ONLY params as ``kind: keyword`` and
    # positional-or-keyword ones with no ``kind`` at all; both are settable BY
    # NAME, which is the whole contract a carrier carries, so both qualify.
    ref_kw = [
        p
        for p in ref_params
        if p.get("name")
        and (p.get("kind") or "positional")
        not in ("self", "cls", "var_keyword", "var_positional")
        and p.get("required") is False
    ]
    if not ref_kw:
        return
    port_names = {p.get("name") for p in port_params}
    # Nothing to unfold to if the port already declares them all.
    target = [p for p in ref_kw if p["name"] not in port_names]
    if not target:
        return
    # The C++ definition's parameter list has no ``self``, so a port param's
    # position in the C++ signature is its index MINUS the leading self/cls.
    self_offset = sum(
        1 for p in port_params if (p.get("kind") or "positional") in ("self", "cls")
    )
    for idx, p in enumerate(port_params):
        if (p.get("kind") or "positional") in ("self", "cls"):
            continue
        name = p.get("name")
        if not name:
            continue
        # A carrier the REFERENCE also declares by name is the contract itself,
        # not a stand-in — leave it typed and whole.
        if any(rp.get("name") == name for rp in ref_params):
            continue
        # A REQUIRED parameter is not an optional-knob carrier. C++ overloads a
        # long reference signature as a domain OBJECT the caller must supply
        # (``define_tool(const ToolDefinition&)``,
        # ``add_language(const LanguageConfig&)``) — the object carries the
        # reference's REQUIRED params too (name/description/parameters/handler,
        # name/code/voice), which an optional-only unfold would silently drop
        # while still consuming the carrier. Measured: without this gate the
        # fold turned ``define_tool(tool)`` into ``define_tool(secure)`` and
        # ``add_language(lang)`` into five optional knobs, destroying the
        # required surface in both. An options carrier the caller may omit
        # entirely cannot be standing in for anything mandatory, so requiring
        # the carrier itself to be optional is exactly the right discriminator.
        if p.get("required") is not False:
            continue
        ptype = p.get("type", "")
        fields = options_by_ref.get(ptype)
        if fields is not None:
            # TYPED-STRUCT form: unfold only the reference keywords the struct
            # genuinely declares a field for. A reference keyword the struct
            # LACKS stays missing and keeps reporting as drift — the fold must
            # not manufacture a knob the port cannot set.
            unfold = [rp for rp in target if rp["name"] in fields]
        elif ptype == "any" and _bag_is_spread(
            guards, cls, meth, name, idx - self_offset
        ):
            # UNTYPED-BAG form: a proven wholesale spread reaches every key, so
            # every remaining reference knob is settable — but ONLY the ones a
            # JSON object can actually hold. A ``const json&`` cannot carry a
            # ``std::function``, so unfolding a reference CALLABLE out of it
            # would claim a callback the port does not accept. (Measured: the
            # fold otherwise invented ``on_completed`` on
            # ``detect_answering_machine`` and ``transcribe``.) A callable knob
            # the port genuinely lacks stays missing and keeps reporting drift,
            # which is the honest result.
            unfold = [rp for rp in target if not _is_callable_type(rp.get("type", ""))]
        else:
            continue
        if not unfold:
            continue
        # Emit each unfolded knob in the reference's OWN kind/type/default. The
        # carrier genuinely offers exactly that: settable by name, in any order,
        # any subset, defaulting to the reference's default when unset. Copying
        # the reference's kind (rather than forcing ``keyword``) keeps the
        # keyword-only vs positional-or-keyword distinction the oracle draws.
        replacement = []
        for rp in unfold:
            np: dict = {"name": rp["name"]}
            if rp.get("kind"):
                np["kind"] = rp["kind"]
            np["type"] = rp.get("type", "any")
            np["required"] = False
            np["default"] = rp.get("default")
            replacement.append(np)
        tail = port_params[idx + 1 :]
        # A port param the reference declares KEYWORD-ONLY carries the same kind
        # as its unfolded siblings. C++ has no keyword arguments at all — every
        # parameter is positional — so on a method whose carrier is being
        # unfolded, a param that is separately declared purely because its API
        # name differs from its wire key (``bind_digit(bind_params)``,
        # ``amazon_bedrock(ai_params)``) is reachable by name exactly the way the
        # bag's keys are. Recording it ``positional`` next to keyword siblings
        # the same fold just emitted would report the carrier's own idiom as
        # drift on one param and not the others.
        ref_kind = {rp["name"]: rp.get("kind") for rp in ref_params if rp.get("name")}
        for i, tp in enumerate(tail):
            if ref_kind.get(tp.get("name")) == "keyword" and not tp.get("kind"):
                # Rebuild so ``kind`` lands in its usual slot (right after
                # ``name``) — the artifact is committed and reviewed, so a
                # param record's key order stays uniform across the file.
                rebuilt = {"name": tp["name"], "kind": "keyword"}
                rebuilt.update({k: v for k, v in tp.items() if k != "name"})
                tail[i] = rebuilt
        # ORDER the unfolded knobs (and any optional port params that follow the
        # carrier) by the REFERENCE's declaration order. A carrier is unordered
        # by nature — a JSON bag and an aggregate initializer both let the caller
        # set any subset in any order — but the diff matches params by POSITION,
        # so an arbitrary order manufactures param-mismatch findings against the
        # neighbouring names. (Measured: ``bind_digit``'s explicit
        # ``bind_params``, declared after the bag so the existing call shape
        # keeps its meaning, otherwise collided with ``realm``/``max_triggers``
        # and reported three bogus type mismatches.) Port params the reference
        # does not name keep their relative order at the end — they are the
        # genuine extras and must stay visible as such.
        ref_order = {rp["name"]: i for i, rp in enumerate(ref_params) if rp.get("name")}
        merged = replacement + [tp for tp in tail if tp.get("required") is False]
        rest = [tp for tp in tail if tp.get("required") is not False]
        merged.sort(key=lambda x: ref_order.get(x.get("name"), len(ref_order)))
        sig["params"] = port_params[:idx] + merged + rest
        return


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


def _oracle_records_class(ns_str: str, class_name: str, header_path: str) -> bool:
    """True when the reference oracle records ``class_name`` in the Python module
    this C++ class maps to — the gate that lets a fields-only POD into the
    signature inventory.

    Resolves the module the SAME way the emit loop does (CLASS_RENAME_MAP, then
    CLASS_MODULE_MAP / module_for_class) so the answer cannot disagree with where
    the class would actually land.

    GENERATED DTOs ARE EXCLUDED BY PATH, and that exclusion is load-bearing rather
    than cosmetic. ``CLASS_MODULE_MAP`` is keyed by bare class NAME, so it is
    namespace-blind: the generated read-side DTO ``signalwire::rest::…::messages::
    Message`` and the generated SWML verb ``…::DataMap`` resolve to the SAME
    canonical key as the hand-written ``signalwire.relay.message.Message`` /
    ``signalwire.core.data_map.DataMap``. Admitting a DTO therefore does not add a
    class — it MERGES its wire fields into the hand-written class's construction
    contract (measured: +13 bogus construction params on Message, and DataMap lost
    its real ``function_name``). The generated DTOs already reach the audit through
    their own generated-payload path; they must not enter here."""
    if "/generated/" in header_path or "_generated/" in header_path:
        return False
    rename_key = (ns_str, class_name)
    if rename_key in CLASS_RENAME_MAP:
        mod, cls = CLASS_RENAME_MAP[rename_key]
    else:
        cls = class_name
        mod = CLASS_MODULE_MAP.get(class_name) or module_for_class(class_name, ns_str)
    if not mod:
        return False
    ref = _load_python_signatures()
    return cls in ref.get("modules", {}).get(mod, {}).get("classes", {})


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
        for fn in entry.get("functions") or {}:
            targets.add((mod, fn))
    return targets


def _translate_with_canonical_fallback(
    spelling: str, canonical_spelling: str, aliases: dict, ctx: str
) -> str:
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
    if (
        canonical_spelling
        and canonical_spelling != spelling
        and primary.startswith("class:")
        and "." in primary
    ):
        # Look up the typedef name in CLASS_MODULE_MAP / CLASS_RENAME_MAP
        # to see if there's an intentional class-rename target. If so,
        # keep the primary translation; otherwise prefer canonical.
        tail = primary.split(":", 1)[1]
        cls_name = tail.rsplit(".", 1)[-1]
        if (
            cls_name not in CLASS_MODULE_MAP
            and not any(v[1] == cls_name for v in CLASS_RENAME_MAP.values())
            and cls_name not in CALLBACK_TYPEDEFS_AS_CALLABLE
        ):
            try:
                return translate_cpp_type(canonical_spelling, aliases, ctx)
            except TypeTranslationError:
                # Fall through and return whatever the spelling produced
                pass
    return primary


def build_signature(
    method: dict,
    aliases: dict,
    context: str,
    *,
    guards: GuardIndex | None = None,
    cpp_class: str | None = None,
    cpp_method: str | None = None,
) -> dict:
    params_out: list = []
    is_static = method.get("is_static", False)
    is_ctor = method.get("is_constructor", False)
    if not is_static:
        params_out.append({"name": "self", "kind": "self"})
    for p_index, p in enumerate(method.get("parameters", [])):
        ctx = f"{context}[{p.get('name')}]"
        canon_type = _translate_with_canonical_fallback(
            p.get("type", ""),
            p.get("canonical_type", ""),
            aliases,
            ctx,
        )
        param: dict = {
            "name": p.get("name", "_") or "_",
            "type": canon_type,
        }
        if p.get("has_default"):
            param["required"] = False
            # C++ declares real default arguments, so the VALUE is recoverable
            # from the header (unlike languages whose reflection only reports
            # that a default exists). A default we could not reduce to a literal
            # — an enum value, a constructor call, an arithmetic expression —
            # stays ``null``: a documented blind spot, never a guessed value.
            dv = p.get("default_value", _NO_DEFAULT)
            if dv is _EMPTY_BRACE:
                dv = _empty_brace_default(canon_type)
            dv = None if dv is _NO_DEFAULT else dv
            # THE NULL <-> ZERO-VALUE SENTINEL FOLD (see GuardIndex). A sentinel
            # default whose absence the body PROVES with a guard is the C++
            # spelling of the reference's ``= None``; record it as null so the
            # two compare equal. An unguarded sentinel is a value the port really
            # ships and keeps reporting as drift.
            if (
                dv is not None
                and guards is not None
                and cpp_class
                and cpp_method
                and _is_foldable_sentinel(canon_type, dv)
            ):
                kind = _sentinel_kind(canon_type)
                if kind and guards.guards(
                    cpp_class, cpp_method, p.get("name") or "", p_index, kind
                ):
                    dv = None
            param["default"] = dv
        else:
            param["required"] = True
        params_out.append(param)
    return_canon = (
        "void"
        if is_ctor
        else _translate_with_canonical_fallback(
            method.get("return_type", "void"),
            method.get("canonical_return_type", ""),
            aliases,
            context + "[->]",
        )
    )
    return {"params": params_out, "returns": return_canon}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--include", type=Path, default=PORT_ROOT / "include")
    parser.add_argument("--out", type=Path, default=PORT_ROOT / "port_signatures.json")
    # FAIL-LOUD BY DEFAULT (2026-07-30). This used to be an opt-in
    # `--strict` that NOT ONE of the six gate invocations passed — run-ci.sh,
    # porting-sdk/scripts/suites/_signatures_fresh.py:156/163 and
    # _surface_commands.py:448/481/535/562/589/635 all call this script bare. So
    # the fail-loud path was dead code, and a translation failure printed a
    # warning and STILL EXITED 0 while the affected symbol vanished from
    # port_signatures.json entirely. Measured on this repo before the fix: 5
    # failures at rc=0, with ContextBuilder.attach_tool_name_supplier and
    # SWMLService.generate_random_hex silently absent despite being declared.
    #
    # BooleanOptionalAction with default=True means the six existing callers need
    # no change — they simply start failing loud. `--no-strict` is the local-only
    # escape hatch for deliberately inspecting a partial artifact. `--strict`
    # still parses, so the documented invocation keeps working.
    parser.add_argument(
        "--strict",
        action=argparse.BooleanOptionalAction,
        default=True,
        help="fail (exit 1) on any type-translation failure instead of silently "
        "emitting an artifact that omits the affected symbols (default: enabled)",
    )
    args = parser.parse_args()

    aliases = load_aliases()

    index = Index.create()
    headers = sorted(args.include.rglob("*.hpp")) + sorted(args.include.rglob("*.h"))
    raw_entries: list[dict] = []
    raw_free_functions: list[dict] = []
    raw_options_structs: dict[str, list[dict]] = {}
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
        cls_entries, fn_entries, opt_structs = walk_translation_unit(tu, args.include)
        if cls_entries:
            raw_entries.extend(cls_entries)
            raw_free_functions.extend(fn_entries)
            raw_options_structs.update(opt_structs)
            single_tu_ok = True
    except Exception as e:
        print(
            f"enumerate_signatures: single-TU parse failed ({e}); "
            f"falling back to per-header",
            file=sys.stderr,
        )

    if not single_tu_ok:
        raw_entries.clear()
        raw_free_functions.clear()
        raw_options_structs.clear()
        for header in headers:
            try:
                tu = index.parse(str(header), args=parse_args, options=_parse_opts)
            except Exception as e:
                print(f"skip {header}: {e}", file=sys.stderr)
                continue
            cls_entries, fn_entries, opt_structs = walk_translation_unit(
                tu, args.include
            )
            raw_entries.extend(cls_entries)
            raw_free_functions.extend(fn_entries)
            raw_options_structs.update(opt_structs)

    # Body index for the null <-> zero-value sentinel fold. Built from the
    # implementation tree + the headers (inline bodies); see GuardIndex.
    guards = GuardIndex([PORT_ROOT / "src", args.include])

    canonical, failures = collect(
        raw_entries, aliases, raw_free_functions, raw_options_structs, guards=guards
    )
    if failures:
        print(
            f"enumerate_signatures: {len(failures)} translation failure(s)",
            file=sys.stderr,
        )
        for f in failures[:30]:
            print(f"  - {f}", file=sys.stderr)
        if len(failures) > 30:
            print(f"  ... ({len(failures) - 30} more)", file=sys.stderr)
        if args.strict:
            print(
                "enumerate_signatures: REFUSING to write a signature artifact that "
                "silently OMITS the symbols above. A failed translation drops that "
                "method from port_signatures.json entirely, so the SIGNATURES / DRIFT "
                "gates would compare against a surface this port does not actually "
                "have. Add the type to porting-sdk/type_aliases.yaml under aliases.cpp "
                "if it is real vocabulary, or make the member non-public if it is an "
                "internal seam. --no-strict emits the partial artifact anyway (local "
                "inspection only; no gate should ever pass it).",
                file=sys.stderr,
            )
            return 1

    args.out.write_text(
        json.dumps(canonical, indent=2, sort_keys=False) + "\n", encoding="utf-8"
    )
    n_mods = len(canonical["modules"])
    n_methods = sum(
        sum(len(c["methods"]) for c in m.get("classes", {}).values())
        for m in canonical["modules"].values()
    )
    print(
        f"enumerate_signatures: wrote {args.out} ({n_mods} modules, {n_methods} methods)"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
