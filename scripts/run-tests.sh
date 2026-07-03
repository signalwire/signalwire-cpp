#!/usr/bin/env bash
# run-tests.sh — CANONICAL test runner for signalwire-cpp (cmake build + run_tests).
#
# This is the single entry point for running the test suite. Do NOT call cmake /
# ctest / ./build/run_tests directly; run-ci, agents, and humans all go through
# this script. It self-bootstraps its tool environment (via scripts/_env.sh) and
# runs from ANY directory.
#
# Configures the build tree if absent, builds the run_tests binary, then runs it.
# Exits non-zero on any test failure.
#
#   run-tests.sh [filter]   optional filter passed through to run_tests so a
#                           caller can run a subset (run_tests takes a positional
#                           substring filter, e.g. `run-tests.sh rest_mock_` or
#                           `run-tests.sh agent`). Omit to run the full suite.
#
# The shared mock servers (mock_signalwire / mock_relay) that the mock-backed
# tests hit self-terminate on parent death and honor the MOCK_*_PORT env-var
# escape hatch (porting-sdk contract) — this script does not need special mock
# hygiene beyond what the test harness already does.
#
# NOTE ON OPENSSL: the full run-ci TEST gate can route the build into an
# OpenSSL-3.0 Docker container on hosts stuck on OpenSSL 1.1.1 (see run-ci.sh's
# BUILD_MODE routing). This canonical script builds + runs on the HOST — the
# common developer/agent case (macOS + Homebrew OpenSSL 3). If your host OpenSSL
# is < 3.0, use `bash scripts/run-ci.sh` (which handles the container routing)
# for the TEST gate.

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/_env.sh"

cd "$REPO"

BUILD_DIR="${SWCPP_BUILD:-build}"

if [ ! -d "$BUILD_DIR" ]; then
    echo "==> $BUILD_DIR absent; configuring (cmake -S . -B $BUILD_DIR)" >&2
    cmake -S . -B "$BUILD_DIR" || exit 1
fi

# Build the test binary (the ONE heavy step). -j uses all cores.
cmake --build "$BUILD_DIR" --target run_tests -j || exit 1

# run_tests takes an optional positional substring filter.
exec "$BUILD_DIR/run_tests" "$@"
