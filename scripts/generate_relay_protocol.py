#!/usr/bin/env python3
"""Generate the RELAY-protocol wire-type surface for signalwire-cpp.

The C++ realization of SESSION_CHANGESET_FOR_PORTS.md item I/D — the
``signalwire.relay.protocol_types_generated`` module — mirroring python's
``generate_relay_protocol`` and ruby's ``generate_relay_protocol.py``.

Source: the canonical porting-sdk ``relay-protocol/*.json`` — one standalone
JSON-Schema file per RELAY WS method+phase, named
``<domain>.<method>.(params|result).json``. NOT derived from openapi.

Class name = PascalCase(``x-method``, fallback filename base) + phase suffix:
  calling.ai_hold.params.json  -> CallingAiHoldParams
  signalwire.connect.result.json -> SignalwireConnectResult

Emit/drop rule = the shared ``is_object_schema`` test: an OBJECT schema WITH
properties -> a method-less C++ data struct; empty-object / scalar / union
placeholder -> NOT surfaced. 126 params/result files - 3 empty-object
placeholders = 123 == the oracle exactly (0/0).

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
import json
import sys
from pathlib import Path


def _load_rest_generator():
    here = Path(__file__).resolve().parent
    spec = importlib.util.spec_from_file_location("generate_rest", here / "generate_rest.py")
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


def build_outputs(psdk: Path) -> dict:
    relay_dir = psdk / "relay-protocol"
    by_name = {p.name: p for p in relay_dir.glob("*.json")}
    outs: dict = {}
    emitted_names: set = set()

    for phase, suffix in _PHASES:
        tail = "." + phase + ".json"
        for name in sorted(n for n in by_name if n.endswith(tail)):
            node = json.loads(by_name[name].read_text())
            if not isinstance(node, dict):
                continue
            method = node.get("x-method") or name[: -len(tail)]
            struct = GR.type_name(_pascal_method(method) + suffix)
            if not GR.is_object_schema(node):
                continue
            if struct in emitted_names:
                continue
            emitted_names.add(struct)
            fn = "/".join(RELAY_SUBDIR) + f"/{GR.snake(struct)}.hpp"
            outs[fn] = GR.emit_methodless_struct(
                RELAY_NS, struct, node.get("properties") or {},
                f"RELAY method {method!r}, {phase}.", "generate_relay_protocol.py",
            )

    return outs


def main(argv: list) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--check", action="store_true", help="GEN-FRESH: exit non-zero if stale")
    ap.add_argument("--out", default="", help="scratch: emit into this dir")
    args = ap.parse_args(argv)

    psdk = resolve_porting_sdk()
    outs = build_outputs(psdk)
    # Only C++ headers are formatted; any .json sidecars are emitted verbatim.
    outs = {fn: (format_generated_cpp(src) if fn.endswith((".hpp", ".h")) else src)
            for fn, src in outs.items()}

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
            sys.stderr.write("GEN-FRESH FAIL: %d generated RELAY-protocol file(s) stale:\n" % len(stale))
            for s in stale:
                sys.stderr.write("  - %s\n" % s)
            return 1
        print("GEN-FRESH: generated RELAY-protocol files match porting-sdk/relay-protocol/.")
        return 0

    for fn, src in outs.items():
        p = out_dir / fn
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(src)
    print(f"generated {len(outs)} RELAY-protocol file(s) into {out_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
