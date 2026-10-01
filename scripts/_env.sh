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

# --- memory-aware build parallelism -------------------------------------------
# sw_build_jobs prints the -j value for cmake --build. NEVER use a bare -j:
# with the Makefile generator that means UNLIMITED parallel jobs, and this
# repo's test unity TU (tests/test_main.cpp, ~115 included test .cpp files)
# peaks at ~2.1 GB compiler RSS (measured clang arm64, default flags; each
# mocktest TU is ~0.75 GB). Unlimited jobs on a 4-core/16 GB CI runner stacks
# the unity TU with ~40 library/tool TUs at once and the OOM killer takes out
# the runner agent — 4/4 cross-port-matrix cpp jobs died with exit 143 /
# "runner received a shutdown signal" after feat/rest-generated grew the unity
# TU (2026-07-08).
#
#   jobs = min(ncpu, available_RAM / SW_BUILD_MEM_PER_JOB_MB), floor 1
#
# SW_BUILD_MEM_PER_JOB_MB defaults to 2560 (worst measured TU + headroom).
# SW_BUILD_JOBS overrides the whole computation (CI tuning / testing the cap).
# Linux uses MemAvailable (the guard that matters on CI runners); macOS uses
# total hw.memsize — dev boxes are RAM-rich, so the cap is a no-op there.
sw_build_jobs() {
    if [ -n "${SW_BUILD_JOBS:-}" ]; then
        echo "$SW_BUILD_JOBS"
        return 0
    fi
    local ncpu mem_kb per_job_kb jobs
    per_job_kb=$(( ${SW_BUILD_MEM_PER_JOB_MB:-2560} * 1024 ))
    if [ "$(uname -s)" = "Darwin" ]; then
        ncpu="$(sysctl -n hw.ncpu)"
        mem_kb=$(( $(sysctl -n hw.memsize) / 1024 ))
    else
        ncpu="$(nproc)"
        mem_kb="$(awk '/MemAvailable/{print $2; exit}' /proc/meminfo 2>/dev/null || true)"
        [ -n "$mem_kb" ] || mem_kb="$(awk '/MemTotal/{print $2; exit}' /proc/meminfo)"
    fi
    jobs=$(( mem_kb / per_job_kb ))
    [ "$jobs" -ge 1 ] || jobs=1
    [ "$jobs" -le "$ncpu" ] || jobs="$ncpu"
    echo "$jobs"
}

# --- ccache (optional compiler cache) ----------------------------------------
# ccache is an OPTIONAL speedup: CMakeLists.txt availability-gates it as the C/C++
# compiler launcher (find_program(CCACHE_PROGRAM ccache) — a strict no-op when
# absent, so the build never fails for a missing ccache). We only HINT here when
# it isn't installed; we do NOT fail, because its absence must not break a build.
# Declared so it's present when wanted, in BOTH CI layers per AGENT_RULES §7:
# porting-sdk's cross-port.yml cpp matrix install step AND this repo's own
# .github/workflows/{test,nightly}.yml (which are what actually run PR + nightly
# CI). Only cross-port.yml declared it until 2026-08-05, so every test.yml and
# nightly.yml runner printed the hint below and built fully cold — both workflows
# now apt-install ccache and persist ~/.cache/ccache across runs.
if ! command -v ccache >/dev/null 2>&1; then
    echo "note: ccache not found — C++ rebuilds will be uncached (optional)." >&2
    echo "      Install it for near-instant warm rebuilds:  brew install ccache" >&2
fi

# Make ccache PATH-INSENSITIVE, for LOCAL runs and CI alike.
#
# ccache's fast "direct" mode keys on the absolute path of the translation unit,
# so a build at a new path misses direct and falls back to the slower
# preprocessed mode. PACKAGE-SMOKE builds in a PID-UNIQUE sandbox
# (porting-sdk package_smoke.py: .sw-tmp/package-smoke-cpp-<pid>), so its path
# differs on EVERY run BY CONSTRUCTION and direct mode could otherwise never hit.
# base_dir rewrites absolute paths beneath it to relative BEFORE hashing, and
# hash_dir=false keeps the cwd out of the hash — together they take the PID out of
# the cache SIGNATURE while leaving the sandbox's isolation untouched (that PID is
# load-bearing: package_smoke.py rm -rf's its own subtree, and a shared name would
# let concurrent runs delete each other's).
#
# Set HERE rather than only in the workflows so local and CI behave IDENTICALLY:
# this file is CWD-independent, whereas $GITHUB_WORKSPACE exists only on a runner —
# keying off that alone would silently give local devs preprocessed-only hits while
# CI got direct hits, i.e. the two environments would disagree about the cache.
#
# CHOOSING THE BASEDIR — DETECTED, NOT ASSUMED. base_dir only rewrites paths that
# live BENEATH it; anything outside stays absolute and misses direct mode. Using
# $REPO would bake in "every build dir is inside the port repo", which is an
# assumption about layout rather than a fact: gates scatter their scratch around
# (package_smoke.py builds in <repo>/.sw-tmp/package-smoke-cpp-<pid>, but
# ca_var_parity.py roots its scratch under the PORTING-SDK checkout instead), and
# the two checkouts sit side by side in both environments (~/src/{signalwire-cpp,
# porting-sdk} locally, $GITHUB_WORKSPACE/{signalwire-cpp,porting-sdk} on a runner).
# So derive the basedir as the deepest COMMON ANCESTOR of this repo and the
# porting-sdk checkout when we can see both, which covers scratch under either one,
# and fall back to $REPO when we cannot. Nothing here hardcodes a directory name or
# a nesting depth.
#
# Existing values always win, so a caller can override.
if [ -z "${CCACHE_BASEDIR:-}" ]; then
    _cc_base="$REPO"
    # Locate the porting-sdk checkout without assuming where it is: honour an
    # explicit $PORTING_SDK, else look for a sibling of the repo.
    _cc_psdk="${PORTING_SDK:-}"
    if [ -z "$_cc_psdk" ] && [ -d "$(dirname "$REPO")/porting-sdk" ]; then
        _cc_psdk="$(dirname "$REPO")/porting-sdk"
    fi
    if [ -n "$_cc_psdk" ] && [ -d "$_cc_psdk" ]; then
        # Deepest common ancestor of $REPO and $_cc_psdk, computed by walking up.
        _cc_a="$(cd "$REPO" && pwd -P)"
        _cc_b="$(cd "$_cc_psdk" && pwd -P)"
        while [ -n "$_cc_a" ] && [ "$_cc_a" != "/" ]; do
            case "$_cc_b/" in
                "$_cc_a"/*) _cc_base="$_cc_a"; break ;;
            esac
            _cc_a="$(dirname "$_cc_a")"
        done
        unset _cc_a _cc_b
    fi
    export CCACHE_BASEDIR="$_cc_base"
    unset _cc_base _cc_psdk
fi
export CCACHE_NOHASHDIR="${CCACHE_NOHASHDIR:-1}"

# Measured (full Release build of the library, 130 TUs):
#   cold at path A                   158.4s
#   rebuild at a DIFFERENT path B      1.5s   130/130 DIRECT hits
# versus 32.0s / 89-of-260 preprocessed-only hits without these.
#
# Correctness negative-controlled in all three directions: a changed source
# recompiles, a changed HEADER recompiles, and a build from a different cwd
# reuses the right object with __FILE__ intact — no false hits.

# --- ctcache (clang-tidy result cache) ---------------------------------------
# run-lint.sh routes clang-tidy through scripts/clang_tidy_cache.py ONLY when
# $CTCACHE_DIR is set (else: plain clang-tidy, exact prior behaviour). That var
# was exported by the CI workflows and nowhere else, so CI got the cache and a
# LOCAL `run-lint.sh` / `run-ci.sh` always re-ran clang-tidy from scratch — the
# same local-vs-CI asymmetry the ccache block above exists to prevent. Measured on
# this machine: a full local LINT is ~378s uncached, while CI's cached LINT is
# 14-27s. Default it here so the two environments cache alike; the CI workflows
# still export their own value (pointing at the dir actions/cache persists), and
# an explicit value always wins.
#
# XDG_CACHE_HOME is honoured when set, else ~/.cache — matching where the CI
# workflow puts it and where ccache defaults, rather than inventing a new dir.
# ctcache does NOT expanduser its value, so this must be an ABSOLUTE path.
export CTCACHE_DIR="${CTCACHE_DIR:-${XDG_CACHE_HOME:-$HOME/.cache}/ctcache}"

# ruff — the PY-LINT gate's linter/formatter for the hand-written Python under
# scripts/. A HINT here rather than a hard failure, because the C++ build/test
# path does not need it; the gate itself (scripts/run-pylint.sh) fails loud with
# the same hint when it is actually required. Declared in BOTH layers per
# AGENT_RULES §7 — here for local devs, and as a pip install in the CI workflow
# next to the pinned clang-format — so a fresh clone or a CI runner has it.
#
# PINNED EXACT, for the same reason clang-format is pinned to 18 above: an
# unbounded linter version is a green-locally/red-in-CI generator. CI installs the
# newest release at run time while a local dev runs whatever they installed months
# ago, so a ruff release that adds a rule or changes a format heuristic reds
# PY-LINT on code that never changed. run-pylint.sh ASSERTS this version, exactly
# as this file asserts clang-format major 18. Keep in lockstep with the
# `pip install "ruff==…"` in .github/workflows/{test,nightly}.yml; 0.15.21 is the
# fleet-wide ruff (python/perl/php/typescript/java pin the same).
SW_RUFF_VERSION="0.15.21"
export SW_RUFF_VERSION

if ! command -v ruff >/dev/null 2>&1; then
    echo "note: ruff not found — the PY-LINT gate (scripts/*.py) cannot run." >&2
    echo "      Install it with:  pip install ruff==$SW_RUFF_VERSION" >&2
fi
