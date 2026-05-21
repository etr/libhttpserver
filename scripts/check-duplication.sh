#!/usr/bin/env bash
#
# check-duplication.sh — enforce zero copy/paste duplication above a
# token-count threshold.
#
# Wraps PMD's CPD (Copy/Paste Detector) with the C++ tokenizer. CPD
# understands C++ tokens (templates, preprocessor, etc.) so it avoids
# the false positives a generic text-token tool would produce on a
# header-heavy library like this one.
#
# Scope: src/ (library code only). test/ and examples/ legitimately
# duplicate fixture and demo scaffolding and are not the subject of
# this gate.
#
# Threshold knob:
#   CPD_MIN_TOKENS  minimum token-run length flagged as a duplicate
#                   (default 100 — PMD's own default, a sensible C++
#                   starting point).
#
# Exit codes:
#   0  no duplicates above CPD_MIN_TOKENS
#   1  duplicate(s) found (PMD CPD exits 4 → remapped here to 1)
#   2  pmd not installed
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CPD_MIN_TOKENS="${CPD_MIN_TOKENS:-100}"

if ! command -v pmd >/dev/null 2>&1; then
    echo "check-duplication: FAIL — pmd not installed" >&2
    echo "  install with: brew install pmd  (or download from https://pmd.github.io/)" >&2
    exit 2
fi

cd "$REPO_ROOT"

echo "check-duplication: scanning src/ with CPD_MIN_TOKENS=$CPD_MIN_TOKENS"

# PMD CPD exit codes:
#   0  clean
#   4  duplicate(s) found (--fail-on-violation is true by default)
#   5  recoverable errors (e.g. a file failed to tokenize)
# We remap 4 -> 1 to fit the conventional "1 == policy violation" shell
# convention, and let 5 propagate as-is so a tokenizer crash isn't
# silently treated as a pass.
status=0
pmd cpd \
    --language cpp \
    --minimum-tokens "$CPD_MIN_TOKENS" \
    --dir src \
    --format text || status=$?

case "$status" in
    0)
        echo "check-duplication: PASS — no duplicates at or above $CPD_MIN_TOKENS tokens"
        ;;
    4)
        echo "check-duplication: FAIL — duplicate code at or above $CPD_MIN_TOKENS tokens" >&2
        echo "  threshold lives in scripts/check-duplication.sh (CPD_MIN_TOKENS default)" >&2
        exit 1
        ;;
    *)
        echo "check-duplication: FAIL — pmd cpd exited with status $status" >&2
        exit "$status"
        ;;
esac
