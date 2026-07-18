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

# STRICT-MOCKS 400-mode (plan §2.2c): strict is the default now. The REST mock
# (mock_signalwire) 400s on any wire_violation and the RELAY mock (mock_relay)
# rejects any unknown-field/duplicate-id frame -- so every mock consumer this run
# spawns inherits wire-truth directly, not only the gates that read the journal.
# Override to 0 to debug in flag-mode.
export MOCK_SIGNALWIRE_STRICT="${MOCK_SIGNALWIRE_STRICT:-1}"
export MOCK_RELAY_STRICT="${MOCK_RELAY_STRICT:-1}"

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
# state_dump/http_dump/wire_relay_dump, reused by the BEHAVIORAL-* gates) and
# run_tests' exit code. The dump binaries are built here too so the downstream
# gates don't reconfigure the tree (and so the host / exec modes leave
# ready-to-run binaries).
test_gate() {
    # STRICT-MOCKS (Sec 2.2b parity): carry MOCK_RELAY_STRICT=1 into the full
    # run_tests pass, not just the nightly relay_mock_-filtered strict_mocks_gate.
    # cpp's relay tests self-spawn `python -m mock_relay` via fork+execlp, which
    # inherits ambient env, so exporting it here reaches the child mock the same
    # way strict_mocks_gate does.
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
            cmake --build build --target emit_corpus emit_skills \
                wire_dump swml_dump state_dump http_dump wire_relay_dump -j"$(sw_build_jobs)" || return 1
            env MOCK_RELAY_STRICT=1 bash "$PORT_ROOT/scripts/run-tests.sh"
            ;;
        exec:*)
            local c="${BUILD_MODE#exec:}"
            docker exec -e MOCK_RELAY_STRICT=1 "$c" bash -c "
                cmake -S '$SWCPP_CONTAINER_REPO' -B '$SWCPP_CONTAINER_BUILD' -DCMAKE_BUILD_TYPE=Release \
                && cmake --build '$SWCPP_CONTAINER_BUILD' --target run_tests emit_corpus emit_skills wire_dump swml_dump state_dump http_dump wire_relay_dump -j\"\$(nproc)\" \
                && '$SWCPP_CONTAINER_BUILD/run_tests'"
            ;;
        run:*)
            local img="${BUILD_MODE#run:}"
            # Mount the repo's PARENT (so porting-sdk is adjacent for the mock
            # adjacency walk) and use --network host to reach host-run mocks.
            docker run --rm --network host -v "$(dirname "$PORT_ROOT")":/src -e MOCK_RELAY_STRICT=1 "$img" bash -c "
                cmake -S '$SWCPP_CONTAINER_REPO' -B '$SWCPP_CONTAINER_BUILD' -DCMAKE_BUILD_TYPE=Release \
                && cmake --build '$SWCPP_CONTAINER_BUILD' --target run_tests emit_corpus emit_skills wire_dump swml_dump state_dump http_dump wire_relay_dump -j\"\$(nproc)\" \
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
behavioral_gate() {
    local surface="$1"  # wire | swml | state | http | wire_relay
    local dump_cmd
    case "$BUILD_MODE" in
        host)
            dump_cmd="$PORT_ROOT/build/${surface}_dump"
            ;;
        exec:*)
            local c="${BUILD_MODE#exec:}"
            dump_cmd="docker exec $c $SWCPP_CONTAINER_BUILD/${surface}_dump"
            ;;
        run:*)
            local img="${BUILD_MODE#run:}"
            dump_cmd="docker run --rm -v $(dirname "$PORT_ROOT"):/src $img bash -c \
                'cmake -S $SWCPP_CONTAINER_REPO -B $SWCPP_CONTAINER_BUILD -DCMAKE_BUILD_TYPE=Release 1>&2 \
                 && cmake --build $SWCPP_CONTAINER_BUILD --target ${surface}_dump -j\$(nproc) 1>&2 \
                 && exec $SWCPP_CONTAINER_BUILD/${surface}_dump'"
            ;;
        *)
            echo "unknown BUILD_MODE: $BUILD_MODE"; return 1 ;;
    esac
    python3 "$PORTING_SDK_DIR/scripts/diff_port_${surface}.py" \
        --port cpp \
        --python-sdk "$PYTHON_SDK_DIR" \
        --dump-cmd "$dump_cmd"
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
# Cache of the freshly-regenerated port_surface.json so SURFACE-FRESH and
# SURFACE-DIFF (which both otherwise re-run enumerate_surface.py from scratch)
# share ONE regeneration. The enumerator is deterministic on a clean tree, so a
# single regen serves both gates. The path is derived INSIDE each gate from
# $PORT_ROOT (not a shared top-level var) so the gates stay self-contained when a
# harness like porting-sdk's sw-verify sources only the function definitions and
# calls one gate directly. Cache lives under the repo-local .sw-tmp (gitignored) —
# never /tmp.
_fresh_surface_cache_path() { echo "$PORT_ROOT/.sw-tmp/fresh_port_surface.json"; }

surface_fresh_gate() {
    local cache; cache="$(_fresh_surface_cache_path)"
    mkdir -p "$(dirname "$cache")"
    rm -f "$cache"
    # 1. snapshot the committed surface (fallback cp if not tracked at HEAD).
    if ! git show HEAD:port_surface.json > "$PORT_ROOT/.sw-tmp/committed_surface.json" 2>/dev/null; then
        cp port_surface.json "$PORT_ROOT/.sw-tmp/committed_surface.json" || return 1
    fi
    # 2. regenerate in place via the same host enumerator the SIGNATURES gate
    #    uses, caching the fresh copy for SURFACE-DIFF to reuse.
    python3 scripts/enumerate_surface.py || { git checkout -- port_surface.json 2>/dev/null; return 1; }
    cp port_surface.json "$cache" 2>/dev/null || true
    # 3. compare committed vs fresh, ignoring the volatile generated_from sha.
    python3 "$PORTING_SDK_DIR/scripts/check_surface_freshness.py" \
        --committed "$PORT_ROOT/.sw-tmp/committed_surface.json" --fresh port_surface.json
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
    # Reuse the fresh surface SURFACE-FRESH already regenerated this run (the
    # enumerator is deterministic on a clean tree, so a second regen would be
    # byte-identical). Fall back to regenerating if the cache is absent (e.g.
    # SURFACE-FRESH was skipped or this gate is run standalone). diff_port_surface
    # reads the file at ./port_surface.json, so point that at the cached fresh
    # copy for the diff, then restore the committed working copy.
    local cache; cache="$(_fresh_surface_cache_path)"
    if ! git show HEAD:port_surface.json > "$PORT_ROOT/.sw-tmp/committed_surface_diff.json" 2>/dev/null; then
        cp port_surface.json "$PORT_ROOT/.sw-tmp/committed_surface_diff.json" || return 1
    fi
    if [ -f "$cache" ]; then
        cp "$cache" port_surface.json || { git checkout -- port_surface.json 2>/dev/null; return 1; }
    else
        python3 scripts/enumerate_surface.py || { git checkout -- port_surface.json 2>/dev/null; return 1; }
    fi
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
    local mock_log="$PORT_ROOT/.sw-tmp/rest_cov_mock_cpp.$$.log"
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
        --gap-baseline "$PORTING_SDK_DIR/REST_COVERAGE_GAP_BASELINE.md" || return 1
    # STRICT-MOCKS section 2.2a -- fail the gate on ANY journaled wire_violation. The shared
    # helper reads the same live mock journal this coverage run just populated and
    # exits non-zero on any offender (porting-sdk/scripts/assert_no_wire_violations.py).
    # WIRE_VIOLATIONS_ALLOW.md holds ONLY owner-signed spec-gap parks.
    python3 "$PORTING_SDK_DIR/scripts/assert_no_wire_violations.py" \
        --rest-mock-url "http://127.0.0.1:$port" \
        --allowlist "$PORT_ROOT/WIRE_VIOLATIONS_ALLOW.md"
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
            cmake --build "$PORT_ROOT/build" --target route_registry -j"$(sw_build_jobs)" 1>&2 || return 1
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

# ROUTE-COLLISION gate: cross-references the route-registry (operation ->
# (method, path)) with the surface enumeration to find latent route-split /
# crud-dup / orphan-dto defects. cpp HAS a registry (the route_registry binary),
# so — unlike ports without one — this gate can run standalone. It is built +
# run per BUILD_MODE exactly like spec_parity_gate above (host binary / docker
# exec / throwaway docker run that rebuilds it), then the collision checker
# reads the registry. The 2 fabric list_addresses route-split entries are the
# user-approved ROUTE_COLLISION_ALLOW.md exceptions (392bb5b); orphan-dto is
# report-only inside the gate. Enforcing (no --report-only).
route_collision_gate() {
    local reg
    reg="$(mktemp -t cpp_route_registry.XXXXXX.json)"
    # shellcheck disable=SC2064
    trap "rm -f '$reg'" RETURN
    case "$BUILD_MODE" in
        host)
            cmake --build "$PORT_ROOT/build" --target route_registry -j"$(sw_build_jobs)" 1>&2 || return 1
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
    python3 "$PORTING_SDK_DIR/scripts/route_collision.py" \
        --port cpp --repo . --registry-json "$reg"
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

# --- gate-enforcement quartet (§2.1-2.4) --------------------------------------
# DOC-WIRE (§2.1): doc_wire.py spawns the mock in FLAG mode, exports
# MOCK_SIGNALWIRE_PORT, runs the doc_wire_dump binary (built by the TEST gate; it
# points a RestClient at the mock and replays the documented REST call SHAPES),
# then reads the mock's wire_violations journal. The runner is resolved per
# BUILD_MODE like the other dump binaries (host binary / docker exec / throwaway
# run). Per-PR: a single quick replay.
doc_wire_gate() {
    local dump_cmd
    case "$BUILD_MODE" in
        host)   dump_cmd="$PORT_ROOT/build/doc_wire_dump" ;;
        exec:*) dump_cmd="docker exec ${BUILD_MODE#exec:} $SWCPP_CONTAINER_BUILD/doc_wire_dump" ;;
        run:*)  local img="${BUILD_MODE#run:}"
                dump_cmd="docker run --rm --network host -v $(dirname "$PORT_ROOT"):/src $img bash -c \
                    'cmake -S $SWCPP_CONTAINER_REPO -B $SWCPP_CONTAINER_BUILD -DCMAKE_BUILD_TYPE=Release 1>&2 \
                     && cmake --build $SWCPP_CONTAINER_BUILD --target doc_wire_dump -j\$(nproc) 1>&2 \
                     && exec $SWCPP_CONTAINER_BUILD/doc_wire_dump'" ;;
        *) echo "unknown BUILD_MODE: $BUILD_MODE"; return 1 ;;
    esac
    # host: build the runner if the TEST gate hasn't (it's not in the TEST target set).
    if [ "$BUILD_MODE" = host ]; then
        cmake --build "$PORT_ROOT/build" --target doc_wire_dump -j"$(sw_build_jobs)" 1>&2 || return 1
    fi
    python3 "$PORTING_SDK_DIR/scripts/doc_wire.py" --port cpp --repo "$PORT_ROOT" \
        --runner "$dump_cmd"
}

# WAIT-LIVENESS (§2.4): the RELAY Action::wait() liveness contract — wait() BLOCKS
# until the deferred completing event arrives (never a no-op at t~=0, never a hang).
# wait_liveness_dump drives a live mock_relay via the relay_mocktest harness, arms
# deferred completions, and emits the classification; the differ compares it to the
# python golden. nightly (a real-time behavioral check). The mock is self-spawned by
# the dump's relay_mocktest harness (adjacency-discovered) — no gate-side mock.
wait_liveness_gate() {
    local dump_cmd
    case "$BUILD_MODE" in
        host)   dump_cmd="$PORT_ROOT/build/wait_liveness_dump" ;;
        exec:*) dump_cmd="docker exec ${BUILD_MODE#exec:} $SWCPP_CONTAINER_BUILD/wait_liveness_dump" ;;
        run:*)  local img="${BUILD_MODE#run:}"
                dump_cmd="docker run --rm --network host -v $(dirname "$PORT_ROOT"):/src $img bash -c \
                    'cmake -S $SWCPP_CONTAINER_REPO -B $SWCPP_CONTAINER_BUILD -DCMAKE_BUILD_TYPE=Release 1>&2 \
                     && cmake --build $SWCPP_CONTAINER_BUILD --target wait_liveness_dump -j\$(nproc) 1>&2 \
                     && exec $SWCPP_CONTAINER_BUILD/wait_liveness_dump'" ;;
        *) echo "unknown BUILD_MODE: $BUILD_MODE"; return 1 ;;
    esac
    if [ "$BUILD_MODE" = host ]; then
        cmake --build "$PORT_ROOT/build" --target wait_liveness_dump -j"$(sw_build_jobs)" 1>&2 || return 1
    fi
    python3 "$PORTING_SDK_DIR/scripts/diff_port_wait_liveness.py" --port cpp \
        --python-sdk "$PYTHON_SDK_DIR" \
        --dump-cmd "$dump_cmd"
}

# STRICT-MOCKS (§2.2): re-run the RELAY mock suite with the mock in STRICT mode
# (MOCK_RELAY_STRICT=1 → mock_relay 400s an unknown field / duplicate id instead of
# tolerantly journaling it), so a wire-shape regression the tolerant mock would
# swallow fails loud. cpp's relay tests self-spawn `python -m mock_relay` via
# fork+execlp, which INHERITS this env, so exporting MOCK_RELAY_STRICT here reaches
# the child mock. run_tests was built by the TEST gate; resolve it per BUILD_MODE.
# nightly (a second full RELAY pass is heavy).
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

# Gate 1: build + run tests (host or OpenSSL-3.0 container per BUILD_MODE)
# (STRICT-MOCKS: MOCK_RELAY_STRICT=1, wired inside test_gate)
run_gate "TEST" "build run_tests + run_tests ($BUILD_MODE) (STRICT-MOCKS: MOCK_RELAY_STRICT=1)" test_gate

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

# Gate 5d: GEN-FRESH — the generated REST resource layer (headers +
# generated_surface_map.json + rest_signatures.json) must match a fresh
# regeneration (no hand edits to generated files).
run_gate "GEN-FRESH" "generated REST layer byte-identical to a fresh regen" \
    python3 scripts/generate_rest.py --check

# Gate 5d-2: GEN-FRESH-TYPES — the generated read-side TYPE surface (item D/H)
# must match a fresh regeneration. Three generators feed distinct modules:
#   generate_swml_verbs.py     -> core/swml_verbs_generated (schema.json $defs)
#   generate_relay_protocol.py -> relay/protocol_types_generated (relay-protocol/)
#   generate_swaig_payloads.py -> core/{post_prompt,swaig_request,swaig_actions}_generated
# (The <ns>_types_generated REST wire types are covered by generate_rest.py --check.)
run_gate "GEN-FRESH-SWML" "generated SWML-verb type surface byte-identical to a fresh regen" \
    python3 scripts/generate_swml_verbs.py --check
run_gate "GEN-FRESH-RELAY" "generated RELAY-protocol type surface byte-identical to a fresh regen" \
    python3 scripts/generate_relay_protocol.py --check
run_gate "GEN-FRESH-SWAIG" "generated SWAIG read-side payload surface byte-identical to a fresh regen" \
    python3 scripts/generate_swaig_payloads.py --check

# Gate 5e: GEN-FRESH-TESTS — the generated full-mock REST wire-test suite
# (item E) must match a fresh regen from the route_registry plan. route_registry
# was built by the SPEC-PARITY gate above; build it here too for host mode so the
# gate is self-contained, then run it into a temp plan and diff the test files.
gen_fresh_tests_gate() {
    case "$BUILD_MODE" in
        host)
            cmake --build "$PORT_ROOT/build" --target route_registry -j"$(sw_build_jobs)" 1>&2 || return 1
            local reg
            reg="$(mktemp -t cpp_rest_test_plan.XXXXXX.json)"
            # shellcheck disable=SC2064
            trap "rm -f '$reg'" RETURN
            "$PORT_ROOT/build/route_registry" >"$reg" || return 1
            python3 "$PORT_ROOT/scripts/generate_rest_tests.py" --check --registry-json "$reg"
            ;;
        *)
            # Container modes: build + run route_registry in-container, then check.
            local reg
            reg="$(mktemp -t cpp_rest_test_plan.XXXXXX.json)"
            # shellcheck disable=SC2064
            trap "rm -f '$reg'" RETURN
            case "$BUILD_MODE" in
                exec:*)
                    docker exec "${BUILD_MODE#exec:}" "$SWCPP_CONTAINER_BUILD/route_registry" >"$reg" || return 1 ;;
                run:*)
                    docker run --rm -v "$(dirname "$PORT_ROOT")":/src "${BUILD_MODE#run:}" bash -c "
                        cmake -S '$SWCPP_CONTAINER_REPO' -B '$SWCPP_CONTAINER_BUILD' -DCMAKE_BUILD_TYPE=Release 1>&2 \
                        && cmake --build '$SWCPP_CONTAINER_BUILD' --target route_registry -j\"\$(nproc)\" 1>&2 \
                        && exec '$SWCPP_CONTAINER_BUILD/route_registry'" >"$reg" || return 1 ;;
            esac
            python3 "$PORT_ROOT/scripts/generate_rest_tests.py" --check --registry-json "$reg"
            ;;
    esac
}
run_gate "GEN-FRESH-TESTS" "generated REST wire-test suite byte-identical to a fresh regen" \
    gen_fresh_tests_gate

# Gate 6: emission — byte-compare FunctionResult serialisation vs Python oracle
run_gate "EMISSION" "diff_port_emission vs python to_dict() (81-entry corpus)" \
    emission_gate

# Gate 6b: BEHAVIORAL-* (Layer D) — diff each surface's dump against the python
# oracle so the 64 cross-port behaviors can never silently regress. Dump binaries
# built in the TEST gate; each differ builds its golden from signalwire-python.
run_gate "BEHAVIORAL-WIRE" "diff_port_wire vs python oracle (Layer D)" \
    behavioral_gate wire
run_gate "BEHAVIORAL-SWML" "diff_port_swml vs python oracle (Layer D)" \
    behavioral_gate swml
run_gate "BEHAVIORAL-STATE" "diff_port_state vs python oracle (Layer D)" \
    behavioral_gate state
run_gate "BEHAVIORAL-HTTP" "diff_port_http vs python oracle (Layer D)" \
    behavioral_gate http
run_gate "BEHAVIORAL-WIRE-RELAY" "diff_port_wire_relay vs python oracle (Layer D)" \
    behavioral_gate wire_relay

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

# Gate 13: SWAIG-COVERAGE — the C++ FunctionResult must expose every engine
# response action (swaig-specs/swaig-response.yaml) or allowlist it with sign-off
# (SWAIG_PIPELINE §5). The shared checker's C++ scraper (_sdk_emits_cpp, keyed on
# the .cpp suffix) captures ONLY top-level action keys — the fluent add_action(),
# direct single-action object-literal pushes, and subscript-set keys of a var
# pushed onto actions_ — NOT nested payload keys. 2 gaps are signed-off
# allowlisted (back_to_back_functions, user_event).
run_gate "SWAIG-COVERAGE" "FunctionResult exposes every engine response action" \
    python3 "$PORTING_SDK_DIR/scripts/swaig_coverage.py" \
        --check \
        --emission "$PORT_ROOT/src/swaig/function_result.cpp"

# --- Day-one deterministic gates (blocking, non-report-only) ------------------
# Six shared porting-sdk gates that police docs/metadata/artifact hygiene. All
# pure Python, host-side, BUILD_MODE-blind (no compiler / no mocks). Wired here
# with run_gate exactly like the sibling python gates above so they BLOCK CI.
#
# ARTIFACT-DENY runs in git-ls-files PROXY mode: cpp has no CPack/install target
# and no standard package-list tool, so there is no authoritative published-file
# listing to feed `--listing -`. The proxy scans `git ls-files` and passes via
# the port's committed ARTIFACT_DENY_ALLOW.md. (Authoritative --listing mode is
# used by ports whose package tool emits a real file list; cpp is proxy-mode.)

# Gate 14: DOC-LANG-PURITY — no python-verbatim docs in a non-python port
run_gate "DOC-LANG-PURITY" "no python-verbatim docs in a non-python port" \
    python3 "$PORTING_SDK_DIR/scripts/doc_lang_purity.py" --port cpp --repo .

# Gate 15: DOC-LINKS — every relative markdown link resolves to a tracked file
run_gate "DOC-LINKS" "every relative markdown link resolves to a tracked file" \
    python3 "$PORTING_SDK_DIR/scripts/doc_links.py" --port cpp --repo .

run_gate "README-INCLUDE" "doc code blocks are byte-identical to their gate-compiled fixture regions" \
    python3 "$PORTING_SDK_DIR/scripts/readme_include.py" --port cpp --repo .

# Gate 16: ROOT-HYGIENE — no audit/scratch clutter tracked at repo root
#          (allowlist ROOT_HYGIENE_ALLOW.md)
run_gate "ROOT-HYGIENE" "no audit/scratch clutter tracked at repo root (allowlist ROOT_HYGIENE_ALLOW.md)" \
    python3 "$PORTING_SDK_DIR/scripts/root_hygiene.py" --port cpp --repo .

# Gate 17: IGNORE-LEDGER-VERIFY — no laundered false-absence entries in
#          DOC_AUDIT_IGNORE.md
run_gate "IGNORE-LEDGER-VERIFY" "no laundered false-absence entries in DOC_AUDIT_IGNORE.md (--require-fields)" \
    python3 "$PORTING_SDK_DIR/scripts/ignore_ledger_verify.py" --port cpp --repo . --require-fields

# Gate 18: META-CONSISTENT — package metadata consistency
run_gate "META-CONSISTENT" "package metadata consistency" --tier=nightly \
    python3 "$PORTING_SDK_DIR/scripts/meta_consistent.py" --port cpp --repo .

# Gate 19: ARTIFACT-DENY — no porting artifacts in the shipped package
#          (git-ls-files proxy; cpp has no CPack/install listing)
run_gate "ARTIFACT-DENY" "no porting artifacts in the PUBLISHED package (git ls-files proxy)" \
    python3 "$PORTING_SDK_DIR/scripts/artifact_deny.py" --port cpp --repo .

# --- Expansion gates (Tier 5 / release; blocking, non-report-only) ------------
# Five more shared porting-sdk gates. The backlog is burned to zero for cpp and
# the GEN_TYPE_DEGENERACY_ALLOW.md / ROUTE_COLLISION_ALLOW.md allowlists are
# user-approved, so all five pass enforcing. Four are pure host-side Python
# (BUILD_MODE-blind); ROUTE-COLLISION needs the route_registry binary and so is
# built + run per BUILD_MODE via route_collision_gate above. SEMVER-DIFF is wired
# below (Gate 25) — the version bump must match the API surface change vs the
# committed port_signatures.baseline.json floor (baseline_version 3.0.0).

# Gate 20: GEN-TYPE-DEGENERACY — generated typed surface isn't all loose aliases
run_gate "GEN-TYPE-DEGENERACY" "generated types aren't degenerate loose aliases / private-in-public (allowlist honored)" \
    python3 "$PORTING_SDK_DIR/scripts/gen_type_degeneracy.py" --port cpp --repo .

# Gate 21: PUBLIC-JARGON — no internal porting jargon in public doc comments
run_gate "PUBLIC-JARGON" "no internal porting jargon leaked into public doc comments" \
    python3 "$PORTING_SDK_DIR/scripts/public_jargon.py" --port cpp --repo .

# Gate 22: ROUTE-COLLISION — no route-split / crud-dup (registry-driven; allowlist honored)
run_gate "ROUTE-COLLISION" "no route-split/crud-dup between registry + surface (ROUTE_COLLISION_ALLOW.md honored)" \
    route_collision_gate

# Gate 23: GEN-IDIOM — generated code is NOT lint-excluded (runs through clang-tidy)
run_gate "GEN-IDIOM" "generated code is not lint-excluded from the idiom linter" \
    python3 "$PORTING_SDK_DIR/scripts/gen_idiom.py" --port cpp --repo .

# Gate 24: RELEASE-FRESH — publish workflow runs the gates BEFORE publishing
#          (cpp HAS a publish workflow: release.yml, gates-before-publish)
run_gate "RELEASE-FRESH" "publish workflow runs gates before publishing" \
    python3 "$PORTING_SDK_DIR/scripts/release_fresh.py" --port cpp --repo .

# Gate 25: SEMVER-DIFF — the CMakeLists project VERSION bump must match the API
#          surface change vs the committed port_signatures.baseline.json floor
#          (baseline_version 3.0.0). No public surface change since the last
#          release ⇒ 'none' required; a bump smaller than the surface delta reds.
run_gate "SEMVER-DIFF" "version bump matches API surface change vs port_signatures.baseline.json floor" \
    python3 "$PORTING_SDK_DIR/scripts/semver_diff.py" --port cpp --repo .

# --- §C1 doc/example execution gates ------------------------------------------
# SNIPPET-COMPILE syntax-checks every cpp fenced block WITH the SDK headers on the
# include path (g++ -fsyntax-only) — the heavy cpp doc gate (~11min). DOC-CLI
# line-detects documented swaig-test invocations (cheap → per-PR). The 3 heavy
# doc-execution gates are --tier=nightly: skipped on per-PR run-ci, run by the
# nightly workflow (and per-PR when the diff touches docs/examples via
# SW_CI_TIER=nightly). EXAMPLES-RUN/SNIPPET-RUN self-skip on cpp but stay in the
# nightly tier for a uniform full-doc sweep.
run_gate "SNIPPET-COMPILE" "documented code snippets compile" --tier=nightly \
    python3 "$PORTING_SDK_DIR/scripts/snippet_compile.py" --port cpp --repo .

run_gate "DOC-CLI" "documented swaig-test invocations parse against the real CLI" \
    python3 "$PORTING_SDK_DIR/scripts/doc_cli.py" --port cpp --repo .

# Wave-3 doc/API-truth gates — deterministic source/doc analysis (no build, no
# mock, ~1.3s for all six). Per-PR tier (default): cheap enough to catch doc/API
# drift at PR time rather than a day later in nightly.
run_gate "ERROR-ENVELOPE" "REST error carries the full (status,body,url,method) envelope + raised on >=400" \
    python3 "$PORTING_SDK_DIR/scripts/error_envelope.py" --port cpp --repo .
run_gate "DEAD-PUBLIC-ERROR" "exported error types are raised/caught/user-signalled (no dead error surface)" \
    python3 "$PORTING_SDK_DIR/scripts/dead_public_error.py" --port cpp --repo .
run_gate "PAGINATION-WIRED" "shipped iterator-protocol paginator is wired into list()" \
    python3 "$PORTING_SDK_DIR/scripts/pagination_wired.py" --port cpp --repo .
run_gate "DOC-ENV" "documented SIGNALWIRE_*/SWML_* env vars <=> code-read vars agree" \
    python3 "$PORTING_SDK_DIR/scripts/doc_env.py" --port cpp --repo .
run_gate "COUNT-CLAIM" "numeric doc claims (skills/namespaces) match reality" \
    python3 "$PORTING_SDK_DIR/scripts/count_claim.py" --port cpp --repo .
run_gate "ACCESSOR-TRUTH" "documented backtick method() refs exist in source" \
    python3 "$PORTING_SDK_DIR/scripts/accessor_truth.py" --port cpp --repo .

# --- gate-enforcement quartet (§2.1-2.4) --------------------------------------
# DOC-WIRE + STATUS-CLAIM run per-PR (catch a wrong doc wire key / a false status
# claim at PR time); WAIT-LIVENESS + STRICT-MOCKS run nightly (a real-time RELAY
# behavioral check + a second full strict RELAY pass are heavy).
run_gate "DOC-WIRE" "documented REST call shapes emit no unknown-field/query-param violations against the strict mock" \
    doc_wire_gate

run_gate "STATUS-CLAIM" "doc status claims (not-implemented/adapter/pending) match shipped reality" \
    python3 "$PORTING_SDK_DIR/scripts/status_claim.py" --port cpp --repo . \
        --surface "$PORT_ROOT/port_surface.json"

run_gate "WAIT-LIVENESS" "RELAY Action::wait() blocks-until-event liveness matches the python golden" --tier=nightly \
    wait_liveness_gate

run_gate "STRICT-MOCKS" "RELAY suite passes with the mock in 400-on-violation strict mode (MOCK_RELAY_STRICT=1)" --tier=nightly \
    strict_mocks_gate

# STRICT-MOCKS: carry MOCK_RELAY_STRICT=1 for parity with the other wired ports
# (python/typescript). Both self-skip on cpp (compiled: no run target), so this
# is a no-op today but keeps the env identical if/when either gains a runner.
run_gate "EXAMPLES-RUN" "shipped examples load/start against the mock (modulo EXAMPLES_RUN_ALLOW.md; STRICT-MOCKS: MOCK_RELAY_STRICT=1)" --tier=nightly \
    env MOCK_RELAY_STRICT=1 python3 "$PORTING_SDK_DIR/scripts/examples_run.py" --port cpp --repo .

run_gate "SNIPPET-RUN" "dynamic-port doc snippets run to a zero exit against the mock (compiled port: self-skips; STRICT-MOCKS: MOCK_RELAY_STRICT=1)" --tier=nightly \
    env MOCK_RELAY_STRICT=1 python3 "$PORTING_SDK_DIR/scripts/snippet_run.py" --port cpp --repo . --report-only

# --- §G anti-laundering ledger gate -------------------------------------------
# SUPPRESSION-LEDGER: no un-ledgered analyzer suppressions (complements the
# already-wired IGNORE-LEDGER-VERIFY DOC_AUDIT_IGNORE hygiene gate above).
run_gate "SUPPRESSION-LEDGER" "no un-ledgered analyzer suppressions" \
    python3 "$PORTING_SDK_DIR/scripts/suppression_ledger.py" --port cpp --repo .

# --- §D1 packaging gate -------------------------------------------------------
# PACKAGE-SMOKE: build the real cmake --install artifact into a clean prefix, then
# compile+link+construct RestClient from the INSTALLED headers/lib. Catches
# missing install() rules the in-tree tests never see. Heaviest gate → runs last.
run_gate "PACKAGE-SMOKE" "real artifact builds, installs, and imports from a clean prefix" --tier=nightly \
    python3 "$PORTING_SDK_DIR/scripts/package_smoke.py" --port cpp --repo .

if [ -z "$FAILED_GATES" ]; then
    echo "==> CI PASS"
    exit 0
else
    echo "==> CI FAIL (gates:$FAILED_GATES )"
    exit 1
fi
