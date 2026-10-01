#!/usr/bin/env python3
"""Generate the RELAY-protocol wire-type surface for signalwire-cpp.

The C++ realization of SESSION_CHANGESET_FOR_PORTS.md item I/D — the
``signalwire.relay.protocol_types_generated`` module — mirroring python's
``generate_relay_protocol`` and ruby's ``generate_relay_protocol.py``.

Source: the canonical porting-sdk ``combined-specs/relay.yaml``, read through the
shared reader ``porting-sdk/scripts/relay_protocol_shapes.py`` (ledger row R11).
That reader serves ``{method: schema_node}`` per phase, merging the shapes carried
on a registered method (``methods.<name>.request.params_dto`` /
``.response.result``) with the six per phase the extractor found for methods the
vendored spec does not register (``<phase>_shapes_unattached.methods.<name>``) —
64 methods per phase either way. NOT derived from openapi.

This replaced a directory of standalone per-method JSON-Schema files
(``relay-protocol/<domain>.<method>.(params|result).json``). The method name now
comes from the document's own key rather than from an ``x-method`` field with a
filename fallback, and the phase from the block it was carried in rather than from
a filename suffix.

Class name = PascalCase(method identifier) + phase suffix:
  calling.ai_hold    (params phase) -> CallingAiHoldParams
  signalwire.connect (result phase) -> SignalwireConnectResult

Emit/drop rule = the shared ``is_object_schema`` test: an OBJECT schema WITH
properties -> a method-less C++ data struct; empty-object / scalar / union
placeholder -> NOT surfaced. 64 params shapes less 2 property-less placeholders
= 62, 64 result less 3 = 61; 62 + 61 = 123 == the oracle exactly (0/0).

(The combined document omits the ``type: object`` the per-file envelope used to
declare; ``is_object_schema``'s ``(type is None and properties)`` branch covers
that, so the emit verdict is unchanged. Pinned by
``porting-sdk/tests/test_relay_protocol_shapes.py``.)

Output: one struct per file under
  include/signalwire/relay/protocol_types_generated/<snake_name>.hpp
in namespace ``signalwire::relay::protocol_types_generated``. The enumerators
route every file under that namespace prefix to the oracle module by PATH
(winning over the name-keyed lookup, so an existing Relay SDK class — Call/Client/
CallState/… — is never misrouted).

Usage:
    python3 scripts/generate_relay_protocol.py            # write into the repo tree
    python3 scripts/generate_relay_protocol.py --check    # GEN-FRESH: fail if stale
    python3 scripts/generate_relay_protocol.py --out DIR  # scratch: emit into DIR
"""

from __future__ import annotations

import argparse
import importlib.util
import re
import sys
from pathlib import Path


def _load_rest_generator():
    here = Path(__file__).resolve().parent
    spec = importlib.util.spec_from_file_location(
        "generate_rest", here / "generate_rest.py"
    )
    if spec is None or spec.loader is None:  # pragma: no cover
        raise SystemExit("generate_relay_protocol.py: cannot load generate_rest.py")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


GR = _load_rest_generator()

sys.path.insert(0, str(Path(__file__).resolve().parent))
from _cpp_fmt import format_generated_cpp  # noqa: E402

RELAY_NS = ["signalwire", "relay", "protocol_types_generated"]
RELAY_SUBDIR = ["relay", "protocol_types_generated"]
_PHASES = (("params", "Params"), ("result", "Result"))


def resolve_porting_sdk() -> Path:
    return GR.resolve_porting_sdk()


def repo_root() -> Path:
    return Path(__file__).resolve().parents[1]


def _pascal_method(method: str) -> str:
    parts = [p for p in re.split(r"[._\-\s]", method) if p]
    return "".join(w[:1].upper() + w[1:] for w in parts)


def _load_relay_shapes(psdk: Path):
    """The shared porting-sdk reader for ``combined-specs/relay.yaml`` (ledger R11).

    Loaded by FILE PATH — the same way this script already loads generate_rest.py —
    because porting-sdk is a sibling checkout, not an installed package.
    """
    path = psdk / "scripts" / "relay_protocol_shapes.py"
    if not path.is_file():
        raise SystemExit(
            f"generate_relay_protocol.py: {path} not found (need porting-sdk adjacency)"
        )
    spec = importlib.util.spec_from_file_location("relay_protocol_shapes", path)
    if spec is None or spec.loader is None:  # pragma: no cover
        raise SystemExit(f"generate_relay_protocol.py: cannot load {path}")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def build_outputs(psdk: Path) -> dict:
    RPS = _load_relay_shapes(psdk)

    outs: dict = {}
    emitted_names: set = set()

    # Params first, then result — each mapping already ordered by method name.
    for phase, suffix in _PHASES:
        for method, node in RPS.shapes(psdk, phase).items():
            struct = GR.type_name(_pascal_method(method) + suffix)
            if not GR.is_object_schema(node):
                continue
            if struct in emitted_names:
                continue
            emitted_names.add(struct)
            fn = "/".join(RELAY_SUBDIR) + f"/{GR.snake(struct)}.hpp"
            outs[fn] = GR.emit_methodless_struct(
                RELAY_NS,
                struct,
                node.get("properties") or {},
                f"RELAY method {method!r}, {phase}.",
                "generate_relay_protocol.py",
            )

    return outs


def main(argv: list) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument(
        "--check", action="store_true", help="GEN-FRESH: exit non-zero if stale"
    )
    ap.add_argument("--out", default="", help="scratch: emit into this dir")
    args = ap.parse_args(argv)

    psdk = resolve_porting_sdk()
    outs = build_outputs(psdk)
    # Only C++ headers are formatted; any .json sidecars are emitted verbatim.
    outs = {
        fn: (format_generated_cpp(src) if fn.endswith((".hpp", ".h")) else src)
        for fn, src in outs.items()
    }

    out_dir = Path(args.out) if args.out else repo_root() / "include" / "signalwire"

    if args.check:
        stale: list = []
        for fn, src in outs.items():
            p = out_dir / fn
            if not p.is_file() or p.read_text() != src:
                stale.append(str(p))
        expected = set(outs.keys())
        gen_root = out_dir / "/".join(RELAY_SUBDIR) if not args.out else out_dir
        if gen_root.is_dir():
            for p in sorted(gen_root.rglob("*.hpp")):
                rel = p.relative_to(out_dir).as_posix()
                if rel not in expected:
                    stale.append(f"{p} (leftover — not in generator output)")
        if stale:
            sys.stderr.write(
                f"GEN-FRESH FAIL: {len(stale)} generated RELAY-protocol file(s) stale:\n"
            )
            for s in stale:
                sys.stderr.write(f"  - {s}\n")
            return 1
        print(
            "GEN-FRESH: generated RELAY-protocol files match porting-sdk/combined-specs/relay.yaml."
        )
        return 0

    for fn, src in outs.items():
        p = out_dir / fn
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(src)
    print(f"generated {len(outs)} RELAY-protocol file(s) into {out_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
