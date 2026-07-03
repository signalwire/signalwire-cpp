#!/usr/bin/env bash
# _env.sh — shared self-bootstrap for the canonical lint/format/test scripts
# (run-format.sh, run-lint.sh, run-tests.sh) AND run-ci.sh for signalwire-cpp.
#
# Source this — do NOT execute it — from the top of each script:
#     source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/_env.sh"
#
# It is CWD-independent: it resolves the repo root from THIS file's own path,
# not from $PWD, so the callers work from any directory.
#
# The C++ tool environment it bootstraps:
#   * clang-format / clang-tidy — these are the LLVM 18 (clang-18) binaries at
#     /opt/homebrew/opt/llvm@18/bin, NOT the macOS default toolchain. The default
#     `clang-format` on a Mac (Apple's, or a newer Homebrew llvm) formats
#     DIFFERENTLY from clang-18, so the FMT gate (and the generators, which shell
#     to clang-format via scripts/_cpp_fmt.py) would disagree byte-for-byte with
#     what was committed. We therefore PREPEND the llvm@18 bin to PATH so every
#     downstream `clang-format` / `clang-tidy` — in these scripts, in run-ci's FMT
#     gate, and in the Python generators — resolves to clang-18. If llvm@18 is
#     absent we fall back to a clang-format/clang-tidy already on PATH, and fail
#     LOUD (below) if the resolved clang-format is not version 18.
#
# Exposes: $REPO (repo root). Fails loud with a clear hint if clang-format 18
# cannot be resolved.

set -euo pipefail

# Resolve repo root from this script's own location (scripts/ is directly under
# the repo root). Independent of the caller's CWD.
_ENV_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(dirname "$_ENV_DIR")"
export REPO

# --- clang-18 (llvm@18) on PATH ----------------------------------------------
# Prepend the llvm@18 bin so clang-format / clang-tidy resolve to clang-18 no
# matter what the caller's shell had on PATH. If the dir is absent, we do
# nothing here and rely on whatever clang-format/clang-tidy is already on PATH
# (validated to be v18 just below).
_LLVM18_BIN="/opt/homebrew/opt/llvm@18/bin"
if [ -d "$_LLVM18_BIN" ]; then
    export PATH="$_LLVM18_BIN:$PATH"
fi

# --- fail loud unless clang-format 18 is resolvable --------------------------
if ! command -v clang-format >/dev/null 2>&1; then
    echo "FATAL: clang-format not found on PATH." >&2
    echo "       The FMT gate + the REST/type generators need clang-18's" >&2
    echo "       clang-format. Install it with:  brew install llvm@18" >&2
    echo "       (it lands at $_LLVM18_BIN/clang-format)." >&2
    exit 1
fi
_CF_VERSION="$(clang-format --version 2>/dev/null | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1)"
_CF_MAJOR="${_CF_VERSION%%.*}"
if [ "${_CF_MAJOR:-0}" != "18" ]; then
    echo "FATAL: clang-format on PATH is version '${_CF_VERSION:-unknown}', not 18." >&2
    echo "       This repo's .clang-format output + the generated headers are" >&2
    echo "       pinned to clang-format 18 (a different major reformats" >&2
    echo "       differently and breaks the FMT / GEN-FRESH gates)." >&2
    echo "       Install it with:  brew install llvm@18" >&2
    echo "       (it lands at $_LLVM18_BIN/clang-format). Override the search" >&2
    echo "       by putting a clang-format 18 earlier on PATH." >&2
    exit 1
fi
