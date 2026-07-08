#!/usr/bin/env bash
# run-lint.sh — CANONICAL linter for signalwire-cpp (clang-tidy, curated set).
#
# This is the single entry point for linting. Do NOT call clang-tidy directly;
# run-ci, agents, and humans all go through this script. It self-bootstraps its
# tool environment (via scripts/_env.sh) and runs from ANY directory.
#
# Runs the curated clang-tidy check set (config in .clang-tidy, burned to ZERO
# findings; WarningsAsErrors:'*' makes any finding a nonzero exit). The curated
# set polices real bug patterns (bugprone-*, performance-*, select readability-*)
# and NEVER idiom. Scope is first-party src/+include/ only; vendored deps/ are
# excluded by the HeaderFilterRegex in .clang-tidy. Exits non-zero on any finding.
#
# clang-tidy is report-only (no autofix wired here); it takes no --fix.
#
# clang-tidy version note: unlike clang-FORMAT (pinned to clang-18 by _env.sh
# because its output must match the committed/generated tree byte-for-byte),
# clang-tidy must be new enough to PARSE the build's standard-library headers. On
# macOS the system SDK's libc++ can outrun clang-tidy 18 (e.g. clang-tidy 18
# cannot parse a macOS-26 SDK's __builtin_clzg), so this gate (a) picks the
# NEWEST clang-tidy it can find unless $SWCPP_CLANG_TIDY overrides, and (b)
# generates a dedicated `build-tidy` compile_commands using THAT toolchain's
# matching clang so headers always resolve, reusing it if already present.
# $SWCPP_TIDY_BUILD overrides the build dir. (This mirrors run-ci's lint_gate
# exactly — the two share this logic.)

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/_env.sh"

cd "$REPO"

resolve_clang_tidy() {
    if [ -n "${SWCPP_CLANG_TIDY:-}" ]; then echo "$SWCPP_CLANG_TIDY"; return 0; fi
    local c
    for c in \
        /opt/homebrew/opt/llvm/bin/clang-tidy \
        /usr/local/opt/llvm/bin/clang-tidy \
        clang-tidy; do
        if command -v "$c" >/dev/null 2>&1 || [ -x "$c" ]; then echo "$c"; return 0; fi
    done
    return 1
}

ct="$(resolve_clang_tidy)" || { echo "no clang-tidy found" >&2; exit 1; }
tidy_build="${SWCPP_TIDY_BUILD:-}"
if [ -z "$tidy_build" ]; then
    # clang-tidy must read a CLANG-generated compile DB. Reuse an existing
    # build-tidy (clang-built by construction below); otherwise generate one with
    # the clang matching the chosen clang-tidy. Do NOT fall back to the plain
    # build/ dir — the TEST gate may build that with g++ (CI) whose
    # compile_commands carries g++-specific flags clang-tidy can't parse as
    # C++17. Always use clang's DB.
    if [ -f build-tidy/compile_commands.json ]; then
        tidy_build="build-tidy"
    else
        cxx="$(dirname "$ct")/clang++"; cc="$(dirname "$ct")/clang"
        # When clang-tidy resolved to a bare name on PATH, dirname is "." and the
        # sibling clang++/clang may not exist there — fall back to the
        # PATH-resolved clang++/clang so CMake still gets a clang compiler.
        [ -x "$cxx" ] || cxx="$(command -v clang++ || true)"
        [ -x "$cc" ] || cc="$(command -v clang || true)"
        cmake_args=(-S . -B build-tidy -DCMAKE_EXPORT_COMPILE_COMMANDS=ON)
        [ -n "$cxx" ] && cmake_args+=(-DCMAKE_CXX_COMPILER="$cxx")
        [ -n "$cc" ] && cmake_args+=(-DCMAKE_C_COMPILER="$cc")
        cmake "${cmake_args[@]}" >&2 || exit 1
        tidy_build="build-tidy"
    fi
fi

# Fan clang-tidy across cores. A single `clang-tidy file1 file2 ...` invocation
# analyzes every TU SERIALLY — historically the whole CI wall-clock (~14min: 65 TUs
# one at a time). clang-tidy parallelizes trivially per-TU, so run one process per
# file across all cores. IDENTICAL behavior: same clang-tidy binary, same compile
# DB (-p), same --header-filter, same .clang-tidy (WarningsAsErrors:'*' => any
# finding is a nonzero exit). xargs exits 123 if ANY invocation exited nonzero, so
# a single finding still fails the gate. Findings still print (each to its own
# clang-tidy's stdout). Job count: cores (min 1); overridable via SWCPP_TIDY_JOBS.
jobs="${SWCPP_TIDY_JOBS:-$( (command -v nproc >/dev/null && nproc) || sysctl -n hw.ncpu 2>/dev/null || echo 4 )}"

# clang-tidy result cache (vendored ctcache, availability-gated). When CTCACHE_DIR
# is set AND the vendored wrapper + python3 are present, route each clang-tidy call
# through it: it hashes the PREPROCESSED TU (all headers inlined) + the raw #include
# lines + the resolved `--dump-config`, so any change to the file, a header it pulls
# in, the checks, or the config invalidates and re-runs. An UNCHANGED TU returns the
# cached result WITHOUT running clang-tidy. Fail-open by construction: any hash/cache
# error falls through to the real clang-tidy (a finding is never skipped — audited +
# tested: cached failures replay, edits invalidate). Absent CTCACHE_DIR / wrapper /
# python3 => plain clang-tidy, exact prior behavior. Cross-run persistence is the CI
# job's `actions/cache` on $CTCACHE_DIR. This composes with the xargs fan-out below.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
tidy_wrapper="$SCRIPT_DIR/clang_tidy_cache.py"
if [ -n "${CTCACHE_DIR:-}" ] && [ -f "$tidy_wrapper" ] && command -v python3 >/dev/null 2>&1; then
    export CTCACHE_SAVE_OUTPUT=1   # cache stdout too, so a cached run replays findings
    tidy_cmd=(python3 "$tidy_wrapper" "$ct")
else
    tidy_cmd=("$ct")
fi

# bash-3.2 compatible (macOS default): stream find -> xargs, NUL-delimited so paths
# with spaces are safe. -n 1 = one clang-tidy (cached) per file; -P = fan across cores.
if ! find src include -name '*.cpp' | grep -q .; then
    echo "no C++ sources found to lint" >&2; exit 1
fi
find src include -name '*.cpp' -print0 \
  | xargs -0 -P "$jobs" -n 1 "${tidy_cmd[@]}" -p "$tidy_build" \
        --header-filter='signalwire-cpp/(src|include)/' --quiet
exit $?
