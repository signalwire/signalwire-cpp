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
#   4. no-cheat gate                      — porting-sdk audit_no_cheat_tests.py
#   5. emission gate                      — porting-sdk diff_port_emission.py
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

# Build run_tests + emit_corpus + execute run_tests, honoring BUILD_MODE. Each
# branch produces the same observable result: a built run_tests (+ emit_corpus,
# which the EMISSION gate reuses) and run_tests' exit code. emit_corpus is built
# here too so the EMISSION gate doesn't reconfigure the tree (and so the host /
# exec modes leave a ready-to-run binary).
test_gate() {
    case "$BUILD_MODE" in
        host)
            if [ ! -d build ]; then
                echo "[BOOTSTRAP] cmake -S . -B build (initial configure)"
                cmake -S . -B build || return 1
            fi
            cmake --build build --target run_tests emit_corpus -j && ./build/run_tests
            ;;
        exec:*)
            local c="${BUILD_MODE#exec:}"
            docker exec "$c" bash -c "
                cmake -S '$SWCPP_CONTAINER_REPO' -B '$SWCPP_CONTAINER_BUILD' -DCMAKE_BUILD_TYPE=Release \
                && cmake --build '$SWCPP_CONTAINER_BUILD' --target run_tests emit_corpus -j\"\$(nproc)\" \
                && '$SWCPP_CONTAINER_BUILD/run_tests'"
            ;;
        run:*)
            local img="${BUILD_MODE#run:}"
            # Mount the repo's PARENT (so porting-sdk is adjacent for the mock
            # adjacency walk) and use --network host to reach host-run mocks.
            docker run --rm --network host -v "$(dirname "$PORT_ROOT")":/src "$img" bash -c "
                cmake -S '$SWCPP_CONTAINER_REPO' -B '$SWCPP_CONTAINER_BUILD' -DCMAKE_BUILD_TYPE=Release \
                && cmake --build '$SWCPP_CONTAINER_BUILD' --target run_tests emit_corpus -j\"\$(nproc)\" \
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

# Gate 4: no-cheat
run_gate "NO-CHEAT" "audit_no_cheat_tests" \
    python3 "$PORTING_SDK_DIR/scripts/audit_no_cheat_tests.py" --root "$PORT_ROOT"

# Gate 5: emission — byte-compare FunctionResult serialisation vs Python oracle
run_gate "EMISSION" "diff_port_emission vs python to_dict() (81-entry corpus)" \
    emission_gate

if [ -z "$FAILED_GATES" ]; then
    echo "==> CI PASS"
    exit 0
else
    echo "==> CI FAIL (gates:$FAILED_GATES )"
    exit 1
fi
