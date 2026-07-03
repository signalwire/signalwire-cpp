#!/usr/bin/env python3
"""Shared clang-format post-processing for the C++ code generators.

Every generator (generate_rest, generate_swml_verbs, generate_relay_protocol,
generate_swaig_payloads) runs its emitted source through ``clang-format`` as its
final step, so a fresh regen equals the formatted on-disk tree. Without this,
GEN-FRESH (byte-compares a fresh regen to the tree) and the FMT gate
(clang-format --dry-run -Werror) are mutually exclusive — the generator would
emit unformatted text and one gate would always be red. Formatting inside the
generator makes both pass by construction. (porting-sdk AGENT_RULES §5.)

The clang-format binary + the repo's .clang-format config are the same ones the
FMT gate uses, so the formatting is identical on both sides.
"""
from __future__ import annotations

import shutil
import subprocess
import sys
from pathlib import Path


def _repo_root() -> Path:
    # scripts/ is directly under the repo root.
    return Path(__file__).resolve().parent.parent


def clang_format_source(src: str, *, assume_filename: str = "x.hpp") -> str:
    """Return ``src`` formatted with the repo's clang-format config.

    Uses ``-assume-filename`` so clang-format applies the repo's .clang-format
    (found by walking up from the repo root) and picks C++ formatting. Raises if
    clang-format is not installed — an unformatted generator output would break
    the FMT/GEN-FRESH reconciliation, so failing loud is correct.
    """
    exe = shutil.which("clang-format")
    if exe is None:
        sys.stderr.write(
            "clang-format not found on PATH — the C++ generators must format their "
            "output (else GEN-FRESH and the FMT gate conflict). Install clang-format.\n"
        )
        raise SystemExit(2)
    proc = subprocess.run(
        [exe, f"-assume-filename={_repo_root() / assume_filename}"],
        input=src,
        capture_output=True,
        text=True,
    )
    if proc.returncode != 0:
        sys.stderr.write(f"clang-format failed:\n{proc.stderr}\n")
        raise SystemExit(proc.returncode)
    return proc.stdout
