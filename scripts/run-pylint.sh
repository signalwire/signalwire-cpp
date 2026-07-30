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

# PIN THE CONFIG EXPLICITLY. ruff resolves configuration by walking UP from the
# TARGET, not from the CWD and not from the repo root, and when no config is
# found on that walk it falls back to BUILT-IN DEFAULTS — a different ruleset,
# reported as success. That is the vacuity trap this campaign keeps paying for:
# a lint gate that silently checks something other than what you configured.
# Two sibling ports measured real drift from exactly this (one found 0 where the
# real config found 4; another had 7 findings silently change status).
CONFIG="$REPO/ruff.toml"
if [ ! -f "$CONFIG" ]; then
    echo "ERROR: $CONFIG not found." >&2
    echo "       Refusing to run: without it ruff would silently fall back to its" >&2
    echo "       BUILT-IN DEFAULTS and report success against the wrong ruleset." >&2
    exit 1
fi

# Always pass the DIRECTORY, never individual files: ruff's `exclude` governs
# directory traversal, and a path named explicitly on the command line is
# analysed even when excluded. The vendored scripts/clang_tidy_cache.py relies
# on that exclusion, so handing ruff the tree (not the file) is what keeps the
# vendored file out. Verified: `ruff check --config <abs> <abs>/scripts/` and the
# relative form both report 0, from the repo root and from a foreign CWD.
if [ "${1:-}" = "--check" ]; then
    ruff check --config "$CONFIG" scripts/ || exit 1
    ruff format --config "$CONFIG" --check scripts/ || exit 1
else
    ruff check --config "$CONFIG" --fix scripts/ || exit 1
    ruff format --config "$CONFIG" scripts/ || exit 1
    # A residual finding --fix cannot resolve must still fail the gate.
    ruff check --config "$CONFIG" scripts/ || exit 1
    ruff format --config "$CONFIG" --check scripts/ || exit 1
fi
