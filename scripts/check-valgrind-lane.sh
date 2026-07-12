#!/usr/bin/env bash
#
# check-valgrind-lane.sh — structural gate for TASK-088 ("Re-enable helgrind +
# drd in the valgrind CI lane (sgcheck intentionally off — removed upstream in
# Valgrind 3.20)").
#
# The v2-branch-gap audit flagged that the Valgrind lane in
# `.github/workflows/verify-build.yml` configured with
# `--disable-valgrind-helgrind --disable-valgrind-drd --disable-valgrind-sgcheck`,
# so only memcheck ever ran: the race detectors (helgrind, drd) and the
# stack-overrun checker (sgcheck) were silently off. TASK-088 re-enables
# helgrind + drd (sgcheck is intentionally left off — it was removed upstream
# in Valgrind 3.20, and ubuntu-latest ships 3.22).
#
# Valgrind is Linux-only, so the actual `make check-valgrind` run under
# helgrind/drd (and the triage of every finding) happens on the PR's Linux
# lane. This LOCAL structural gate asserts the workflow is wired correctly.
# It mirrors the self-testing `scripts/check-*.sh` gate idiom paired with a
# `scripts/test_check_*.sh` unit test (see check-msan-lane.sh).
#
# Assertions against `.github/workflows/verify-build.yml`:
#   (a) the file parses as valid YAML (via python3 + PyYAML; NOT actionlint
#       or yamllint, which are not guaranteed present on the runner);
#   (b) three separate matrix `include:` entries exist with build-type:
#       valgrind-memcheck, valgrind-helgrind, and valgrind-drd (parallel
#       lanes so GitHub Actions runs the three tools concurrently);
#   (c) the valgrind configure branch no longer carries ANY of
#       --disable-valgrind-helgrind / --disable-valgrind-drd /
#       --disable-valgrind-sgcheck (grep must find zero);
#   (d) the "Run Valgrind checks" step invokes a valid AX_VALGRIND_CHECK
#       per-tool target via make "check-valgrind-${TOOL}" (TOOL derived from
#       BUILD_TYPE); bare make "check-${TOOL}" without the "valgrind-" prefix
#       does not exist and must fail the gate;
#   (e) the results-print step surfaces helgrind + drd logs
#       (test-suite-helgrind.log, test-suite-drd.log), not just memcheck.
#
# Assertion against `configure.ac`:
#   (f) AX_VALGRIND_DFLT([sgcheck], ...) precedes AX_VALGRIND_CHECK (the
#       macro order autoconf requires for the sgcheck default to take
#       effect).
#
# Usage: check-valgrind-lane.sh [workflow-file] [configure.ac-path]
# Both paths default to the real repo files; the second positional argument
# exists so the unit test can point assertion (f) at a synthetic fixture.
#
# Exit codes:
#   0  workflow is wired correctly
#   1  one or more assertions failed
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

WF="${1:-.github/workflows/verify-build.yml}"
CONFIGURE_AC="${2:-configure.ac}"

echo "check-valgrind-lane: inspecting $WF"

if [ ! -f "$WF" ]; then
    echo "check-valgrind-lane: FAIL — workflow file not found: $WF" >&2
    exit 1
fi

fail=0

# (a) valid YAML  +  (b) valgrind include entries.
# Both are structural: the shared locator (scripts/lib/find-matrix-includes.py)
# parses the file with PyYAML and emits strategy.matrix.include as JSON; the
# short per-gate check below inspects it.
if ! include_json="$(python3 scripts/lib/find-matrix-includes.py "$WF")" \
   || ! printf '%s' "$include_json" | python3 -c '
import collections
import json
import sys

include = json.load(sys.stdin)
VALGRIND_TOOLS = {"valgrind-memcheck", "valgrind-helgrind", "valgrind-drd"}
build_types = [e["build-type"] for e in include
               if isinstance(e, dict) and e.get("build-type") in VALGRIND_TOOLS]
found = set(build_types)
if found != VALGRIND_TOOLS:
    missing = VALGRIND_TOOLS - found
    print(f"  (b) missing valgrind matrix entries: {missing}", file=sys.stderr)
    sys.exit(4)
dupes = {bt: n for bt, n in collections.Counter(build_types).items() if n > 1}
if dupes:
    print(f"  (b) duplicate valgrind matrix entries: {dupes}", file=sys.stderr)
    sys.exit(5)

print("  (a) YAML parses; (b) valgrind-memcheck + valgrind-helgrind"
      " + valgrind-drd entries present")
'
then
    echo "  (a)/(b) FAILED" >&2
    fail=1
fi

# (c) The valgrind configure branch must no longer disable helgrind/drd/sgcheck.
# NOTE: this grep is intentionally whole-file/literal (not scoped to the
# configure step), so a future YAML comment that happens to mention one of
# these exact --disable-valgrind-{helgrind,drd,sgcheck} strings would also
# trip this assertion. Avoid using these literal strings in unrelated
# comments elsewhere in the workflow file.
disabled=""
for flag in --disable-valgrind-helgrind --disable-valgrind-drd --disable-valgrind-sgcheck; do
    if grep -qF -- "$flag" "$WF"; then
        disabled="$disabled $flag"
    fi
done
if [ -n "$disabled" ]; then
    echo "  (c) valgrind lane still carries disable flags:$disabled" >&2
    fail=1
else
    echo "  (c) no --disable-valgrind-{helgrind,drd,sgcheck} flags remain"
fi

# (d) The "Run Valgrind checks" step must invoke a valid AX_VALGRIND_CHECK
# per-tool target. AX_VALGRIND_CHECK generates check-valgrind-<tool> targets
# (check-valgrind-memcheck, check-valgrind-helgrind, check-valgrind-drd);
# bare check-<tool> targets do NOT exist and would fail the make invocation.
# The step must use: TOOL="${BUILD_TYPE#valgrind-}" ; make "check-valgrind-${TOOL}"
if grep -qF 'check-valgrind-${TOOL}' "$WF"; then
    echo "  (d) per-tool valgrind check-valgrind-\${TOOL} invocation present"
else
    echo "  (d) per-tool check-valgrind-\${TOOL} invocation absent from the workflow" >&2
    fail=1
fi

# (e) The results-print step must surface helgrind + drd logs, not just memcheck.
if grep -qF 'test-suite-helgrind.log' "$WF"; then
    echo "  (e1) helgrind log surfaced in results-print step"
else
    echo "  (e1) test-suite-helgrind.log not surfaced in the workflow" >&2
    fail=1
fi
if grep -qF 'test-suite-drd.log' "$WF"; then
    echo "  (e2) drd log surfaced in results-print step"
else
    echo "  (e2) test-suite-drd.log not surfaced in the workflow" >&2
    fail=1
fi

# (f) configure.ac: AX_VALGRIND_DFLT([sgcheck], ...) must precede
# AX_VALGRIND_CHECK, or the sgcheck default is not honored.
if [ ! -f "$CONFIGURE_AC" ]; then
    echo "  (f) configure.ac not found at $CONFIGURE_AC" >&2
    fail=1
else
    # Skip comment-only lines so a prose mention of these macro names (e.g.
    # "AX_VALGRIND_DFLT calls must precede AX_VALGRIND_CHECK.") does not
    # itself satisfy the assertion.
    dflt_line="$(awk '/^[[:space:]]*#/ { next } /AX_VALGRIND_DFLT\(\[sgcheck\]/ { print NR; exit }' "$CONFIGURE_AC")"
    check_line="$(awk '/^[[:space:]]*#/ { next } /AX_VALGRIND_CHECK/ { print NR; exit }' "$CONFIGURE_AC")"
    if [ -z "$dflt_line" ] || [ -z "$check_line" ]; then
        echo "  (f) configure.ac missing AX_VALGRIND_DFLT([sgcheck]...) and/or AX_VALGRIND_CHECK" >&2
        fail=1
    elif [ "$dflt_line" -ge "$check_line" ]; then
        echo "  (f) AX_VALGRIND_DFLT([sgcheck]...) (line $dflt_line) must precede AX_VALGRIND_CHECK (line $check_line)" >&2
        fail=1
    else
        echo "  (f) AX_VALGRIND_DFLT([sgcheck]...) precedes AX_VALGRIND_CHECK in configure.ac"
    fi
fi

if [ "$fail" -ne 0 ]; then
    echo "check-valgrind-lane: FAIL — the valgrind lane is not fully wired" >&2
    exit 1
fi

echo "check-valgrind-lane: PASS — valgrind lanes (memcheck + helgrind + drd, parallel) structurally wired"
