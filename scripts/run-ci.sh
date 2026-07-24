#!/usr/bin/env bash
# run-ci.sh — canonical local-and-CI gate runner for signalwire-cpp.
#
# Same script invoked locally (`bash scripts/run-ci.sh`) AND by the
# GitHub Actions workflow. No drift between local and CI behavior.
#
# Canonical FMT/LINT/TEST entry points (self-bootstrapping, run from any CWD):
#   * FMT   -> scripts/run-format.sh  (clang-format -i / --dry-run -Werror)
#   * LINT  -> scripts/run-lint.sh    (clang-tidy curated set)
#   * TEST  -> scripts/run-tests.sh   (cmake build + run_tests; host mode)
# All three (and this run-ci) source scripts/_env.sh for the clang-18 (llvm@18)
# PATH bootstrap. Do NOT invoke clang-format/clang-tidy/run_tests directly —
# these scripts are the single documented entry points.
#
# Gates (in order, fail-fast):
#   1. cmake --build build --target run_tests emit_corpus + ./build/run_tests
#                                         — language test runner
#   2. signature regen                    — python adapter via libclang
#   3. drift gate                         — porting-sdk diff_port_signatures.py
#   4. surface-fresh gate                 — porting-sdk check_surface_freshness.py
#                                           (regenerate port_surface.json in place
#                                           via the SAME host-side regex enumerator
#                                           the SIGNATURES gate uses, then compare
#                                           the committed copy against the regen
#                                           MODULO the generated_from git-sha;
#                                           closes the Layer-B-not-gated hole where
#                                           port_surface.json silently rots)
#   5. no-cheat gate                      — porting-sdk audit_no_cheat_tests.py
#   6. emission gate                      — porting-sdk diff_port_emission.py
#                                           (byte-compare FunctionResult
#                                           serialisation vs Python to_dict()
#                                           over the shared 81-entry corpus;
#                                           reuses the emit_corpus binary built
#                                           in gate 1)
#
# OpenSSL 3.0+ requirement
# ------------------------
# The C++ build now enables cpp-httplib TLS (CPPHTTPLIB_OPENSSL_SUPPORT) and
# links IXWebSocket's OpenSSL backend, so it REQUIRES OpenSSL >= 3.0. Hosts
# stuck on OpenSSL 1.1.1 (EOL) can no longer build it. The TEST gate therefore
# builds + runs in an OpenSSL-3.0 container when the host OpenSSL is too old:
#
#   * Build host: if `openssl version` reports >= 3.0, build on the host.
#   * Otherwise: run cmake + run_tests inside Docker. Set SWCPP_CONTAINER to a
#     RUNNING container (default "swcpp") that has the repo's parent dir mounted
#     and --network host (so it reaches the host-run mock servers); the gate
#     uses `docker exec`. If no such container is running, set SWCPP_DOCKER_IMAGE
#     (default gcc:14-based with OpenSSL 3 + cmake + libssl-dev) and the gate
#     `docker run`s a throwaway one with ~/src mounted + --network host.
#   * Force the container path regardless of host OpenSSL with SWCPP_USE_DOCKER=1.
#
# The shared mock servers (mock_signalwire / mock_relay, plus their --tls
# instances) run on the HOST; the container reaches them at 127.0.0.1 over
# --network host. Gates 2-4 (libclang regen + pure-Python diffs) run on the
# host unchanged.
#
# Assumes the build/ tree has already been configured. If a host build is used
# and build/ is missing it is bootstrapped here; the container path configures
# its own out-of-source tree under /tmp.

set -u
set -o pipefail

PORT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$PORT_ROOT/.sw-tmp"  # repo-local CI scratch (never /tmp)
PORT_NAME="signalwire-cpp"

# Gate-enforcement plan (Part D): cpp's widened Wave-A findings are BLOCKING, not
# report-only. The shared wave-A gate scripts (count_claim / audit_docs /
# status_claim / semver_diff / dead_public_error) default to report-only when
# SW_WAVE_A_REPORT_ONLY is unset; setting it to 0 makes every newly-caught wave-A
# violation count toward the exit code. cpp's wave-A red list has been burned to
# zero (namespace counts → 22, area_code → areacode wire fix, RELAY status heading,
# baseline commit anchor), so this stays green while fully enforced.
export SW_WAVE_A_REPORT_ONLY=0

# D3 strict-mocks endgame: the REST mock 400s an unknown key / wrong type instead of
# tolerantly journaling it. Exported here (not only in a workflow) so local and CI
# stay in lockstep; cpp's per-test mocktest harness spawns `python -m mock_signalwire`
# via fork+execlp, which INHERITS this env, so the strict default reaches every
# REST-suite mock (TEST + REST-COVERAGE lanes). Declared load-bearing in
# WIRED_MODES.md (the WIRED-MODES guard reds if a merge drops this line).
export MOCK_SIGNALWIRE_STRICT=1

# Shared tool-environment bootstrap — the SAME _env.sh the canonical
# run-format.sh / run-lint.sh / run-tests.sh source. It prepends clang-18
# (llvm@18) to PATH so the FMT gate's clang-format, the LINT gate's clang-tidy
# fallback, AND the Python REST/type generators (which shell to clang-format via
# scripts/_cpp_fmt.py) all resolve clang-18 no matter the caller's shell, and it
# fails loud if clang-format 18 is unresolvable. _env.sh runs with `set -e`; run-ci
# manages gate exit codes manually via run_gate, so restore run-ci's own shell
# options (-u -o pipefail, NO -e) immediately after sourcing.
# shellcheck source=scripts/_env.sh
source "$PORT_ROOT/scripts/_env.sh"
set +e
set -u
set -o pipefail

# --- OpenSSL-3.0 build routing ------------------------------------------------
SWCPP_CONTAINER="${SWCPP_CONTAINER:-swcpp}"
SWCPP_DOCKER_IMAGE="${SWCPP_DOCKER_IMAGE:-}"

host_openssl_ge_3() {
    local v
    v="$(openssl version 2>/dev/null | awk '{print $2}')" || return 1
    case "$v" in
        3.*|[4-9].*) return 0 ;;
        *) return 1 ;;
    esac
}

# Pick a free TCP port by binding 127.0.0.1:0 and reading back the OS-assigned
# port, then closing the socket so the caller can hand the (now-free) port to a
# child process. Avoids the hardcoded-port collisions that hang the mock gate
# when something else already holds the fixed port. Echos the port; returns 1
# (printing nothing) if no port could be obtained.

# Decide how the TEST gate builds + runs run_tests. Echos one of:
#   host                 — build on host (OpenSSL >= 3.0)
#   exec:<container>     — docker exec into a running container
#   run:<image>          — docker run a throwaway container
select_build_mode() {
    if [ "${SWCPP_USE_DOCKER:-0}" != "1" ] && host_openssl_ge_3; then
        echo "host"; return 0
    fi
    if [ -n "$SWCPP_CONTAINER" ] && command -v docker >/dev/null 2>&1 \
       && docker ps --format '{{.Names}}' 2>/dev/null | grep -qx "$SWCPP_CONTAINER"; then
        echo "exec:$SWCPP_CONTAINER"; return 0
    fi
    if [ -n "$SWCPP_DOCKER_IMAGE" ] && command -v docker >/dev/null 2>&1; then
        echo "run:$SWCPP_DOCKER_IMAGE"; return 0
    fi
    return 1
}

resolve_porting_sdk() {
    if [ -n "${PORTING_SDK:-}" ] && [ -d "$PORTING_SDK/scripts" ]; then
        echo "$PORTING_SDK"
        return 0
    fi
    if [ -d "$PORT_ROOT/../porting-sdk/scripts" ]; then
        (cd "$PORT_ROOT/../porting-sdk" && pwd)
        return 0
    fi
    return 1
}

PORTING_SDK_DIR="$(resolve_porting_sdk)" || {
    echo "FATAL: porting-sdk not found, clone it adjacent to this repo" >&2
    echo "       (expected $PORT_ROOT/../porting-sdk or \$PORTING_SDK env var)" >&2
    exit 2
}

# signalwire-python checkout used as the ORACLE side of the Layer-D BEHAVIORAL-*
# gates (diff_port_<surface>.py builds the golden by importing signalwire-python).
# Resolved the SAME way porting-sdk's diff_port_emission._resolve_python_sdk does:
# $PYTHON_SDK env wins (CI sets it), else adjacency ($PORT_ROOT/../signalwire-python
# — the layout used both locally and in the cross-port workflow). Passed explicitly
# to each differ via --python-sdk so the gate never depends on `signalwire` already
# being importable from the caller's ambient environment.
resolve_python_sdk() {
    if [ -n "${PYTHON_SDK:-}" ] && [ -d "$PYTHON_SDK/signalwire" ]; then
        echo "$PYTHON_SDK"
        return 0
    fi
    if [ -d "$PORT_ROOT/../signalwire-python/signalwire" ]; then
        (cd "$PORT_ROOT/../signalwire-python" && pwd)
        return 0
    fi
    return 1
}

PYTHON_SDK_DIR="$(resolve_python_sdk)" || {
    echo "FATAL: signalwire-python not found (needed as the BEHAVIORAL-* oracle)." >&2
    echo "       (expected $PORT_ROOT/../signalwire-python or \$PYTHON_SDK env var)" >&2
    exit 2
}

FAILED_GATES=""

run_gate() {
    local name="$1"; shift
    local description="$1"; shift
    # Optional leading --tier=<pr|nightly> (default pr). A nightly-tier gate is
    # SKIPPED unless SW_CI_TIER=nightly|all — mirrors gate_scheduler.sh's tier= so
    # cpp (which runs gates serially via run_gate, not the DAG scheduler) gets the
    # same per-PR/nightly split. nightly is a superset (runs pr gates too).
    local tier=pr
    case "$1" in --tier=*) tier="${1#--tier=}"; shift ;; esac
    if [ "$tier" = "nightly" ]; then
        case "${SW_CI_TIER:-pr}" in
            nightly|all) : ;;   # active — run it
            *) echo "[$name] $description ... SKIP (tier=nightly; runs in nightly CI)"; return 0 ;;
        esac
    fi
    local logfile
    logfile="$(mktemp)"
    "$@" >"$logfile" 2>&1
    local rc=$?
    if [ "$rc" -eq 0 ]; then
        echo "[$name] $description ... PASS"
        rm -f "$logfile"
        return 0
    fi
    echo "[$name] $description ... FAIL: exit $rc"
    sed 's/^/    /' "$logfile" | tail -40
    rm -f "$logfile"
    FAILED_GATES="$FAILED_GATES $name"
    return $rc
}

cd "$PORT_ROOT"

echo "==> running CI gates for $PORT_NAME (porting-sdk at $PORTING_SDK_DIR)"

BUILD_MODE="$(select_build_mode)" || {
    echo "FATAL: C++ build needs OpenSSL >= 3.0 but the host has $(openssl version 2>/dev/null | awk '{print $2}')" >&2
    echo "       and no OpenSSL-3.0 container is available. Start a container named" >&2
    echo "       \$SWCPP_CONTAINER (default 'swcpp') with the repo parent mounted +" >&2
    echo "       --network host, OR set \$SWCPP_DOCKER_IMAGE to an OpenSSL-3.0 image." >&2
    exit 2
}
echo "==> TEST build mode: $BUILD_MODE"

# In-container path to this repo (repo parent is mounted at /src by convention).
SWCPP_CONTAINER_REPO="${SWCPP_CONTAINER_REPO:-/src/$PORT_NAME}"
SWCPP_CONTAINER_BUILD="${SWCPP_CONTAINER_BUILD:-/tmp/run-ci-build}"  # container-internal path (docker /tmp, not host)

# Build run_tests + emit_corpus + emit_skills + execute run_tests, honoring
# BUILD_MODE. Each branch produces the same observable result: a built run_tests
# (+ emit_corpus, reused by the EMISSION gate; + emit_skills, reused by the
# SKILL-CONTRACT gate; + the five Layer-D dump binaries wire_dump/swml_dump/
# state_dump/http_dump/wire_relay_dump/doc_wire_dump, reused by the BEHAVIORAL
# suite Layer-D + DOC-WIRE rules) and
# run_tests' exit code. The dump binaries are built here too so the downstream
# gates don't reconfigure the tree (and so the host / exec modes leave
# ready-to-run binaries).
test_gate() {
    case "$BUILD_MODE" in
        host)
            if [ ! -d build ]; then
                echo "[BOOTSTRAP] cmake -S . -B build (initial configure)"
                cmake -S . -B build || return 1
            fi
            # Build the two downstream-gate dump binaries (emit_corpus for
            # EMISSION, emit_skills for SKILL-CONTRACT) here so those gates find
            # them ready, then delegate the run_tests build+run to the CANONICAL
            # scripts/run-tests.sh (self-bootstrapping, host mode). run-tests.sh
            # rebuilds run_tests (a near-no-op since it was just built) and runs
            # the full suite.
            # pagination_dump (PAGINATION-CORPUS, per-PR) + relay_liveness_dump
            # (RELAY-LIVENESS, nightly) are built here too so the BEHAVIORAL
            # suite finds their binaries at build/<name> — without this the
            # per-PR PAGINATION-CORPUS rule runs a missing binary and reds with
            # "dump did not emit valid JSON".
            cmake --build build --target emit_corpus emit_skills \
                wire_dump swml_dump strict_render_dump state_dump http_dump wire_relay_dump doc_wire_dump \
                pagination_dump relay_liveness_dump ai_chat_dump -j"$(sw_build_jobs)" || return 1
            bash "$PORT_ROOT/scripts/run-tests.sh"
            ;;
        exec:*)
            local c="${BUILD_MODE#exec:}"
            docker exec "$c" bash -c "
                cmake -S '$SWCPP_CONTAINER_REPO' -B '$SWCPP_CONTAINER_BUILD' -DCMAKE_BUILD_TYPE=Release \
                && cmake --build '$SWCPP_CONTAINER_BUILD' --target run_tests emit_corpus emit_skills wire_dump swml_dump strict_render_dump state_dump http_dump wire_relay_dump doc_wire_dump pagination_dump relay_liveness_dump ai_chat_dump -j\"\$(nproc)\" \
                && '$SWCPP_CONTAINER_BUILD/run_tests'"
            ;;
        run:*)
            local img="${BUILD_MODE#run:}"
            # Mount the repo's PARENT (so porting-sdk is adjacent for the mock
            # adjacency walk) and use --network host to reach host-run mocks.
            docker run --rm --network host -v "$(dirname "$PORT_ROOT")":/src "$img" bash -c "
                cmake -S '$SWCPP_CONTAINER_REPO' -B '$SWCPP_CONTAINER_BUILD' -DCMAKE_BUILD_TYPE=Release \
                && cmake --build '$SWCPP_CONTAINER_BUILD' --target run_tests emit_corpus emit_skills wire_dump swml_dump strict_render_dump state_dump http_dump wire_relay_dump doc_wire_dump pagination_dump relay_liveness_dump ai_chat_dump -j\"\$(nproc)\" \
                && '$SWCPP_CONTAINER_BUILD/run_tests'"
            ;;
        *)
            echo "unknown BUILD_MODE: $BUILD_MODE"; return 1 ;;
    esac
}

# EMISSION gate: byte-compare the C++ FunctionResult serialisation against
# Python's to_dict() over the shared 81-entry corpus
# (porting-sdk/scripts/diff_port_emission.py + tools/emit_corpus.cpp). Pure
# serialisation — no mocks, no network. The differ runs the dump program and
# reads its stdout; we hand it a --dump-cmd that emits the binary built in the
# TEST gate, resolved per BUILD_MODE. For host/exec the binary persists
# (`./build/emit_corpus` / a docker-exec into the running container); for the
# throwaway run: mode the container vanishes after test_gate, so the dump-cmd is
# a self-contained docker run that (re)builds emit_corpus with logs on stderr and
# execs it so ONLY the JSON object reaches stdout.

# BEHAVIORAL-<SURFACE> gates (Layer D): run each surface's dump binary and diff its
# observed behavior against the signalwire-python oracle
# (porting-sdk/scripts/diff_port_<surface>.py + tools/<surface>_dump.cpp). Locks
# the 64 cross-port behaviors so they can never silently regress. Same BUILD_MODE
# dump-cmd routing as the EMISSION gate: for host/exec the binary persists
# (`./build/<surface>_dump` / a docker-exec into the running container); for the
# throwaway run: mode the container vanishes after test_gate, so the dump-cmd is a
# self-contained docker run that (re)builds the target with logs on stderr and
# execs it so ONLY the JSON reaches stdout. The dumps already emit pure JSON on
# stdout (logs go to stderr) regardless of ambient SIGNALWIRE_LOG_* env.

# SURFACE-FRESH gate: prove the committed port_surface.json (Layer B) still
# matches a fresh regeneration. run-ci otherwise gates only Layer A
# (diff_port_signatures.py over port_signatures.json), so the surface can rot
# unnoticed when a symbol lands in headers but the surface isn't regenerated.
#
# cpp's enumerate_surface.py is a HOST-side regex header walker (no compiler,
# no libclang, no mock servers) — exactly like the SIGNATURES gate's
# enumerate_signatures.py — so it runs on the host regardless of BUILD_MODE and
# needs no container routing. It writes port_surface.json in place (default
# output) and includes the strip_attributes [[nodiscard]] fix, so a clean tree
# regenerates byte-for-byte modulo the generated_from git-sha. We snapshot the
# committed copy, regenerate in place, diff modulo provenance via
# check_surface_freshness.py, then restore the working copy.
# Cache of the freshly-regenerated port_surface.json so SURFACE-FRESH and
# SURFACE-DIFF (which both otherwise re-run enumerate_surface.py from scratch)
# share ONE regeneration. The enumerator is deterministic on a clean tree, so a
# single regen serves both gates. The path is derived INSIDE each gate from
# $PORT_ROOT (not a shared top-level var) so the gates stay self-contained when a
# harness like porting-sdk's sw-verify sources only the function definitions and
# calls one gate directly. Cache lives under the repo-local .sw-tmp (gitignored) —
# never /tmp.
_fresh_surface_cache_path() { echo "$PORT_ROOT/.sw-tmp/fresh_port_surface.json"; }


# SURFACE-DIFF gate: diff the port's public surface against the Python reference
# (membership: omissions + additions). The signature DRIFT gate (Layer A) checks
# method *signatures*; this checks surface *membership* — public symbols the port
# has that Python doesn't and vice-versa. Like SURFACE-FRESH it regenerates
# port_surface.json in place via the host regex enumerator (no compiler / no
# mocks / BUILD_MODE-independent), diffs, then restores the committed copy
# unconditionally so the gate is side-effect free.

# SKILL-CONTRACT gate: the surface/drift/emission gates see signatures + symbol
# names + FunctionResult.to_json(); NONE sees a built-in skill's SWAIG tool
# contract ({name, parameters, required, enum} each skill registers). This differ
# closes that gap: it builds the Python oracle by instantiating each covered
# reference skill, runs the C++ skill-dump program (the emit_skills binary built
# in the TEST gate, which reads the SAME shared corpus via
# skill_contract_corpus.py), and structurally compares the two. DESCRIPTIONS +
# implementation (handler vs DataMap) are not compared — only name / param-name /
# param-type / enum / required. Mirrors the EMISSION gate's BUILD_MODE routing:
# the emit_skills binary is resolved per host/exec/run mode exactly like
# emit_corpus.

# REST-COVERAGE gate: every canonical REST route the SDK implements must be
# exercised with BOTH a success (2xx) AND an error (4xx/5xx) response on the
# correct on-the-wire path (parity). Measured by replaying the mock journal of a
# REST-suite run through porting-sdk's rest_coverage checker. Accepted gaps —
# routes with no SDK method, malformed canonical routes, mock-router collisions —
# are allowlisted: the shared baseline (porting-sdk/REST_COVERAGE_BASELINE.md) +
# this port's REST_COVERAGE_GAPS.md. A stale entry (route now covered) fails the
# gate. Self-contained: spins its own mock, runs the rest_mock_ suite SERIALLY
# (run_tests on one thread) so all traffic lands in one journal, then checks that
# journal. Same shape as go's/python's/java's gate.
#
# The run_tests binary was already built by the TEST gate (gate 1). It is
# resolved per BUILD_MODE exactly like the EMISSION / SKILL-CONTRACT gates: host
# uses ./build/run_tests; exec/run modes go through the container. The mock runs
# on the HOST regardless (the container reaches it via --network host).

# Gate 5c: SPEC-PARITY — the REST surface must match the canonical spec in BOTH
# directions. REST-COVERAGE (5b) proves every route the SDK *implements* is
# exercised; this proves the set the SDK implements EQUALS the canonical spec
# (modulo checked-in gaps): no canonical route left unimplemented (A−B), no
# implemented route that matches no canonical route (B−A, i.e. invented surface).
# Set B is produced deterministically by the route_registry binary, which stands
# up a loopback recording server, points a RestClient at it, invokes every
# namespace method, and reads back the routes the SDK actually dispatched (no
# hand-authored route list, no reflection — C++ has none).
# diff_spec_implementation.py matches that against the spec. Accepted
# not-implemented gaps live in the shared SPEC_IMPLEMENTATION_GAPS.md; a stale
# gap (now implemented) or unsanctioned divergence fails the gate. Same shape as
# go's/java's/rust's gate. The binary was built by the TEST gate; it is resolved
# per BUILD_MODE exactly like the EMISSION gate (host binary / docker exec /
# throwaway docker run that rebuilds it).

# ROUTE-COLLISION gate: cross-references the route-registry (operation ->
# (method, path)) with the surface enumeration to find latent route-split /
# crud-dup / orphan-dto defects. cpp HAS a registry (the route_registry binary),
# so — unlike ports without one — this gate can run standalone. It is built +
# run per BUILD_MODE exactly like spec_parity_gate above (host binary / docker
# exec / throwaway docker run that rebuilds it), then the collision checker
# reads the registry. The 2 fabric list_addresses route-split entries are the
# user-approved ROUTE_COLLISION_ALLOW.md exceptions (392bb5b); orphan-dto is
# report-only inside the gate. Enforcing (no --report-only).

# FMT gate: the C++ format gate (clang-format, config in .clang-format —
# Google base / 100-col). Source-style only; proven surface/emission-neutral
# (the libclang SIGNATURES enumerator + the regex SURFACE enumerator are both
# whitespace-insensitive, so a reformat leaves port_signatures.json /
# port_surface.json byte-identical and EMISSION 81/81). Mirrors the go/ruby/java
# fmt_gate split:
#   * LOCAL ($CI unset)  -> `clang-format -i`: reformats your working tree in
#     place so you never hand-run it; notes if it changed files, then re-checks.
#   * CI ($CI=true)      -> `clang-format --dry-run -Werror` (read-only): FAILS
#     if any unformatted source reached CI.
# Scope is first-party src/ + include/ ONLY — vendored deps/ (httplib.h,
# json.hpp, nlohmann/) and the FetchContent IXWebSocket tree are never touched.
# clang-format runs on the host regardless of BUILD_MODE (no compiler/SDK
# needed — it only parses tokens).
# The FMT gate now delegates to the CANONICAL scripts/run-format.sh (single
# entry point, self-bootstrapping via _env.sh). LOCAL (CI unset) applies in
# place; CI ($CI set) passes --check for the read-only dry-run. Same source
# scope + dual-mode behavior as before, just no longer inlined here.
fmt_gate() {
    bash "$PORT_ROOT/scripts/run-format.sh" ${CI:+--check}
}

# LINT gate: the C++ lint gate (clang-tidy, curated check set in .clang-tidy
# burned to ZERO findings; WarningsAsErrors:'*' makes any finding a nonzero
# exit). The curated set polices real bug patterns (bugprone-*, performance-*,
# select readability-*) and NEVER idiom — modernize-* / style-churn checks and
# the three surface/idiom-forcing checks (derived-method-shadowing,
# enum-size, throwing-static-init) are disabled with rationale at the config
# site (RULES.md §3: a check that fails a parity-required, wire-neutral shape is
# mis-scoped — fix the check, not the port). Scope is first-party src/+include/
# only; vendored deps/ are excluded by the HeaderFilterRegex in .clang-tidy.
#
# clang-tidy needs a compile_commands.json AND a clang-tidy new enough to parse
# the build's standard-library headers. On Linux CI the system clang-tidy and
# the `build` tree's compile commands agree, so we reuse `build`. On macOS the
# system SDK's libc++ can outrun an older Homebrew clang-tidy (e.g. clang-tidy
# 18 cannot parse a macOS-26 SDK's __builtin_clzg), so the gate (a) picks the
# newest clang-tidy it can find unless $SWCPP_CLANG_TIDY overrides, and (b)
# generates a dedicated `build-tidy` compile_commands using THAT toolchain's
# matching clang so headers always resolve, reusing it if already present.
# $SWCPP_TIDY_BUILD overrides the build dir.
# The LINT gate now delegates to the CANONICAL scripts/run-lint.sh (single entry
# point, self-bootstrapping via _env.sh). The clang-tidy resolution (newest tidy
# for macOS-SDK parse / $SWCPP_CLANG_TIDY override) + the dedicated build-tidy
# compile-DB generation now live in that script; run-ci just invokes it.
lint_gate() {
    bash "$PORT_ROOT/scripts/run-lint.sh"
}

# STRICT-MOCKS (§2.2 / Part 1.4): re-run the RELAY mock suite with mock_relay in
# STRICT mode (MOCK_RELAY_STRICT=1 → 400s an unknown field / duplicate id instead
# of tolerantly journaling it) so a wire-shape regression fails loud. cpp's relay
# tests self-spawn `python -m mock_relay` via fork+execlp which INHERITS this env,
# so exporting it here reaches the child mock. run_tests was built by the TEST gate;
# resolve it per BUILD_MODE exactly like the other run_tests invocations.
# (Round-4 finding: this body was dropped in the strict-mocks × Part-5 merge while
# the call at the STRICT-MOCKS gate below survived → nightly exit 127. Restored.)
strict_mocks_gate() {
    case "$BUILD_MODE" in
        host)
            env MOCK_RELAY_STRICT=1 SW_TEST_PARALLEL=1 "$PORT_ROOT/build/run_tests" relay_mock_ ;;
        exec:*)
            docker exec -e MOCK_RELAY_STRICT=1 -e SW_TEST_PARALLEL=1 "${BUILD_MODE#exec:}" \
                "$SWCPP_CONTAINER_BUILD/run_tests" relay_mock_ ;;
        run:*)
            local img="${BUILD_MODE#run:}"
            docker run --rm --network host -v "$(dirname "$PORT_ROOT")":/src \
                -e MOCK_RELAY_STRICT=1 -e SW_TEST_PARALLEL=1 "$img" bash -c "
                cmake -S '$SWCPP_CONTAINER_REPO' -B '$SWCPP_CONTAINER_BUILD' -DCMAKE_BUILD_TYPE=Release 1>&2 \
                && cmake --build '$SWCPP_CONTAINER_BUILD' --target run_tests -j\"\$(nproc)\" 1>&2 \
                && '$SWCPP_CONTAINER_BUILD/run_tests' relay_mock_" ;;
        *) echo "unknown BUILD_MODE: $BUILD_MODE"; return 1 ;;
    esac
}

# --- gate-enforcement quartet (§2.1-2.4) --------------------------------------
# DOC-WIRE (§2.1): doc_wire.py spawns the mock in FLAG mode, exports
# MOCK_SIGNALWIRE_PORT, runs the doc_wire_dump binary (built by the TEST gate; it
# points a RestClient at the mock and replays the documented REST call SHAPES),
# then reads the mock's wire_violations journal. The runner is resolved per
# BUILD_MODE like the other dump binaries (host binary / docker exec / throwaway
# run). Per-PR: a single quick replay.

# WAIT-LIVENESS (§2.4): the RELAY Action::wait() liveness contract — wait() BLOCKS
# until the deferred completing event arrives (never a no-op at t~=0, never a hang).
# wait_liveness_dump drives a live mock_relay via the relay_mocktest harness, arms
# deferred completions, and emits the classification; the differ compares it to the
# python golden. nightly (a real-time behavioral check). The mock is self-spawned by
# the dump's relay_mocktest harness (adjacency-discovered) — no gate-side mock.

# STRICT-MOCKS (§2.2): re-run the RELAY mock suite with the mock in STRICT mode
# (MOCK_RELAY_STRICT=1 → mock_relay 400s an unknown field / duplicate id instead of
# tolerantly journaling it), so a wire-shape regression the tolerant mock would
# swallow fails loud. cpp's relay tests self-spawn `python -m mock_relay` via
# fork+execlp, which INHERITS this env, so exporting MOCK_RELAY_STRICT here reaches
# the child mock. run_tests was built by the TEST gate; resolve it per BUILD_MODE.
# nightly (a second full RELAY pass is heavy).

# =============================================================================
# GATE INVOCATIONS
# =============================================================================
# Part 5 gate SUITES. The former ~42 per-gate run_gate lines
# (SIGNATURES/DRIFT/SURFACE-*/SEMVER-DIFF/GEN-TYPE-DEGENERACY/GEN-IDIOM/
# ROUTE-COLLISION/GEN-FRESH*/EMISSION/BEHAVIORAL-*/SKILL-CONTRACT/SWAIG-*/
# ERROR-ENVELOPE/PAGINATION-WIRED/DOC-WIRE/WAIT-LIVENESS/REST-COVERAGE/
# SPEC-PARITY/DOC-AUDIT/DOC-LINKS/DOC-LANG-PURITY/DOC-ENV/COUNT-CLAIM/
# ACCESSOR-TRUTH/STATUS-CLAIM/README-INCLUDE/SUPPRESSION-LEDGER/
# IGNORE-LEDGER-VERIFY/ARTIFACT-DENY/RELEASE-FRESH/PACKAGE-SMOKE/
# META-CONSISTENT) now run under 6 shared SUITE engines. Each suite emits every
# original gate NAME as a `[SUITE:RULE] ... PASS/FAIL` rule ID (failure identity +
# allowlists + finding output unchanged) and exits nonzero iff any of its rules
# fails. Byte-identity vs the old per-gate path is proven by
# porting-sdk/tests/test_suite_parity*.py.
#
# cpp runs its gates SERIALLY via run_gate (not the DAG scheduler the other 9
# ports use), so each suite is invoked as a single run_gate line. The go/ts
# `sched_gate NAME ... -- cmd` template maps 1:1 to cpp's
# `run_gate "NAME" "desc" cmd`; the scheduler's `tier=nightly` maps to run_gate's
# leading `--tier=nightly`, and its `--rules <subset>` mixed-tier split is passed
# through verbatim to behavioral.py / package.py.
#
# The `--fn` gate BODIES the old per-gate lines used (surface_fresh_gate,
# surface_diff_gate, emission_gate, behavioral_gate, skill_contract_gate,
# rest_coverage_gate, spec_parity_gate, route_collision_gate, gen_fresh_tests_gate,
# doc_wire_gate, wait_liveness_gate) are DEAD here — the suites reproduce them
# internally (with cpp's exact BUILD_MODE dump-cmd routing). They are retained as
# function definitions above ONLY because cpp's suite engines shell back to this
# port's generators/binaries the same way; see each suite's cpp branch. run-ci no
# longer CALLS them directly.

# ---- TEST (STAY: native toolchain, heavy) -----------------------------------
# Gate 1: build + run tests (host or OpenSSL-3.0 container per BUILD_MODE). Builds
# the dump/registry binaries the SURFACE/GEN/BEHAVIORAL/PACKAGE suites reuse.
run_gate "TEST" "build run_tests + run_tests ($BUILD_MODE)" test_gate

# ---- Part 5 gate SUITES ------------------------------------------------------

# SURFACE (parity spine): SIGNATURES/DRIFT/SURFACE-FRESH/SURFACE-DIFF/
# GEN-TYPE-DEGENERACY/ROUTE-COLLISION/GEN-IDIOM/SEMVER-DIFF. SIGNATURES->DRIFT
# ordering, the SEMVER-DIFF-reads-SIGNATURES data dep, and SURFACE-FRESH's
# regenerate-then-restore all live INSIDE the suite. ROUTE-COLLISION uses cpp's
# route_registry binary (built in the TEST gate) via the suite's cpp branch.
run_gate "SURFACE" "surface parity suite (SIGNATURES/DRIFT/SURFACE-FRESH/SURFACE-DIFF/GEN-TYPE-DEGENERACY/ROUTE-COLLISION/GEN-IDIOM/SEMVER-DIFF)" \
    python3 "$PORTING_SDK_DIR/scripts/suites/surface.py" --port cpp --repo "$PORT_ROOT"

# GEN (regen-from-specs family): GEN-FRESH/-SWML/-RELAY/-SWAIG/-TESTS.
# GEN-FRESH-TESTS reuses cpp's route_registry binary via the suite's cpp branch.
run_gate "GEN" "generated-code freshness suite (GEN-FRESH/-SWML/-RELAY/-SWAIG/-TESTS)" \
    python3 "$PORTING_SDK_DIR/scripts/suites/gen.py" --port cpp --repo "$PORT_ROOT"

# BEHAVIORAL (one Layer-D pass per rule): the per-PR rules. WAIT-LIVENESS (nightly)
# is the separate line below. cpp's RELAY behavioral rule keeps cpp's HYPHEN
# spelling BEHAVIORAL-WIRE-RELAY. The suite drives cpp's dump binaries (built in
# the TEST gate) + the mock-backed REST-COVERAGE/SPEC-PARITY/DOC-WIRE via cpp's
# BUILD_MODE routing internally.
run_gate "BEHAVIORAL" "behavioral suite, per-PR rules (BEHAVIORAL-*/EMISSION/SKILL-CONTRACT/SWAIG-*/ERROR-ENVELOPE/PAGINATION-WIRED/DOC-WIRE/REST-COVERAGE/SPEC-PARITY)" \
    python3 "$PORTING_SDK_DIR/scripts/suites/behavioral.py" --port cpp --repo "$PORT_ROOT" \
        --rules REST-COVERAGE,SPEC-PARITY,EMISSION,BEHAVIORAL-WIRE,BEHAVIORAL-SWML,BEHAVIORAL-STATE,BEHAVIORAL-HTTP,BEHAVIORAL-WIRE-RELAY,SKILL-CONTRACT,SWAIG-COVERAGE,SWAIG-CLI,ERROR-ENVELOPE,PAGINATION-WIRED,PAGINATION-CORPUS,DOC-WIRE

# BEHAVIORAL-NIGHTLY: the timing-sensitive connection-liveness dumps.
# WAIT-LIVENESS (Action::wait() blocks-until-event) + RELAY-LIVENESS (the broader
# RELAY connection+error contract: A6 creds / A2 relay-contract / F2 dead-peer +
# black-hole / F3 reconnect / max-active-calls). Both spawn a real dump binary
# (built by the TEST gate) and compare its per-fixture classification to the
# python golden. tier=nightly (defer=1).
run_gate "BEHAVIORAL-NIGHTLY" "behavioral suite, nightly rules (WAIT-LIVENESS/RELAY-LIVENESS)" --tier=nightly \
    python3 "$PORTING_SDK_DIR/scripts/suites/behavioral.py" --port cpp --repo "$PORT_ROOT" \
        --rules WAIT-LIVENESS,RELAY-LIVENESS

# DOC-TRUTH (one markdown walk): DOC-AUDIT/DOC-LINKS/DOC-LANG-PURITY/DOC-ENV/
# COUNT-CLAIM/ACCESSOR-TRUTH/STATUS-CLAIM/README-INCLUDE.
run_gate "DOC-TRUTH" "doc-truth suite (DOC-AUDIT/DOC-LINKS/DOC-LANG-PURITY/DOC-ENV/COUNT-CLAIM/ACCESSOR-TRUTH/STATUS-CLAIM/README-INCLUDE)" \
    python3 "$PORTING_SDK_DIR/scripts/suites/doc_truth.py" --port cpp --repo "$PORT_ROOT"

# LEDGER: SUPPRESSION-LEDGER + IGNORE-LEDGER-VERIFY.
run_gate "LEDGER" "ledger governance suite (SUPPRESSION-LEDGER/IGNORE-LEDGER-VERIFY)" \
    python3 "$PORTING_SDK_DIR/scripts/suites/ledger.py" --port cpp --repo "$PORT_ROOT"

# PACKAGE: per-PR rules (ARTIFACT-DENY/RELEASE-FRESH); nightly rules (PACKAGE-SMOKE/
# META-CONSISTENT) on the separate line below.
run_gate "PACKAGE" "package suite, per-PR rules (ARTIFACT-DENY/RELEASE-FRESH)" \
    python3 "$PORTING_SDK_DIR/scripts/suites/package.py" --port cpp --repo "$PORT_ROOT" \
        --rules ARTIFACT-DENY,RELEASE-FRESH

run_gate "PACKAGE-NIGHTLY" "package suite, nightly rules (PACKAGE-SMOKE/META-CONSISTENT)" --tier=nightly \
    python3 "$PORTING_SDK_DIR/scripts/suites/package.py" --port cpp --repo "$PORT_ROOT" \
        --rules PACKAGE-SMOKE,META-CONSISTENT

# ---- gates that STAY standalone (native toolchains + cpp singletons) ----------
# 11 gates the suites do NOT absorb: the native clang toolchains (FMT/LINT), the
# no-cheat auditor, the source/root-analysis singletons (DEAD-PUBLIC-ERROR/
# ROOT-HYGIENE/PUBLIC-JARGON), and the §C1 doc/example EXECUTION gates
# (DOC-CLI + the nightly SNIPPET-COMPILE/SNIPPET-RUN/EXAMPLES-RUN/STRICT-MOCKS).

run_gate "NO-CHEAT" "audit_no_cheat_tests" \
    python3 "$PORTING_SDK_DIR/scripts/audit_no_cheat_tests.py" --root "$PORT_ROOT"

# FMT — clang-format (local: apply in place; CI: --dry-run -Werror)
run_gate "FMT" "clang-format (.clang-format; local: apply, CI: check)" fmt_gate

# LINT — clang-tidy curated set burned to zero (WarningsAsErrors:'*')
run_gate "LINT" "clang-tidy curated set, zero findings" lint_gate

# DEAD-PUBLIC-ERROR — exported error types are raised/caught/user-signalled
run_gate "DEAD-PUBLIC-ERROR" "exported error types are raised/caught/user-signalled (no dead error surface)" \
    python3 "$PORTING_SDK_DIR/scripts/dead_public_error.py" --port cpp --repo .

# ROOT-HYGIENE — no audit/scratch clutter tracked at repo root
run_gate "ROOT-HYGIENE" "no audit/scratch clutter tracked at repo root (allowlist ROOT_HYGIENE_ALLOW.md)" \
    python3 "$PORTING_SDK_DIR/scripts/root_hygiene.py" --port cpp --repo .

# PUBLIC-JARGON — no internal porting jargon in public doc comments
run_gate "PUBLIC-JARGON" "no internal porting jargon leaked into public doc comments" \
    python3 "$PORTING_SDK_DIR/scripts/public_jargon.py" --port cpp --repo .

# AI-CHAT (task #22, COORDINATED pass cpp:ai-chat-client <-> porting-sdk:ai-chat-client):
# wire-behavioral gate for the AIChatClient. Drives the built ai_chat_dump binary
# through the shared ai_chat_corpus against porting-sdk's in-process mock_ai_chat
# and asserts the client speaks the AI Chat JSON-RPC protocol per the vendored spec
# (ai-chat-specs/ai-chat.yaml). The gate script (diff_port_ai_chat.py) + mock live
# on the porting-sdk `ai-chat-client` branch, so during the coordinated pass
# PORTING_SDK_REF pins that branch and the gate runs; on plain main it skip-passes
# until the branch merges. The dump binary is built by the TEST gate; resolve its
# path per BUILD_MODE exactly like the other dump gates (host binary / docker exec /
# throwaway docker run that rebuilds it).
ai_chat_gate() {
    if [ ! -f "$PORTING_SDK_DIR/scripts/diff_port_ai_chat.py" ]; then
        echo "[ai-chat] diff_port_ai_chat.py not on porting-sdk main yet — skip-pass (coordinated-branch dep: porting-sdk ai-chat-client)"
        return 0
    fi
    case "$BUILD_MODE" in
        host)
            python3 "$PORTING_SDK_DIR/scripts/diff_port_ai_chat.py" --port cpp \
                --dump-cmd "$PORT_ROOT/build/ai_chat_dump"
            ;;
        exec:*)
            local c="${BUILD_MODE#exec:}"
            python3 "$PORTING_SDK_DIR/scripts/diff_port_ai_chat.py" --port cpp \
                --dump-cmd "docker exec -e MOCK_AI_CHAT_URL -e SIGNALWIRE_PROJECT_ID -e SIGNALWIRE_API_TOKEN $c $SWCPP_CONTAINER_BUILD/ai_chat_dump"
            ;;
        run:*)
            local img="${BUILD_MODE#run:}"
            python3 "$PORTING_SDK_DIR/scripts/diff_port_ai_chat.py" --port cpp \
                --dump-cmd "docker run --rm --network host -e MOCK_AI_CHAT_URL -e SIGNALWIRE_PROJECT_ID -e SIGNALWIRE_API_TOKEN -v $(dirname "$PORT_ROOT"):/src $img bash -c 'cmake -S $SWCPP_CONTAINER_REPO -B $SWCPP_CONTAINER_BUILD -DCMAKE_BUILD_TYPE=Release >&2 && cmake --build $SWCPP_CONTAINER_BUILD --target ai_chat_dump -j\"\$(nproc)\" >&2 && $SWCPP_CONTAINER_BUILD/ai_chat_dump'"
            ;;
        *)
            echo "unknown BUILD_MODE: $BUILD_MODE"; return 1 ;;
    esac
}
run_gate "AI-CHAT" "AIChatClient speaks the AI Chat protocol per the vendored spec (mock_ai_chat wire-behavioral)" \
    ai_chat_gate

# WIRED-MODES (plan 1.6 / D7): the merge-coherence guard. WIRED_MODES.md lists the
# load-bearing env/mode lines this run-ci MUST carry (MOCK_RELAY_STRICT=1, the
# strict_mocks_gate body, the MOCK_SIGNALWIRE_STRICT export). The strict-mocks ×
# Part-5 merge race dropped exactly such a line here (the strict_mocks_gate body,
# round-4 nightly exit 127); if a future merge drops one again, this gate reds
# instead of shipping a green-but-vacuous strict lane.
# GUARDED: check_wired_modes.py ships on the porting-sdk plan branch. CI pins that
# branch (workflows: `ref: plan/a-bar-2026-07-18`), so CI takes the real path; the
# skip-with-pass guard only covers a local sibling still on a pre-plan main.
# Remove the guard once the engine is on porting-sdk main.
wired_modes_gate() {
    if [ -f "$PORTING_SDK_DIR/scripts/check_wired_modes.py" ]; then
        python3 "$PORTING_SDK_DIR/scripts/check_wired_modes.py" --port cpp --repo "$PORT_ROOT"
    else
        echo "[wired-modes] check_wired_modes.py not on porting-sdk main yet — skip-pass (plan-branch dep)"
    fi
}
run_gate "WIRED-MODES" "load-bearing run-ci modes present (WIRED_MODES.md merge-coherence guard)" \
    wired_modes_gate

# DOC-SURFACE (plan §6.3): doxygen-header coverage floor on the public surface. The
# floor is pinned in .doc_surface_floor (90.2% today) and ratchets up via
# --write-floor; report-only at graduation, so a doc regression is visible without
# failing the run yet (never-regress is enforced once the floor flips blocking).
# GUARDED like WIRED-MODES: doc_surface.py is a porting-sdk plan-branch dep.
doc_surface_gate() {
    if [ -f "$PORTING_SDK_DIR/scripts/doc_surface.py" ]; then
        python3 "$PORTING_SDK_DIR/scripts/doc_surface.py" --port cpp --repo "$PORT_ROOT" --report-only
    else
        echo "[doc-surface] doc_surface.py not on porting-sdk main yet — skip-pass (plan-branch dep)"
    fi
}
run_gate "DOC-SURFACE" "public doc-comment coverage floor (.doc_surface_floor ratchet; report-only)" \
    doc_surface_gate

# GATE-INVENTORY NOTE (plan §2.16): porting-sdk/GATE_INVENTORY.md is generated by
# gen_gate_inventory.py from the REFERENCE port's run-ci.sh (typescript — the
# canonical copy every port mirrors), so the gates in THIS file that are
# CPP-SPECIFIC do NOT appear in that generated inventory and that is intentional,
# not drift:
#   * WIRED-MODES / DOC-SURFACE — cpp carries load-bearing strict-mode exports and
#     a doxygen-header coverage floor the ts reference does not have (yet).
#   * BEHAVIORAL-WIRE-RELAY — cpp's hyphen spelling of the RELAY behavioral rule.
#   * the STRICT-MOCKS line runs via cpp's strict_mocks_gate body (BUILD_MODE
#     routing: host/exec/run) and MOCK_SIGNALWIRE_STRICT=1 is exported for the
#     REST lanes (D3) — both declared in WIRED_MODES.md so a merge can't silently
#     drop them (that exact drop happened in the strict-mocks × Part-5 race).
#   * cpp runs gates SERIALLY via run_gate, not the DAG gate_scheduler.sh —
#     tier=nightly maps to run_gate's leading --tier=nightly.

# --- §C1 doc/example execution gates -----------------------------------------
# SNIPPET-COMPILE (heavy, ~11min) is nightly; DOC-CLI stays per-PR (cheap
# CLI-parse). EXAMPLES-RUN/SNIPPET-RUN self-skip on cpp but stay in the nightly
# tier for a uniform full-doc sweep. STRICT-MOCKS (a second full strict RELAY
# pass) is nightly.
run_gate "SNIPPET-COMPILE" "documented code snippets compile" --tier=nightly \
    python3 "$PORTING_SDK_DIR/scripts/snippet_compile.py" --port cpp --repo .

run_gate "DOC-CLI" "documented swaig-test invocations parse against the real CLI" \
    python3 "$PORTING_SDK_DIR/scripts/doc_cli.py" --port cpp --repo .

run_gate "STRICT-MOCKS" "RELAY suite passes with the mock in 400-on-violation strict mode (MOCK_RELAY_STRICT=1)" --tier=nightly \
    strict_mocks_gate

run_gate "EXAMPLES-RUN" "shipped examples load/start against the mock (modulo EXAMPLES_RUN_ALLOW.md)" --tier=nightly \
    python3 "$PORTING_SDK_DIR/scripts/examples_run.py" --port cpp --repo .

run_gate "SNIPPET-RUN" "dynamic-port doc snippets run to a zero exit against the mock (compiled port: self-skips)" --tier=nightly \
    python3 "$PORTING_SDK_DIR/scripts/snippet_run.py" --port cpp --repo . --report-only

if [ -z "$FAILED_GATES" ]; then
    echo "==> CI PASS"
    exit 0
else
    echo "==> CI FAIL (gates:$FAILED_GATES )"
    exit 1
fi
