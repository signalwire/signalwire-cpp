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

# SCOPE (widened 2026-07-30). Previously src/ + include/ only. tools/ (19 TUs)
# was FORMATTED but never LINTED, and examples/ (73 TUs across examples/,
# rest/examples/, relay/examples/) was neither -- all excluded by this hard
# `find` path list with no rationale anywhere. The only exclusion this repo ever
# WROTE DOWN a reason for is vendored deps/, and that reason ("third-party code
# we do not own") does not extend to our own tools or our own shipped examples.
#
# Per the owner: examples and tests are shipping code too, and there is ONE bar.
# Linting these trees found real defects -- 11 discarded [[nodiscard]] results in
# the RELAY/REST examples, 7 atoi/atof calls that turned a bad PORT into port 0,
# and 87 main() functions that answered an exception with std::terminate.
#
# deps/ stays out, now enforced at the compiler (CMake marks it a SYSTEM include)
# rather than by a path list, because clang-diagnostic-* are compiler warnings
# that --header-filter cannot reach.
#
# tests/ is in scope too, via its own invocation below (three checks scoped off,
# owner-ruled). The header-filter must therefore cover tests/ as well, so that
# findings inside the 123 .cpp files #included into test_main.cpp are reported at
# their OWN file:line rather than being filtered out as "not the main file".
HEADER_FILTER='signalwire-cpp/(src|include|tools|tests|examples|rest|relay)/'

if ! find src include tools examples rest/examples relay/examples -name '*.cpp' | grep -q .; then
    echo "no C++ sources found to lint" >&2; exit 1
fi
find src include tools examples rest/examples relay/examples -name '*.cpp' -print0 \
  | xargs -0 -P "$jobs" -n 1 "${tidy_cmd[@]}" -p "$tidy_build" \
        --header-filter="$HEADER_FILTER" --quiet
shipped_rc=$?

# ---------------------------------------------------------------------------
# tests/ — SAME .clang-tidy, SAME WarningsAsErrors, burned to ZERO, with exactly
# THREE checks scoped off (owner-ruled 2026-07-30). This is a separate
# invocation only because those three exclusions are scoped; it is NOT a second
# config and NOT a looser tier. Every other check is ON for tests/, and tests/
# is fully covered by the FMT gate.
#
# The three, each with the reason it is wrong FOR THIS CONTEXT:
#
#   performance-unnecessary-copy-initialization
#     MACRO ARTIFACT + a real correctness hazard. 319 of its 320 findings come
#     from `auto _a = (a)` inside ASSERT_EQ/ASSERT_NE, not from test source, and
#     that copy is LOAD-BEARING: binding by const& instead makes
#     `mock.requests()[0].method` a reference into a by-value temporary that
#     dies at the end of the full expression. Measured, not assumed — the change
#     fails 5 tests. A rule whose remedy introduces dangling references into
#     correct code is the rule being wrong here.
#
#   readability-simplify-boolean-expr
#     MACRO ARTIFACT, 231 of 231. ASSERT_TRUE(x) expands to `if (!(x))`, and the
#     check then proposes applying DeMorgan to the MACRO's negation of the
#     caller's compound condition. There is nothing in test source to simplify.
#
#   bugprone-suspicious-include
#     THE DOCUMENTED ARCHITECTURE, 123 of 123. test_main.cpp #includes 123 .cpp
#     files on purpose so the suite is one translation unit — CLAUDE.md:96:
#     "All test files are #included into test_main.cpp and compiled as one
#     translation unit."
#
# Everything else in tests/ was BURNED, not excused: discarded [[nodiscard]]
# results, unchecked ::bind/getsockname, swallowed MOCK_*_PORT parse errors,
# exceptions escaping std::thread bodies, std::system("mkdir -p"), and 21
# unchecked optional dereferences.
tests_checks='-performance-unnecessary-copy-initialization'
tests_checks="$tests_checks,-readability-simplify-boolean-expr"
tests_checks="$tests_checks,-bugprone-suspicious-include"

# Analyse only the files that are REAL translation units. The other 123 test
# .cpp files are #included into test_main.cpp and compiled as one TU, so they
# have no compile_commands entry -- handing them to clang-tidy directly would
# ERROR ("Compile command not found"), not analyse. They ARE analysed, through
# test_main.cpp, and $HEADER_FILTER covers tests/ so each finding is reported at
# its own file:line. Proven: planting a violation in an #included test file makes
# this gate report it against THAT file.
tests_tus="tests/test_main.cpp tests/mocktest.cpp tests/relay_mocktest.cpp tests/tls_mocktest.cpp"

# Guard the list against going stale: any tests/*.cpp that is NOT one of the four
# and is NOT #included by test_main.cpp would be silently unanalysed. Fail loud
# instead of quietly shrinking the gate's coverage.
missing=""
for f in tests/*.cpp; do
    case " $tests_tus " in *" $f "*) continue ;; esac
    base="${f##*/}"
    grep -q "#include \"$base\"" tests/test_main.cpp || missing="$missing $f"
done
if [ -n "$missing" ]; then
    echo "run-lint: these tests/*.cpp are neither a listed TU nor #included by" >&2
    echo "          test_main.cpp, so nothing would analyse them:$missing" >&2
    echo "          Add them to \$tests_tus (with a compile_commands entry) or to" >&2
    echo "          test_main.cpp's include list." >&2
    exit 1
fi

# shellcheck disable=SC2086
echo $tests_tus | tr ' ' '\n' \
  | xargs -P "$jobs" -n 1 "${tidy_cmd[@]}" -p "$tidy_build" \
        --header-filter="$HEADER_FILTER" --checks="$tests_checks" --quiet
tests_rc=$?

[ "$shipped_rc" -eq 0 ] && [ "$tests_rc" -eq 0 ]
exit $?
