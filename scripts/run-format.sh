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
# Scope is EVERY first-party C++ tree — src/ include/ tools/ tests/ examples/
# rest/examples/ relay/examples/ — both the hand-written and the GENERATED
# headers (which are already clang-format-clean by construction, so --check stays
# green — AGENT_RULES §5).
#
# Widened 2026-07-30 from src/+include/+tools/ to the whole first-party tree.
# tests/ and examples/ were previously outside the fmt scope with no stated
# rationale — the only exclusion this repo ever justified is vendored code.
# Per the owner: "all the full directories should be linted and formatted
# including tests examples and all ... examples and tests are shipping code too."
# There is ONE bar and it is the bar the shipped library already meets.
#
# The ONLY thing still excluded is genuinely third-party code we do not own:
# vendored deps/ (httplib.h, json.hpp, nlohmann/) and the FetchContent
# IXWebSocket tree. Do not add a directory exclusion here for first-party code.

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/_env.sh"

cd "$REPO"

# Same source set as run-ci's fmt_gate: EVERY first-party C++ tree. rest/ and
# relay/ are listed by their examples/ subdir because the rest of those trees is
# markdown docs, not source.
fmt_sources() {
    find src include tools tests examples rest/examples relay/examples -type f \
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
