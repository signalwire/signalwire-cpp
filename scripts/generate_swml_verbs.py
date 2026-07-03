#!/usr/bin/env python3
"""Generate the typed SWML-verbs CONFIG surface for signalwire-cpp.

The C++ realization of SESSION_CHANGESET_FOR_PORTS.md item D2 — the
``signalwire.core.swml_verbs_generated`` module — mirroring python's
``swml_verbs_generated.py`` and ruby's ``generate_swml_verbs.py``.

Source: the CANONICAL porting-sdk ``schema.json`` ``$defs``. Emits the 155
method-less SWML config types the Python SURFACE oracle records (the reference's
``_SwmlVerbs`` verb-METHOD protocol is ``_``-prefixed and NOT part of the
cross-port surface oracle, so only the CONFIG type surface is emitted):

  1. One method-less C++ data struct per ``$defs`` OBJECT schema (133).
  2. One ``<Verb>Config`` struct per SWMLMethod.anyOf verb whose inner schema
     is an inline object / oneOf union (22) — the flattened UNION of the verb's
     variant properties. Hand-written verbs (answer/hangup/ai/play/say) are
     excluded from the Config flatten, matching go/php/ruby.

  133 + 22 = 155 == the oracle exactly (0 missing / 0 extra).

Output: one struct per file under
  include/signalwire/core/swml_verbs_generated/<snake_name>.hpp
in namespace ``signalwire::core::swml_verbs_generated``. The surface / signature
enumerators route every file under that namespace prefix to the oracle module
``signalwire.core.swml_verbs_generated`` BY PATH (not class name), so a type name
that also exists as a REST wire type lands in the right module; the SURFACE-DIFF
gen-type leaf fold then collapses the cross-module duplicates on both sides.

Usage:
    python3 scripts/generate_swml_verbs.py            # write into the repo tree
    python3 scripts/generate_swml_verbs.py --check    # GEN-FRESH: fail if stale
    python3 scripts/generate_swml_verbs.py --out DIR  # scratch: emit into DIR
"""
from __future__ import annotations

import argparse
import importlib.util
import json
import re
import sys
from pathlib import Path


def _load_rest_generator():
    here = Path(__file__).resolve().parent
    spec = importlib.util.spec_from_file_location("generate_rest", here / "generate_rest.py")
    if spec is None or spec.loader is None:  # pragma: no cover
        raise SystemExit("generate_swml_verbs.py: cannot load generate_rest.py")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


GR = _load_rest_generator()

sys.path.insert(0, str(Path(__file__).resolve().parent))
from _cpp_fmt import clang_format_source  # noqa: E402

SWML_VERBS_NS = ["signalwire", "core", "swml_verbs_generated"]
SWML_VERBS_SUBDIR = ["core", "swml_verbs_generated"]
HAND_WRITTEN_VERBS = {"answer", "hangup", "ai", "play", "say"}


def resolve_porting_sdk() -> Path:
    return GR.resolve_porting_sdk()


def repo_root() -> Path:
    return Path(__file__).resolve().parents[1]


def _load_defs(psdk: Path) -> dict:
    doc = json.loads((psdk / "schema.json").read_text())
    defs = doc.get("$defs")
    if not defs:
        raise SystemExit("generate_swml_verbs.py: schema.json has no $defs")
    return defs


def _ref_leaf(ref: str) -> str:
    return ref.rsplit("/", 1)[-1] if ref else ref


def _type_str(node: dict):
    t = node.get("type")
    if isinstance(t, list):
        return next((x for x in t if x != "null"), None)
    return t


def _pascal(s: str) -> str:
    parts = re.split(r"[_\-\s.]", s)
    return "".join(w[:1].upper() + w[1:] for w in parts if w)


def _flatten_union(defs: dict, node) -> dict:
    """Return the UNION of properties across allOf/oneOf/anyOf, following $ref.
    First-seen wins (mirrors go's flattenUnion / the reference _flatten_union)."""
    out: dict = {}

    def walk(n) -> None:
        if not n:
            return
        ref = n.get("$ref")
        if ref:
            walk(defs.get(_ref_leaf(ref)))
            return
        for sub in n.get("allOf") or []:
            walk(sub)
        for name, psc in (n.get("properties") or {}).items():
            out.setdefault(name, psc)
        for sub in n.get("oneOf") or []:
            walk(sub)
        for sub in n.get("anyOf") or []:
            walk(sub)

    walk(node)
    return out


def build_outputs(psdk: Path) -> dict:
    defs = _load_defs(psdk)
    outs: dict = {}
    emitted_names: set = set()

    def emit(name: str, props: dict, desc: str) -> None:
        if name in emitted_names:
            return
        emitted_names.add(name)
        fn = "/".join(SWML_VERBS_SUBDIR) + f"/{GR.snake(name)}.hpp"
        outs[fn] = GR.emit_methodless_struct(SWML_VERBS_NS, name, props, desc,
                                             "generate_swml_verbs.py")

    # 1. One data struct per OBJECT $defs schema.
    for raw_name, node in defs.items():
        if not isinstance(node, dict) or not GR.is_object_schema(node):
            continue
        emit(GR.type_name(raw_name), node.get("properties") or {},
             f"schema.json $defs schema {raw_name!r}.")

    # 2. One <Verb>Config struct per flattenable SWMLMethod.anyOf verb.
    sm = defs.get("SWMLMethod")
    if sm:
        for ref in sm.get("anyOf") or []:
            wrapper = _ref_leaf(ref.get("$ref", ""))
            wdef = defs.get(wrapper)
            if not wdef or not (wdef.get("properties") or {}):
                continue
            verb = next(iter(wdef["properties"].keys()))
            if verb in HAND_WRITTEN_VERBS:
                continue
            inner = wdef["properties"][verb]
            if _type_str(inner) == "string" or inner.get("$ref"):
                continue
            has_inline = _type_str(inner) == "object" and bool(inner.get("properties"))
            if not inner.get("oneOf") and not has_inline:
                continue
            props = _flatten_union(defs, inner)
            if not props:
                continue
            emit(GR.type_name(_pascal(verb) + "Config"), props,
                 f"flattened SWMLMethod verb {verb!r} config.")

    return outs


def main(argv: list) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--check", action="store_true", help="GEN-FRESH: exit non-zero if stale")
    ap.add_argument("--out", default="", help="scratch: emit into this dir")
    args = ap.parse_args(argv)

    psdk = resolve_porting_sdk()
    outs = build_outputs(psdk)
    outs = {fn: clang_format_source(src) for fn, src in outs.items()}

    out_dir = Path(args.out) if args.out else repo_root() / "include" / "signalwire"

    if args.check:
        stale: list = []
        for fn, src in outs.items():
            p = out_dir / fn
            if not p.is_file() or p.read_text() != src:
                stale.append(str(p))
        expected = set(outs.keys())
        gen_root = out_dir / "/".join(SWML_VERBS_SUBDIR) if not args.out else out_dir
        if gen_root.is_dir():
            for p in sorted(gen_root.rglob("*.hpp")):
                rel = p.relative_to(out_dir).as_posix()
                if rel not in expected:
                    stale.append(f"{p} (leftover — not in generator output)")
        if stale:
            sys.stderr.write("GEN-FRESH FAIL: %d generated SWML-verb file(s) stale:\n" % len(stale))
            for s in stale:
                sys.stderr.write("  - %s\n" % s)
            return 1
        print("GEN-FRESH: generated SWML-verb files match porting-sdk/schema.json ($defs).")
        return 0

    for fn, src in outs.items():
        p = out_dir / fn
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(src)
    print(f"generated {len(outs)} SWML-verb file(s) into {out_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
