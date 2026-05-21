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
# CCN_MAX is intentionally elevated above the long-term target (10,
# matching the project's wider mccabe convention) while the incremental
# refactor that drives every existing offender below the line lands on
# feature/v2.0. Each refactor commit ratchets CCN_MAX one step lower
# until the final value of 10 is reached. The default below is updated
# in the same commit that retires each offender.
#
# Exit codes:
#   0  no violations
#   1  one or more functions exceed CCN_MAX
#   2  lizard not installed
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CCN_MAX="${CCN_MAX:-29}"

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
