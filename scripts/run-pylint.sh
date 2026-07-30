#!/usr/bin/env bash
# run-pylint.sh — CANONICAL Python linter/formatter for signalwire-cpp (ruff).
#
# This is a C++ SDK, but it carries 9 hand-written Python files under scripts/
# (~10.4k lines), and until 2026-07-30 no gate linted or formatted ANY of them.
# Two are load-bearing lint/format infrastructure themselves — _cpp_fmt.py (the
# REST/type/verb generators shell to it to format their emitted C++) and
# clang_tidy_cache.py (the wrapper the LINT gate routes every clang-tidy call
# through) — so the tooling that enforced the bar was itself below any bar.
#
# Modes (mirroring scripts/run-format.sh so the two behave identically):
#   (default)  APPLY  — `ruff check --fix` + `ruff format`: fix in place.
#   --check    VERIFY — `ruff check` + `ruff format --check`: read-only, exits
#                       non-zero on any finding. This is the CI mode.
#
# Config lives in ruff.toml at the repo root and MIRRORS the reference
# implementation's rule selection (signalwire-python/pyproject.toml) so the
# fleet stays consistent. The single excluded file, clang_tidy_cache.py, is
# VENDORED third-party code (matus-chochlik/ctcache at a pinned SHA) — the same
# category as deps/ on the C++ side, and excluded for the same reason.

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/_env.sh"

cd "$REPO"

if ! command -v ruff >/dev/null 2>&1; then
    echo "ERROR: ruff not found on PATH." >&2
    echo "       It lints + formats the Python under scripts/." >&2
    echo "       Install it with:  pip install ruff   (or: brew install ruff)" >&2
    exit 1
fi

# Fail loud rather than silently passing on an empty file set — a gate that
# checks nothing is worse than no gate.
if ! find scripts -name '*.py' | grep -q .; then
    echo "no Python sources found to lint" >&2; exit 1
fi

if [ "${1:-}" = "--check" ]; then
    ruff check scripts/ || exit 1
    ruff format --check scripts/ || exit 1
else
    ruff check --fix scripts/ || exit 1
    ruff format scripts/ || exit 1
    # A residual finding --fix cannot resolve must still fail the gate.
    ruff check scripts/ || exit 1
    ruff format --check scripts/ || exit 1
fi
