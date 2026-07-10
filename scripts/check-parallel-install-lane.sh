#!/usr/bin/env bash
#
# check-parallel-install-lane.sh — structural gate for TASK-089 ("Wire
# check-parallel-install into per-PR CI + remove SKIP-degrades-to-pass paths").
#
# The v2-branch-gap audit flagged that the TASK-044 parallel-install
# acceptance gate (scripts/check-parallel-install.sh) was opt-in only
# (`make check-parallel-install`, never run in per-PR CI) AND degraded to
# `exit 0` on five environment-quirk paths — so it never actually gated
# anything. TASK-089 wires it into CI on the Linux gcc/libstdc++ lane and
# makes those five paths FAIL the job unless HTTPSERVER_ALLOW_PARALLEL_INSTALL_SKIP=1.
#
# The real end-to-end run (build v2, build v1 from master, assert SONAME
# coexistence) happens on the PR's Linux lane. This LOCAL structural gate
# asserts the workflow stays wired correctly, guarding against silent drift.
# It mirrors the self-testing scripts/check-*.sh gate idiom paired with a
# scripts/test_check_*.sh unit test (see check-valgrind-lane.sh, check-msan-lane.sh).
#
# Assertions against `.github/workflows/verify-build.yml`:
#   (a) the file parses as valid YAML (via python3 + PyYAML; NOT actionlint
#       or yamllint, which are not guaranteed present on the runner);
#   (b) at least one matrix `include:` entry carries `parallel-install: check`
#       (the opt-in key that turns the gate on for exactly one baseline lane);
#   (c) a step invokes `make check-parallel-install`;
#   (d) a step fetches the master ref (git fetch of refs/heads/master), so the
#       gate's Phase-2 v1 build has a real ref instead of a guaranteed SKIP on
#       the default shallow (fetch-depth: 2) checkout;
#   (e) the authorization env var is NOT set anywhere in the workflow, so the
#       five environment-quirk SKIP paths stay FATAL in CI (a set value would
#       silently re-open the SKIP-becomes-pass hole this task closed).
#
# Exit codes:
#   0  workflow is wired correctly
#   1  one or more assertions failed
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

WF="${1:-.github/workflows/verify-build.yml}"

echo "check-parallel-install-lane: inspecting $WF"

if [ ! -f "$WF" ]; then
    echo "check-parallel-install-lane: FAIL — workflow file not found: $WF" >&2
    exit 1
fi

fail=0

# (a) valid YAML  +  (b) an include entry carrying `parallel-install: check`.
# Both are structural: the shared locator (scripts/lib/find-matrix-includes.py)
# parses the file with PyYAML and emits strategy.matrix.include as JSON; the
# short per-gate check below inspects it.
if ! include_json="$(python3 scripts/lib/find-matrix-includes.py "$WF")" \
   || ! printf '%s' "$include_json" | python3 -c '
import json
import sys

include = json.load(sys.stdin)
opted_in = [e for e in include
            if isinstance(e, dict) and e.get("parallel-install") == "check"]
if not opted_in:
    print("  (b) no matrix entry carries `parallel-install: check`",
          file=sys.stderr)
    sys.exit(4)

print(f"  (a) YAML parses; "
      f"(b) {len(opted_in)} matrix entry(ies) carry parallel-install: check")
'
then
    echo "  (a)/(b) FAILED" >&2
    fail=1
fi

# (c) A step must invoke `make check-parallel-install`.
if grep -qF 'make check-parallel-install' "$WF"; then
    echo "  (c) make check-parallel-install invocation present"
else
    echo '  (c) no `make check-parallel-install` invocation found in the workflow' >&2
    fail=1
fi

# (d) A step must fetch the master ref so the v1 build has a real ref rather
# than a guaranteed SKIP on the default shallow checkout.
if grep -qE 'git fetch.*refs/heads/master' "$WF"; then
    echo "  (d) master-ref fetch step present"
else
    echo '  (d) no `git fetch ... refs/heads/master` step found in the workflow' >&2
    fail=1
fi

# (e) The SKIP-authorization env var must NOT appear anywhere in the workflow:
# in CI the five environment-quirk SKIP paths must stay fatal. (This gate greps
# for the literal token, so the workflow must never print it — not even in a
# comment; describe the escape hatch in prose without the variable name.)
if grep -qF 'HTTPSERVER_ALLOW_PARALLEL_INSTALL_SKIP' "$WF"; then
    echo "  (e) SKIP-authorization env var is set in the workflow — CI skips would not be fatal" >&2
    fail=1
else
    echo "  (e) SKIP-authorization env var absent — environment-quirk SKIPs stay fatal in CI"
fi

if [ "$fail" -ne 0 ]; then
    echo "check-parallel-install-lane: FAIL — the parallel-install lane is not fully wired" >&2
    exit 1
fi

echo "check-parallel-install-lane: PASS — parallel-install CI gate structurally wired"
