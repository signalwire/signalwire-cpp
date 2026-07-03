#!/usr/bin/env bash
# run-format.sh — CANONICAL formatter for signalwire-cpp (clang-format).
#
# This is the single entry point for formatting. Do NOT call clang-format
# directly; run-ci, agents, and humans all go through this script. It
# self-bootstraps its tool environment (via scripts/_env.sh — which pins
# clang-format to clang-18) and runs from ANY directory.
#
# Modes:
#   (default)   APPLY  — `clang-format -i`: reformat the tree in place, exit 0
#                        on success even if files changed.
#   --check     VERIFY — `clang-format --dry-run -Werror`: read-only, exit
#                        non-zero if anything is unformatted. This is the
#                        dual-mode CI FMT gate.
#
# Scope is the first-party src/ + include/ + tools/ trees ONLY — both the
# hand-written and the GENERATED headers (which are already clang-format-clean by
# construction, so --check stays green — AGENT_RULES §5). Vendored deps/
# (httplib.h, json.hpp, nlohmann/) and the FetchContent IXWebSocket tree are
# never touched.

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/_env.sh"

cd "$REPO"

# Same source set as run-ci's fmt_gate: first-party C++ under src/ include/ tools/.
fmt_sources() {
    find src include tools -type f \
        \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' -o -name '*.cc' \) \
        2>/dev/null
}

FILES="$(fmt_sources)"
[ -n "$FILES" ] || { echo "no C++ sources found to format" >&2; exit 1; }

if [ "${1:-}" = "--check" ]; then
    # shellcheck disable=SC2086
    exec clang-format --dry-run -Werror $FILES
else
    # shellcheck disable=SC2086
    clang-format -i $FILES
    if ! git diff --quiet 2>/dev/null; then
        echo "(FMT auto-applied formatting to your working tree — review & stage)"
    fi
    # A residual issue -i can't fix must still fail the gate.
    # shellcheck disable=SC2086
    clang-format --dry-run -Werror $FILES
fi
