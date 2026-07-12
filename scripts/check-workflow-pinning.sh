#!/usr/bin/env bash
#
# check-workflow-pinning.sh — structural gate for TASK-090 ("Harden CodeQL
# workflow and Codecov upload").
#
# Two supply-chain / observability weaknesses the v2-branch-gap audit flagged:
#   1. GitHub Actions in the CI workflows must ALL be pinned to full 40-hex
#      commit SHAs (a floating `@vN` tag is a mutable supply-chain surface).
#      This gate enforces the acceptance criterion "All GitHub Actions in BOTH
#      workflows are pinned to commit SHAs" across codeql-analysis.yml AND
#      verify-build.yml.
#   2. The Codecov upload in verify-build.yml ran with `fail_ci_if_error: false`,
#      so a broken/tokenless upload was silently non-blocking. TASK-090 flips it
#      to `true`: an UPLOAD failure now breaks CI. (A coverage-PERCENTAGE drop is
#      a SEPARATE gate — Codecov's own status checks driven by codecov.yml — not
#      this step.)
#
# It also guards the durable documentation of the Windows doxygen invariance
# exclusion so the rationale cannot silently vanish from verify-build.yml.
#
# This LOCAL structural gate mirrors the self-testing scripts/check-*.sh idiom
# paired with a scripts/test_check_*.sh unit test (see check-codeql-workflow.sh,
# check-valgrind-lane.sh). The CodeQL-specific structural assertions (autobuild
# gone, explicit build, matching init/analyze SHAs) live in
# check-codeql-workflow.sh; this gate covers the cross-workflow invariants.
#
# Assertions:
#   (a) both workflow files parse as valid YAML (python3 + PyYAML);
#   (b) NEITHER workflow carries a floating `uses: …@vN` action ref;
#   (c) verify-build.yml's Codecov upload sets `fail_ci_if_error: true` and
#       carries NO `fail_ci_if_error: false`;
#   (d) verify-build.yml still documents the Windows doxygen invariance
#       exclusion (the labelled rationale near the MinGW64 install step).
#
# Exit codes:
#   0  both workflows hardened correctly
#   1  one or more assertions failed
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

CODEQL_WF="${1:-.github/workflows/codeql-analysis.yml}"
VERIFY_WF="${2:-.github/workflows/verify-build.yml}"

echo "check-workflow-pinning: inspecting $CODEQL_WF and $VERIFY_WF"

fail=0

for wf in "$CODEQL_WF" "$VERIFY_WF"; do
    if [ ! -f "$wf" ]; then
        echo "check-workflow-pinning: FAIL — workflow file not found: $wf" >&2
        exit 1
    fi
done

# (a) both workflows parse as valid YAML.
for wf in "$CODEQL_WF" "$VERIFY_WF"; do
    if ! python3 - "$wf" <<'PY'
import sys
try:
    import yaml
except ImportError:
    print("  PyYAML not importable — install with 'pip install pyyaml'", file=sys.stderr)
    sys.exit(10)

path = sys.argv[1]
try:
    with open(path, encoding='utf-8') as fh:
        yaml.safe_load(fh)
except Exception as e:  # noqa: BLE001 - surface any parse failure
    print(f"  (a) YAML parse error in {path}: {e}", file=sys.stderr)
    sys.exit(2)
PY
    then
        echo "  (a) FAILED — $wf does not parse as YAML" >&2
        fail=1
    fi
done
[ "$fail" -eq 0 ] && echo "  (a) both workflows parse as YAML"

# (b) neither workflow carries a floating `uses: …@vN` action ref.
floating=0
for wf in "$CODEQL_WF" "$VERIFY_WF"; do
    if grep -nE 'uses:.*@v[0-9]+' "$wf"; then
        echo "  (b) floating-tag action ref(s) in $wf — pin to a 40-hex SHA" >&2
        floating=1
    fi
done
if [ "$floating" -ne 0 ]; then
    fail=1
else
    echo "  (b) no floating-tag (@vN) action refs in either workflow"
fi

# (c) the Codecov upload must hard-fail CI on an upload error.
if grep -qE 'fail_ci_if_error:[[:space:]]*false' "$VERIFY_WF"; then
    echo "  (c) verify-build.yml carries fail_ci_if_error: false — a Codecov upload failure is silently non-blocking" >&2
    fail=1
elif grep -qE 'fail_ci_if_error:[[:space:]]*true' "$VERIFY_WF"; then
    echo "  (c) Codecov upload sets fail_ci_if_error: true (upload failures break CI)"
else
    echo "  (c) verify-build.yml has no fail_ci_if_error: true on the Codecov upload" >&2
    fail=1
fi

# (d) the Windows doxygen invariance exclusion must stay documented.
if grep -qF 'Invariance exclusion (doxygen)' "$VERIFY_WF"; then
    echo "  (d) Windows doxygen invariance-exclusion rationale documented"
else
    echo "  (d) verify-build.yml lost the 'Invariance exclusion (doxygen)' rationale" >&2
    fail=1
fi

if [ "$fail" -ne 0 ]; then
    echo "check-workflow-pinning: FAIL — workflow pinning / Codecov / doxygen invariants not met" >&2
    exit 1
fi

echo "check-workflow-pinning: PASS — both workflows SHA-pinned, Codecov upload fails CI, doxygen exclusion documented"
