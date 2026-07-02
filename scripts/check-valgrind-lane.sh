#!/usr/bin/env bash
#
# check-valgrind-lane.sh — structural gate for TASK-088 ("Re-enable helgrind /
# drd / sgcheck in the valgrind CI lane").
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
#   (b) a matrix `include:` entry with `build-type: valgrind` exists;
#   (c) the valgrind configure branch no longer carries ANY of
#       --disable-valgrind-helgrind / --disable-valgrind-drd /
#       --disable-valgrind-sgcheck (grep must find zero);
#   (d) the "Run Valgrind checks" step still invokes `make check-valgrind`;
#   (e) the results-print step surfaces helgrind + drd logs
#       (test-suite-helgrind.log, test-suite-drd.log), not just memcheck.
#
# Exit codes:
#   0  workflow is wired correctly
#   1  one or more assertions failed
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

WF="${1:-.github/workflows/verify-build.yml}"

echo "check-valgrind-lane: inspecting $WF"

if [ ! -f "$WF" ]; then
    echo "check-valgrind-lane: FAIL — workflow file not found: $WF" >&2
    exit 1
fi

fail=0

# (a) valid YAML  +  (b) valgrind include entry.
# Both are structural, so PyYAML parses the file and inspects the matrix.
if ! python3 - "$WF" <<'PY'
import sys
try:
    import yaml
except ImportError:
    print("  PyYAML not importable — install with 'pip install pyyaml'", file=sys.stderr)
    sys.exit(10)

path = sys.argv[1]
try:
    doc = yaml.safe_load(open(path))
except Exception as e:  # noqa: BLE001 - surface any parse failure
    print(f"  (a) YAML parse error: {e}", file=sys.stderr)
    sys.exit(2)

# Locate the matrix include list in whichever job defines it (job name is
# not hard-coded so the gate survives a rename).
include = None
for job_name, job in (doc.get("jobs") or {}).items():
    if not isinstance(job, dict):
        continue
    inc = (((job.get("strategy") or {}).get("matrix") or {}).get("include"))
    if isinstance(inc, list):
        include = inc
        break
if include is None:
    print("  (b) could not locate strategy.matrix.include in any job", file=sys.stderr)
    sys.exit(3)

valgrind = [e for e in include if isinstance(e, dict) and e.get("build-type") == "valgrind"]
if not valgrind:
    print("  (b) no matrix include entry with build-type: valgrind", file=sys.stderr)
    sys.exit(4)

print(f"  (a) YAML parses; (b) valgrind include entry present "
      f"(count={len(valgrind)})")
PY
then
    echo "  (a)/(b) FAILED" >&2
    fail=1
fi

# (c) The valgrind configure branch must no longer disable helgrind/drd/sgcheck.
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

# (d) The "Run Valgrind checks" step must still invoke make check-valgrind.
if grep -qE 'make check-valgrind' "$WF"; then
    echo "  (d) 'make check-valgrind' invocation present"
else
    echo "  (d) 'make check-valgrind' invocation absent from the workflow" >&2
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

if [ "$fail" -ne 0 ]; then
    echo "check-valgrind-lane: FAIL — the valgrind lane is not fully wired" >&2
    exit 1
fi

echo "check-valgrind-lane: PASS — valgrind lane (memcheck + helgrind + drd) structurally wired"
