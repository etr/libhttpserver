#!/usr/bin/env bash
#
# check-complexity.sh — enforce per-function cyclomatic complexity gate.
#
# Wraps `lizard` (https://pypi.org/project/lizard/) and fails the build
# if any function under src/ exceeds the configured CCN ceiling.
#
# Scope: src/ (library code only). test/, examples/, and build/ are not
# scanned — test fixtures legitimately stress branching depth and are
# not the subject of this gate.
#
# Threshold knobs:
#   CCN_MAX       max cyclomatic complexity per function (default below)
#   LIZARD_EXTRA  extra flags passed through to lizard (e.g. --length=200)
#
# CCN_MAX is set to 10, matching the project's wider mccabe convention.
# The bar was reached on feature/v2.0 via an incremental refactor that
# retired the 14 v1 offenders one commit at a time, with CCN_MAX
# ratcheting down each step so each commit was both a refactor and a
# tighter gate. New offenders must be brought below 10 at the same
# commit they are introduced; lifting CCN_MAX is not allowed.
#
# Exit codes:
#   0  no violations
#   1  one or more functions exceed CCN_MAX
#   2  lizard not installed
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CCN_MAX="${CCN_MAX:-10}"

# Prefer the standalone `lizard` entrypoint if it's on PATH; fall back to
# `python3 -m lizard` which is what `pip install --user lizard` produces
# when the user's pip bin dir is not on PATH (common on macOS).
if command -v lizard >/dev/null 2>&1; then
    LIZARD=(lizard)
elif python3 -m lizard --version >/dev/null 2>&1; then
    LIZARD=(python3 -m lizard)
else
    echo "check-complexity: FAIL — lizard not installed" >&2
    echo "  install with: pip3 install lizard" >&2
    exit 2
fi

cd "$REPO_ROOT"

echo "check-complexity: scanning src/ with CCN_MAX=$CCN_MAX"
if ! "${LIZARD[@]}" -C "$CCN_MAX" --warnings_only ${LIZARD_EXTRA:-} src/; then
    echo "check-complexity: FAIL — one or more functions exceed CCN $CCN_MAX" >&2
    echo "  threshold lives in scripts/check-complexity.sh (CCN_MAX default)" >&2
    exit 1
fi

echo "check-complexity: PASS — no function exceeds CCN $CCN_MAX"
