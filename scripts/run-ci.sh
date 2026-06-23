#!/usr/bin/env bash
# run-ci.sh — canonical local-and-CI gate runner for signalwire-cpp.
#
# Same script invoked locally (`bash scripts/run-ci.sh`) AND by the
# GitHub Actions workflow. No drift between local and CI behavior.
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
PORT_NAME="signalwire-cpp"

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
pick_free_port() {
    python3 - <<'PY' || return 1
import socket, sys
s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
try:
    s.bind(("127.0.0.1", 0))
    print(s.getsockname()[1])
except OSError as e:
    sys.stderr.write("pick_free_port: %s\n" % e)
    sys.exit(1)
finally:
    s.close()
PY
}

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

FAILED_GATES=""

run_gate() {
    local name="$1"; shift
    local description="$1"; shift
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
SWCPP_CONTAINER_BUILD="${SWCPP_CONTAINER_BUILD:-/tmp/run-ci-build}"

# Build run_tests + emit_corpus + emit_skills + execute run_tests, honoring
# BUILD_MODE. Each branch produces the same observable result: a built run_tests
# (+ emit_corpus, reused by the EMISSION gate; + emit_skills, reused by the
# SKILL-CONTRACT gate) and run_tests' exit code. The two dump binaries are built
# here too so the downstream gates don't reconfigure the tree (and so the host /
# exec modes leave ready-to-run binaries).
test_gate() {
    case "$BUILD_MODE" in
        host)
            if [ ! -d build ]; then
                echo "[BOOTSTRAP] cmake -S . -B build (initial configure)"
                cmake -S . -B build || return 1
            fi
            cmake --build build --target run_tests emit_corpus emit_skills -j && ./build/run_tests
            ;;
        exec:*)
            local c="${BUILD_MODE#exec:}"
            docker exec "$c" bash -c "
                cmake -S '$SWCPP_CONTAINER_REPO' -B '$SWCPP_CONTAINER_BUILD' -DCMAKE_BUILD_TYPE=Release \
                && cmake --build '$SWCPP_CONTAINER_BUILD' --target run_tests emit_corpus emit_skills -j\"\$(nproc)\" \
                && '$SWCPP_CONTAINER_BUILD/run_tests'"
            ;;
        run:*)
            local img="${BUILD_MODE#run:}"
            # Mount the repo's PARENT (so porting-sdk is adjacent for the mock
            # adjacency walk) and use --network host to reach host-run mocks.
            docker run --rm --network host -v "$(dirname "$PORT_ROOT")":/src "$img" bash -c "
                cmake -S '$SWCPP_CONTAINER_REPO' -B '$SWCPP_CONTAINER_BUILD' -DCMAKE_BUILD_TYPE=Release \
                && cmake --build '$SWCPP_CONTAINER_BUILD' --target run_tests emit_corpus emit_skills -j\"\$(nproc)\" \
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
emission_gate() {
    local dump_cmd
    case "$BUILD_MODE" in
        host)
            dump_cmd="$PORT_ROOT/build/emit_corpus"
            ;;
        exec:*)
            local c="${BUILD_MODE#exec:}"
            dump_cmd="docker exec $c $SWCPP_CONTAINER_BUILD/emit_corpus"
            ;;
        run:*)
            local img="${BUILD_MODE#run:}"
            dump_cmd="docker run --rm -v $(dirname "$PORT_ROOT"):/src $img bash -c \
                'cmake -S $SWCPP_CONTAINER_REPO -B $SWCPP_CONTAINER_BUILD -DCMAKE_BUILD_TYPE=Release 1>&2 \
                 && cmake --build $SWCPP_CONTAINER_BUILD --target emit_corpus -j\$(nproc) 1>&2 \
                 && exec $SWCPP_CONTAINER_BUILD/emit_corpus'"
            ;;
        *)
            echo "unknown BUILD_MODE: $BUILD_MODE"; return 1 ;;
    esac
    python3 "$PORTING_SDK_DIR/scripts/diff_port_emission.py" --dump-cmd "$dump_cmd"
}

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
surface_fresh_gate() {
    # 1. snapshot the committed surface (fallback cp if not tracked at HEAD).
    if ! git show HEAD:port_surface.json > /tmp/committed_surface.json 2>/dev/null; then
        cp port_surface.json /tmp/committed_surface.json || return 1
    fi
    # 2. regenerate in place via the same host enumerator the SIGNATURES gate uses.
    python3 scripts/enumerate_surface.py || { git checkout -- port_surface.json 2>/dev/null; return 1; }
    # 3. compare committed vs fresh, ignoring the volatile generated_from sha.
    python3 "$PORTING_SDK_DIR/scripts/check_surface_freshness.py" \
        --committed /tmp/committed_surface.json --fresh port_surface.json
    local rc=$?
    # 4. always restore the working copy (regen rewrote the git-sha provenance).
    git checkout -- port_surface.json 2>/dev/null
    return $rc
}

# SURFACE-DIFF gate: diff the port's public surface against the Python reference
# (membership: omissions + additions). The signature DRIFT gate (Layer A) checks
# method *signatures*; this checks surface *membership* — public symbols the port
# has that Python doesn't and vice-versa. Like SURFACE-FRESH it regenerates
# port_surface.json in place via the host regex enumerator (no compiler / no
# mocks / BUILD_MODE-independent), diffs, then restores the committed copy
# unconditionally so the gate is side-effect free.
surface_diff_gate() {
    if ! git show HEAD:port_surface.json > /tmp/committed_surface_diff.json 2>/dev/null; then
        cp port_surface.json /tmp/committed_surface_diff.json || return 1
    fi
    python3 scripts/enumerate_surface.py || { git checkout -- port_surface.json 2>/dev/null; return 1; }
    python3 "$PORTING_SDK_DIR/scripts/diff_port_surface.py" \
        --reference "$PORTING_SDK_DIR/python_surface.json" \
        --port-surface port_surface.json \
        --omissions "$PORT_ROOT/PORT_OMISSIONS.md" \
        --additions "$PORT_ROOT/PORT_ADDITIONS.md"
    local rc=$?
    git checkout -- port_surface.json 2>/dev/null
    return $rc
}

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
skill_contract_gate() {
    local dump_cmd
    case "$BUILD_MODE" in
        host)
            dump_cmd="$PORT_ROOT/build/emit_skills"
            ;;
        exec:*)
            local c="${BUILD_MODE#exec:}"
            dump_cmd="docker exec $c $SWCPP_CONTAINER_BUILD/emit_skills"
            ;;
        run:*)
            local img="${BUILD_MODE#run:}"
            dump_cmd="docker run --rm -v $(dirname "$PORT_ROOT"):/src $img bash -c \
                'cmake -S $SWCPP_CONTAINER_REPO -B $SWCPP_CONTAINER_BUILD -DCMAKE_BUILD_TYPE=Release 1>&2 \
                 && cmake --build $SWCPP_CONTAINER_BUILD --target emit_skills -j\$(nproc) 1>&2 \
                 && exec $SWCPP_CONTAINER_BUILD/emit_skills'"
            ;;
        *)
            echo "unknown BUILD_MODE: $BUILD_MODE"; return 1 ;;
    esac
    python3 "$PORTING_SDK_DIR/scripts/diff_skill_contracts.py" \
        --dump-cmd "$dump_cmd" --port-repo "$PORT_ROOT"
}

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
rest_coverage_gate() {
    local port
    port="$(pick_free_port)" || { echo "rest_coverage_gate: could not allocate a free port"; return 1; }
    local mock_pkg_parent="$PORTING_SDK_DIR/test_harness/mock_signalwire"
    export PYTHONPATH="$mock_pkg_parent${PYTHONPATH:+:$PYTHONPATH}"
    local mock_log="/tmp/rest_cov_mock_cpp.$$.log"
    python3 -m mock_signalwire --host 127.0.0.1 --port "$port" --log-level error \
        >"$mock_log" 2>&1 &
    local mock_pid=$!
    # shellcheck disable=SC2064
    trap "kill $mock_pid 2>/dev/null" RETURN
    local i healthy=0
    for i in $(seq 1 60); do
        # Fail loud if the mock process died (e.g. port stolen between
        # pick_free_port and bind) instead of polling a dead pid for 30s.
        if ! kill -0 "$mock_pid" 2>/dev/null; then
            echo "rest_coverage_gate: mock_signalwire (pid $mock_pid) exited before becoming healthy on port $port; log follows:"
            cat "$mock_log" >&2 || true
            return 1
        fi
        if python3 -c "import urllib.request; urllib.request.urlopen('http://127.0.0.1:$port/__mock__/health',timeout=1)" 2>/dev/null; then
            healthy=1
            break
        fi
        sleep 0.5
    done
    if [ "$healthy" -ne 1 ]; then
        echo "rest_coverage_gate: mock_signalwire never became healthy on port $port within 30s; log follows:"
        cat "$mock_log" >&2 || true
        return 1
    fi
    # Fresh journal + scenarios so only this run's traffic is measured.
    python3 -c "import urllib.request; urllib.request.urlopen(urllib.request.Request('http://127.0.0.1:$port/__mock__/journal/reset',method='POST'),timeout=5).read()"
    python3 -c "import urllib.request; urllib.request.urlopen(urllib.request.Request('http://127.0.0.1:$port/__mock__/scenarios/reset',method='POST'),timeout=5).read()"
    # Run the REST suite serially (one thread => one ordered journal) against the
    # mock, filtered to the mock-backed rest cases.
    case "$BUILD_MODE" in
        host)
            MOCK_SIGNALWIRE_PORT="$port" SW_TEST_PARALLEL=1 \
                "$PORT_ROOT/build/run_tests" rest_mock_ || return 1
            ;;
        exec:*)
            local c="${BUILD_MODE#exec:}"
            docker exec -e MOCK_SIGNALWIRE_PORT="$port" -e SW_TEST_PARALLEL=1 "$c" \
                "$SWCPP_CONTAINER_BUILD/run_tests" rest_mock_ || return 1
            ;;
        run:*)
            local img="${BUILD_MODE#run:}"
            docker run --rm --network host -v "$(dirname "$PORT_ROOT")":/src \
                -e MOCK_SIGNALWIRE_PORT="$port" -e SW_TEST_PARALLEL=1 "$img" bash -c "
                cmake -S '$SWCPP_CONTAINER_REPO' -B '$SWCPP_CONTAINER_BUILD' -DCMAKE_BUILD_TYPE=Release 1>&2 \
                && cmake --build '$SWCPP_CONTAINER_BUILD' --target run_tests -j\"\$(nproc)\" 1>&2 \
                && '$SWCPP_CONTAINER_BUILD/run_tests' rest_mock_" || return 1
            ;;
        *)
            echo "unknown BUILD_MODE: $BUILD_MODE"; return 1 ;;
    esac
    python3 -m mock_signalwire.rest_coverage \
        --mock-url "http://127.0.0.1:$port" \
        --spec-root "$PORTING_SDK_DIR/rest-apis" \
        --allowlist "$PORTING_SDK_DIR/REST_COVERAGE_BASELINE.md" \
        --allowlist "$PORT_ROOT/REST_COVERAGE_GAPS.md" \
        --gap-baseline "$PORTING_SDK_DIR/REST_COVERAGE_GAP_BASELINE.md"
}

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
spec_parity_gate() {
    local reg
    reg="$(mktemp -t cpp_route_registry.XXXXXX.json)"
    # shellcheck disable=SC2064
    trap "rm -f '$reg'" RETURN
    case "$BUILD_MODE" in
        host)
            # The TEST gate builds run_tests/emit_corpus/emit_skills but not
            # route_registry, so build it here before invoking it.
            cmake --build "$PORT_ROOT/build" --target route_registry -j 1>&2 || return 1
            "$PORT_ROOT/build/route_registry" >"$reg" || return 1
            ;;
        exec:*)
            local c="${BUILD_MODE#exec:}"
            docker exec "$c" "$SWCPP_CONTAINER_BUILD/route_registry" >"$reg" || return 1
            ;;
        run:*)
            local img="${BUILD_MODE#run:}"
            docker run --rm -v "$(dirname "$PORT_ROOT")":/src "$img" bash -c "
                cmake -S '$SWCPP_CONTAINER_REPO' -B '$SWCPP_CONTAINER_BUILD' -DCMAKE_BUILD_TYPE=Release 1>&2 \
                && cmake --build '$SWCPP_CONTAINER_BUILD' --target route_registry -j\"\$(nproc)\" 1>&2 \
                && exec '$SWCPP_CONTAINER_BUILD/route_registry'" >"$reg" || return 1
            ;;
        *)
            echo "unknown BUILD_MODE: $BUILD_MODE"; return 1 ;;
    esac
    python3 "$PORTING_SDK_DIR/scripts/diff_spec_implementation.py" \
        --registry-json "$reg" \
        --gaps "$PORTING_SDK_DIR/SPEC_IMPLEMENTATION_GAPS.md"
}

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
fmt_sources() {
    find src include tools -type f \
        \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' -o -name '*.cc' \) \
        2>/dev/null
}
fmt_gate() {
    local files
    files="$(fmt_sources)"
    [ -n "$files" ] || { echo "no C++ sources found to format" >&2; return 1; }
    if [ -n "${CI:-}" ]; then
        # shellcheck disable=SC2086
        clang-format --dry-run -Werror $files
    else
        # shellcheck disable=SC2086
        clang-format -i $files
        if ! git diff --quiet 2>/dev/null; then
            echo "    (FMT auto-applied formatting to your working tree — review & stage)"
        fi
        # A residual issue -i can't fix must still fail the gate.
        # shellcheck disable=SC2086
        clang-format --dry-run -Werror $files
    fi
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
lint_gate() {
    local ct tidy_build cxx cc
    ct="$(resolve_clang_tidy)" || { echo "no clang-tidy found" >&2; return 1; }
    tidy_build="${SWCPP_TIDY_BUILD:-}"
    if [ -z "$tidy_build" ]; then
        # clang-tidy must read a CLANG-generated compile DB. Reuse an existing
        # build-tidy (clang-built by construction below); otherwise generate one
        # with the clang matching the chosen clang-tidy. Do NOT fall back to the
        # plain build/ dir — the TEST gate builds that with g++ (CI) whose
        # compile_commands carries g++-specific flags + system-header paths that
        # clang-tidy can't parse as C++17 (it falls back to a pre-C++17 parse,
        # yielding bogus "no template named 'optional'" / "decomposition
        # declarations are a C++17 extension" errors). Always use clang's DB.
        if [ -f build-tidy/compile_commands.json ]; then
            tidy_build="build-tidy"
        else
            cxx="$(dirname "$ct")/clang++"; cc="$(dirname "$ct")/clang"
            # When clang-tidy resolved to a bare name on PATH, dirname is "." and
            # the sibling clang++/clang may not exist there — fall back to the
            # PATH-resolved clang++/clang so CMake still gets a clang compiler.
            [ -x "$cxx" ] || cxx="$(command -v clang++ || true)"
            [ -x "$cc" ] || cc="$(command -v clang || true)"
            local cmake_args=(-S . -B build-tidy -DCMAKE_EXPORT_COMPILE_COMMANDS=ON)
            [ -n "$cxx" ] && cmake_args+=(-DCMAKE_CXX_COMPILER="$cxx")
            [ -n "$cc" ] && cmake_args+=(-DCMAKE_C_COMPILER="$cc")
            cmake "${cmake_args[@]}" >&2 || return 1
            tidy_build="build-tidy"
        fi
    fi
    local files
    files="$(find src include -name '*.cpp')"
    [ -n "$files" ] || { echo "no C++ sources found to lint" >&2; return 1; }
    # shellcheck disable=SC2086
    "$ct" -p "$tidy_build" --header-filter='signalwire-cpp/(src|include)/' --quiet $files
}

# Gate 1: build + run tests (host or OpenSSL-3.0 container per BUILD_MODE)
run_gate "TEST" "build run_tests + run_tests ($BUILD_MODE)" test_gate

# Gate 2: signature regen
run_gate "SIGNATURES" "regenerate port_signatures.json (libclang)" \
    python3 scripts/enumerate_signatures.py

# Gate 3: drift gate
run_gate "DRIFT" "diff_port_signatures vs python reference" \
    python3 "$PORTING_SDK_DIR/scripts/diff_port_signatures.py" \
        --reference "$PORTING_SDK_DIR/python_signatures.json" \
        --port-signatures "$PORT_ROOT/port_signatures.json" \
        --surface-omissions "$PORT_ROOT/PORT_OMISSIONS.md" \
        --surface-additions "$PORT_ROOT/PORT_ADDITIONS.md" \
        --omissions "$PORT_ROOT/PORT_SIGNATURE_OMISSIONS.md"

# Gate 4: surface-fresh — committed port_surface.json must match a fresh regen
run_gate "SURFACE-FRESH" "check_surface_freshness vs regenerated port_surface.json" \
    surface_fresh_gate

# Gate 5: no-cheat
run_gate "NO-CHEAT" "audit_no_cheat_tests" \
    python3 "$PORTING_SDK_DIR/scripts/audit_no_cheat_tests.py" --root "$PORT_ROOT"

# Gate 5b: REST-COVERAGE — every implemented REST route covered success+error
# (parity + allowlist). Runs after NO-CHEAT.
run_gate "REST-COVERAGE" "every implemented REST route covered success+error (parity + allowlist)" \
    rest_coverage_gate

# Gate 5c: SPEC-PARITY — implemented REST routes == canonical spec (modulo gaps);
# deterministic Set B from the route_registry binary.
run_gate "SPEC-PARITY" "implemented REST routes == canonical spec (modulo gaps); deterministic Set B" \
    spec_parity_gate

# Gate 6: emission — byte-compare FunctionResult serialisation vs Python oracle
run_gate "EMISSION" "diff_port_emission vs python to_dict() (81-entry corpus)" \
    emission_gate

# Gate 7: FMT — clang-format (local: apply in place; CI: --dry-run -Werror)
run_gate "FMT" "clang-format (.clang-format; local: apply, CI: check)" fmt_gate

# Gate 8: LINT — clang-tidy curated set burned to zero (WarningsAsErrors:'*')
run_gate "LINT" "clang-tidy curated set, zero findings" lint_gate

# Gate 9: DOC-AUDIT — every symbol referenced in docs/ + examples/ fenced code
# blocks must resolve to a real symbol in the committed port_surface.json (the
# SURFACE-FRESH gate above already proved it fresh). DOC_AUDIT_IGNORE.md lists
# intentional non-symbol references. Pure python; host-side, BUILD_MODE-blind.
run_gate "DOC-AUDIT" "audit_docs vs port_surface.json" \
    python3 "$PORTING_SDK_DIR/scripts/audit_docs.py" \
        --root "$PORT_ROOT" \
        --surface "$PORT_ROOT/port_surface.json" \
        --ignore "$PORT_ROOT/DOC_AUDIT_IGNORE.md"

# Gate 10: SURFACE-DIFF — diff public surface membership vs the Python reference
run_gate "SURFACE-DIFF" "diff_port_surface vs python reference" \
    surface_diff_gate

# Gate 11: SKILL-CONTRACT — diff each built-in skill's SWAIG tool contract vs
# the Python reference (emit_skills built in the TEST gate)
run_gate "SKILL-CONTRACT" "diff_skill_contracts vs python reference" \
    skill_contract_gate

# Gate 12: SWAIG-CLI — lightweight shared swaig-test mini-contract (NOT python
# parity; python's in-process simulator surface is reference-only). Black-box:
# invokes `bin/swaig-test --help` + golden invocations and asserts the shared
# verbs are documented and no-action errors (the cross-port majority default).
# The C++ swaig-test is a shell script and takes its URL as a POSITIONAL
# <agent-url> rather than --url, so the default-action probe uses the positional
# form (the gate doesn't require a literal --url token). No --simulate-serverless,
# so the no-serverless clause asserts the flag is rejected as an unknown option.
run_gate "SWAIG-CLI" "swaig-test shared mini-contract (verbs/serverless-reject/default-action)" \
    python3 "$PORTING_SDK_DIR/scripts/audit_swaig_cli_contract.py" \
        --port cpp \
        --cmd "bash $PORT_ROOT/bin/swaig-test" \
        --require-url-model \
        --default-action-argv='http://user:pass@127.0.0.1:1/' \
        --no-serverless-argv='http://user:pass@127.0.0.1:1/|--simulate-serverless|lambda|--list-tools'

if [ -z "$FAILED_GATES" ]; then
    echo "==> CI PASS"
    exit 0
else
    echo "==> CI FAIL (gates:$FAILED_GATES )"
    exit 1
fi
